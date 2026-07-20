#include "Core.hpp"
#include "Log.hpp"
#include "Client/Client.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

static OmegaClient g_client;
static bool g_network_enabled = false;
static bool ShowInventory = false;

// Local projectiles (fired by this client)
static struct LocalProjectile {
    Vector3 origin;
    Vector3 direction;
    float spawn_time;
    int weapon_type;
} g_local_projectiles[32];
static int g_local_proj_count = 0;

static float now_f() {
    return static_cast<float>(GetTime());
}

// ---------------------------------------------------------------------------
// HUD: draw player stats bars (always visible)
// ---------------------------------------------------------------------------
static void DrawPlayerHUD() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int bar_w = 220;
    const int bar_h = 14;
    const int pad = 6;
    int x = pad;
    int y = pad;

    // Level
    DrawText(TextFormat("Lv.%d", OmegaPlayer.Level), x, y, 14, WHITE);
    y += 18;

    // XP bar
    int xp_w = bar_w - 40;
    DrawText("XP", x, y, 12, LIGHTGRAY);
    if (OmegaPlayer.XPToNext > 0) {
        float xp_pct = (float)OmegaPlayer.XP / OmegaPlayer.XPToNext;
        DrawRectangle(x + 30, y, xp_w, bar_h, (Color){30, 30, 30, 255});
        DrawRectangle(x + 30, y, (int)(xp_pct * xp_w), bar_h, SKYBLUE);
        DrawText(TextFormat("%d/%d", OmegaPlayer.XP, OmegaPlayer.XPToNext),
                 x + 34, y + 1, 10, WHITE);
    }
    y += bar_h + pad;

    // Health bar
    float hp_pct = (OmegaPlayer.MaxHealth > 0)
        ? (OmegaPlayer.Health / OmegaPlayer.MaxHealth) : 0;
    DrawText(TextFormat("HP %d/%d", (int)OmegaPlayer.Health, (int)OmegaPlayer.MaxHealth),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){50, 10, 10, 255});
    DrawRectangle(x, y + 14, (int)(hp_pct * bar_w), bar_h, RED);
    y += 14 + bar_h + pad;

    // Mana bar
    float mp_pct = (OmegaPlayer.MaxMana > 0)
        ? (OmegaPlayer.Mana / OmegaPlayer.MaxMana) : 0;
    DrawText(TextFormat("MP %d/%d", (int)OmegaPlayer.Mana, (int)OmegaPlayer.MaxMana),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){10, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(mp_pct * bar_w), bar_h, BLUE);
    y += 14 + bar_h + pad;

    // Psychic Energy bar
    float pe_pct = (OmegaPlayer.MaxPsychicEnergy > 0)
        ? (OmegaPlayer.PsychicEnergy / OmegaPlayer.MaxPsychicEnergy) : 0;
    DrawText(TextFormat("PE %d/%d", (int)OmegaPlayer.PsychicEnergy, (int)OmegaPlayer.MaxPsychicEnergy),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){40, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(pe_pct * bar_w), bar_h, PURPLE);
}

static Color unpack_color(uint32_t packed) {
    return (Color){
        (unsigned char)(packed & 0xFF),
        (unsigned char)((packed >> 8) & 0xFF),
        (unsigned char)((packed >> 16) & 0xFF),
        (unsigned char)((packed >> 24) & 0xFF)
    };
}

// ---------------------------------------------------------------------------
// Remote player rendering
// ---------------------------------------------------------------------------
static void DrawRemotePlayers() {
    if (!g_network_enabled || !g_client.is_connected()) return;
    const auto& players = g_client.remote_players();
    for (const auto& rp : players) {
        if (!rp.active) continue;
        Vector3 pos = {rp.position.x, rp.position.y, rp.position.z};
        float height = 8.0f;
        float radius = 1.5f;
        Color col = unpack_color(rp.color_packed);

        // Body (cylinder)
        DrawCylinder(pos, radius, radius, height, 8, col);
        // Head (sphere on top)
        Vector3 head_pos = {pos.x, pos.y + height + 1.0f, pos.z};
        DrawSphere(head_pos, 1.2f, col);
        // Direction indicator (small cone)
        Vector3 dir_end = {
            pos.x + sinf(rp.yaw) * 3.0f,
            pos.y + height * 0.5f,
            pos.z + cosf(rp.yaw) * 3.0f
        };
        DrawLine3D({pos.x, pos.y + height * 0.5f, pos.z}, dir_end, YELLOW);
    }
}

// ---------------------------------------------------------------------------
// Projectile rendering
// ---------------------------------------------------------------------------
static void DrawActiveProjectiles() {
    float cur = now_f();
    float proj_speed = 30.0f;
    float lifetime = 1.5f;

    // Draw client-received projectiles
    if (g_network_enabled && g_client.is_connected()) {
        for (const auto& p : g_client.projectiles()) {
            float age = cur - p.spawn_time;
            if (age < 0 || age > lifetime) continue;
            float t = age / lifetime;
            float dist = t * proj_speed;
            Vector3 pos = {
                p.origin.x + p.direction.x * dist,
                p.origin.y + p.direction.y * dist,
                p.origin.z + p.direction.z * dist
            };
            float alpha = 1.0f - t;
            Color c = unpack_color(p.color_packed);
            c.a = (unsigned char)(alpha * 255);
            DrawSphere(pos, 0.8f + t * 0.5f, c);
            DrawSphereWires(pos, 1.0f + t * 0.8f, 6, 6, (Color){c.r, c.g, c.b, (unsigned char)(alpha * 80)});
        }
    }

    // Draw local projectiles
    for (int i = 0; i < g_local_proj_count; i++) {
        float age = cur - g_local_projectiles[i].spawn_time;
        if (age < 0 || age > lifetime) continue;
        float t = age / lifetime;
        float dist = t * proj_speed;
        Vector3 pos = {
            g_local_projectiles[i].origin.x + g_local_projectiles[i].direction.x * dist,
            g_local_projectiles[i].origin.y + g_local_projectiles[i].direction.y * dist,
            g_local_projectiles[i].origin.z + g_local_projectiles[i].direction.z * dist
        };
        float alpha = 1.0f - t;
        Color c = {0, 200, 255, (unsigned char)(alpha * 255)};
        DrawSphere(pos, 0.8f + t * 0.5f, c);
        DrawSphereWires(pos, 1.0f + t * 0.8f, 6, 6, (Color){0, 200, 255, (unsigned char)(alpha * 80)});
    }
}

// ---------------------------------------------------------------------------
// Fire weapon helper
// ---------------------------------------------------------------------------
static void FireWeapon() {
    Camera3D& cam = OmegaTechData.MainCamera;

    // Get camera forward direction
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));

    // Calculate origin slightly in front of camera
    Vector3 origin = Vector3Add(cam.position, Vector3Scale(forward, 2.0f));

    // Add local projectile
    if (g_local_proj_count < 32) {
        int idx = g_local_proj_count++;
        g_local_projectiles[idx].origin = origin;
        g_local_projectiles[idx].direction = forward;
        g_local_projectiles[idx].spawn_time = now_f();
        g_local_projectiles[idx].weapon_type = 1;
    }

    // Send to server
    if (g_network_enabled && g_client.is_connected()) {
        g_client.send_weapon_fire(
            origin.x, origin.y, origin.z,
            forward.x, forward.y, forward.z,
            1, 10);
    }
}

// ---------------------------------------------------------------------------
// Inventory overlay (draw when Tab pressed)
// ---------------------------------------------------------------------------
static void DrawInventoryOverlay() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int panel_w = 640;
    const int panel_h = 480;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = (sh - panel_h) / 2;

    // Dim background
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});

    // Panel background
    DrawRectangle(panel_x, panel_y, panel_w, panel_h, (Color){30, 30, 40, 240});
    DrawRectangleLines(panel_x, panel_y, panel_w, panel_h, WHITE);

    // Title
    DrawText("INVENTORY", panel_x + 10, panel_y + 10, 20, WHITE);

    // Stats section (left column)
    int sx = panel_x + 20;
    int sy = panel_y + 45;
    DrawText("-- Player Stats --", sx, sy, 14, LIGHTGRAY);
    sy += 22;

    DrawText(TextFormat("Level: %d", OmegaPlayer.Level), sx, sy, 14, WHITE);
    sy += 18;
    DrawText(TextFormat("XP: %d / %d", OmegaPlayer.XP, OmegaPlayer.XPToNext), sx, sy, 14, WHITE);
    sy += 22;
    DrawText(TextFormat("Health: %d / %d", (int)OmegaPlayer.Health, (int)OmegaPlayer.MaxHealth), sx, sy, 14, RED);
    sy += 20;
    DrawText(TextFormat("Mana: %d / %d", (int)OmegaPlayer.Mana, (int)OmegaPlayer.MaxMana), sx, sy, 14, BLUE);
    sy += 20;
    DrawText(TextFormat("P.Energy: %d / %d", (int)OmegaPlayer.PsychicEnergy, (int)OmegaPlayer.MaxPsychicEnergy), sx, sy, 14, PURPLE);

    // Items section (middle column)
    int ox = panel_x + 240;
    int oy = panel_y + 45;
    DrawText("-- Objects --", ox, oy, 14, LIGHTGRAY);
    oy += 22;

    struct ObjSlot {
        wstring name;
        bool owned;
    };
    ObjSlot slots[5] = {
        {OmegaTechGameObjects.Object1Name, OmegaTechGameObjects.Object1Owned},
        {OmegaTechGameObjects.Object2Name, OmegaTechGameObjects.Object2Owned},
        {OmegaTechGameObjects.Object3Name, OmegaTechGameObjects.Object3Owned},
        {OmegaTechGameObjects.Object4Name, OmegaTechGameObjects.Object4Owned},
        {OmegaTechGameObjects.Object5Name, OmegaTechGameObjects.Object5Owned},
    };

    for (int i = 0; i < 5; i++) {
        Color slot_color = slots[i].owned ? WHITE : (Color){80, 80, 80, 255};
        Color bg_color = slots[i].owned ? (Color){40, 40, 60, 255} : (Color){20, 20, 25, 255};

        DrawRectangle(ox, oy, 180, 30, bg_color);
        DrawRectangleLines(ox, oy, 180, 30, slot_color);

        DrawText(TextFormat("[%d]", i + 1), ox + 6, oy + 6, 12, slot_color);

        if (slots[i].owned && !slots[i].name.empty()) {
            std::string narrow(slots[i].name.begin(), slots[i].name.end());
            DrawText(narrow.c_str(), ox + 30, oy + 6, 12, slot_color);
        } else if (slots[i].owned) {
            DrawText("Item", ox + 30, oy + 6, 12, slot_color);
        } else {
            DrawText("Empty", ox + 30, oy + 6, 12, (Color){60, 60, 60, 255});
        }
        oy += 34;
    }

    // Armory section (right column)
    int ax = panel_x + 440;
    int ay = panel_y + 45;
    DrawText("-- Equipment --", ax, ay, 14, LIGHTGRAY);
    ay += 22;

    struct EquipSlot {
        wstring name;
        bool owned;
        const char* label;
    };
    EquipSlot equip[4] = {
        {OmegaTechGameObjects.Armory1Name, OmegaTechGameObjects.Armory1Owned, "Armor"},
        {OmegaTechGameObjects.Armory2Name, OmegaTechGameObjects.Armory2Owned, "Armor"},
        {OmegaTechGameObjects.Jewelry1Name, OmegaTechGameObjects.Jewelry1Owned, "Jewel"},
        {OmegaTechGameObjects.Jewelry2Name, OmegaTechGameObjects.Jewelry2Owned, "Jewel"},
    };

    for (int i = 0; i < 4; i++) {
        Color slot_color = equip[i].owned ? WHITE : (Color){80, 80, 80, 255};
        Color bg_color = equip[i].owned ? (Color){40, 50, 40, 255} : (Color){20, 25, 20, 255};

        DrawRectangle(ax, ay, 180, 30, bg_color);
        DrawRectangleLines(ax, ay, 180, 30, slot_color);

        DrawText(TextFormat("%s", equip[i].label), ax + 6, ay + 6, 12, slot_color);

        if (equip[i].owned && !equip[i].name.empty()) {
            std::string narrow(equip[i].name.begin(), equip[i].name.end());
            DrawText(narrow.c_str(), ax + 60, ay + 6, 12, slot_color);
        } else if (equip[i].owned) {
            DrawText("Equipped", ax + 60, ay + 6, 12, slot_color);
        } else {
            DrawText("Empty", ax + 60, ay + 6, 12, (Color){60, 60, 60, 255});
        }
        ay += 34;
    }

    // Controls hint
    DrawText("ARROW KEYS / WHEEL: select  |  E: pickup  |  LMB: fire  |  TAB: close",
             panel_x + 20, panel_y + panel_h - 30, 12, DARKGRAY);
}

int main(){
    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(1280 , 720 , "Project Omega");
    SetTargetFPS(60);

    InitAudioDevice();

    if (!IsAudioDeviceReady()){
        CloseAudioDevice();
    }

    OmegaTechInit();
    PlayHomeScreen();
    
    LoadWorld();

    // Connect to server (if Join Game was selected, use custom IP)
    g_client.set_on_chat_received([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });

    if (SetServerJoinFlag && SetServerJoinIP) {
        g_network_enabled = g_client.connect(SetServerJoinIP, 27015);
        if (g_network_enabled) {
            OZ_INFO("Network: connected to %s:27015", SetServerJoinIP);
        }
        SetServerJoinFlag = false;
    } else {
        g_network_enabled = g_client.connect("127.0.0.1", 27015);
        if (g_network_enabled) {
            OZ_INFO("Network: connected to 127.0.0.1:27015");
        }
    }

    HideCursor(); 
    DisableCursor();  
    
    while (!WindowShouldClose())
    {
        // Tab key toggles inventory
        if (IsKeyPressed(KEY_TAB)) {
            ShowInventory = !ShowInventory;
            if (ShowInventory) {
                ShowCursor();
                EnableCursor();
            } else {
                HideCursor();
                DisableCursor();
            }
        }

        // Capture left-mouse state before camera (handle after)
        static bool left_click_was_down = false;
        bool left_click_now = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool left_just_pressed = left_click_now && !left_click_was_down;

        OmegaPlayer.OldX = OmegaTechData.MainCamera.position.x;
        OmegaPlayer.OldY = OmegaTechData.MainCamera.position.y;
        OmegaPlayer.OldZ = OmegaTechData.MainCamera.position.z;

        if (!ShowSettings && !ShowInventory){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

        // Weapon fire AFTER camera so left-click does not disrupt movement
        if (!ShowInventory && left_just_pressed) {
            if (SelectedObject == 1 &&
                OmegaTechGameObjects.Object1Owned) {
                FireWeapon();
            }
        }

        left_click_was_down = left_click_now;

        OmegaInputController.UpdateInputs();
        UpdateLightSources();

        if (Direction == 1)
        {
            R += 1;
            G += 1;
            B += 1;

            if (R == 254)
            {
                FadeDone = true;
                Direction = 3;
            }
        }

        FadeColor = (Color){R, G, B, 255};
    
        DrawWorld();
        
        BeginDrawing();  

        ClearBackground(BLACK);

        DrawTexturePro(Target.texture, (Rectangle){ 0, 0, Target.texture.width, -Target.texture.height }, (Rectangle){ 0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, (Vector2){ 0, 0 } , 0.f , WHITE);
        UpdateObjectBar();
        EndShaderMode();

        if (ParticlesEnabled){
            OmegaTechData.RainParticles.Update(0,0);
            OmegaTechData.RainParticles.TriggerEffect({0,0} , RainEffect);
        }

        if (FPSEnabled){
            DrawFPS(0,0);
        }

        UpdateSettings();

        OmegaTechTextSystem.Update();

        // Network client update
        if (g_network_enabled) {
            Camera3D& cam = OmegaTechData.MainCamera;
            g_client.update(cam.position.x, cam.position.y, cam.position.z,
                           0.0f, 0.0f);

            if (g_client.is_connected()) {
                OmegaPlayer.Level = g_client.get_level();
                OmegaPlayer.XP = g_client.get_xp();
                OmegaPlayer.XPToNext = g_client.get_xp_to_next();

                const auto& npcs = g_client.npcs();
                int limit = std::min((int)npcs.size(), EntityCount);
                for (int i = 0; i < limit; i++) {
                    OmegaEnemy[i].X = npcs[i].position.x;
                    OmegaEnemy[i].Y = npcs[i].position.y;
                    OmegaEnemy[i].Z = npcs[i].position.z;
                    OmegaEnemy[i].IsActive = npcs[i].active;
                }

                // Pickup collection check (press E near a pickup)
                if (IsKeyPressed(KEY_E)) {
                    const auto& pickups = g_client.pickups();
                    Vector3 cp = OmegaTechData.MainCamera.position;
                    float nearest_dist = 5.0f;
                    int nearest_pickup = -1;
                    int nearest_world = 0;
                    for (const auto& p : pickups) {
                        if (!p.active) continue;
                        float dx = p.position.x - cp.x;
                        float dz = p.position.z - cp.z;
                        float dist = sqrtf(dx*dx + dz*dz);
                        if (dist < nearest_dist) {
                            nearest_dist = dist;
                            nearest_pickup = p.id;
                            nearest_world = p.world_index;
                        }
                    }
                    if (nearest_pickup >= 0) {
                        g_client.send_pickup_collect(nearest_pickup, nearest_world);
                    }
                }
            }
        }

        // HUD: player stats (always visible)
        DrawPlayerHUD();

        // Network ping (only when connected)
        if (g_network_enabled && g_client.is_connected()) {
            DrawText(TextFormat("Ping: %dms", g_client.get_ping_ms()),
                     10, GetScreenHeight() - 20, 12, GREEN);
        }

        // Remote player models
        DrawRemotePlayers();

        // Active projectiles
        DrawActiveProjectiles();

        // Inventory overlay
        if (ShowInventory) {
            DrawInventoryOverlay();
        }

        EndDrawing();

        if (IsKeyPressed(KEY_F11))ToggleFullscreen();

    }
    
    g_client.disconnect();
    CloseWindow();
}