#include "Core.hpp"
#include "Log.hpp"
#include "Client/Client.hpp"
#include <cmath>
#include <algorithm>

static OmegaClient g_client;
static bool g_network_enabled = false;
static bool ShowInventory = false;

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

// ---------------------------------------------------------------------------
// Inventory overlay (draw when Tab pressed)
// ---------------------------------------------------------------------------
static void DrawInventoryOverlay() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int panel_w = 500;
    const int panel_h = 420;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = (sh - panel_h) / 2;

    // Dim background
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});

    // Panel background
    DrawRectangle(panel_x, panel_y, panel_w, panel_h, (Color){30, 30, 40, 240});
    DrawRectangleLines(panel_x, panel_y, panel_w, panel_h, WHITE);

    // Title
    DrawText("INVENTORY", panel_x + 10, panel_y + 10, 20, WHITE);

    // Stats section (left side)
    int sx = panel_x + 20;
    int sy = panel_y + 45;
    DrawText("-- Player Stats --", sx, sy, 14, LIGHTGRAY);
    sy += 22;

    // Level
    DrawText(TextFormat("Level: %d", OmegaPlayer.Level), sx, sy, 14, WHITE);
    sy += 18;

    // XP
    DrawText(TextFormat("XP: %d / %d", OmegaPlayer.XP, OmegaPlayer.XPToNext), sx, sy, 14, WHITE);
    sy += 22;

    // Health
    DrawText(TextFormat("Health: %d / %d", (int)OmegaPlayer.Health, (int)OmegaPlayer.MaxHealth), sx, sy, 14, RED);
    sy += 20;

    // Mana
    DrawText(TextFormat("Mana: %d / %d", (int)OmegaPlayer.Mana, (int)OmegaPlayer.MaxMana), sx, sy, 14, BLUE);
    sy += 20;

    // Psychic Energy
    DrawText(TextFormat("P.Energy: %d / %d", (int)OmegaPlayer.PsychicEnergy, (int)OmegaPlayer.MaxPsychicEnergy), sx, sy, 14, PURPLE);
    sy += 30;

    // Objects / Items section (right side)
    int ox = panel_x + 260;
    int oy = panel_y + 45;
    DrawText("-- Items --", ox, oy, 14, LIGHTGRAY);
    oy += 22;

    // Collect object names with own status
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

        // Draw slot background
        DrawRectangle(ox, oy, 200, 36, bg_color);
        DrawRectangleLines(ox, oy, 200, 36, slot_color);

        // Slot number
        DrawText(TextFormat("[%d]", i + 1), ox + 6, oy + 8, 14, slot_color);

        // Item name (or "Empty")
        if (slots[i].owned && !slots[i].name.empty()) {
            std::string narrow(slots[i].name.begin(), slots[i].name.end());
            DrawText(narrow.c_str(), ox + 36, oy + 8, 14, slot_color);
        } else if (slots[i].owned) {
            DrawText("Item", ox + 36, oy + 8, 14, slot_color);
        } else {
            DrawText("Empty", ox + 36, oy + 8, 14, (Color){60, 60, 60, 255});
        }

        oy += 40;
    }

    // Controls hint
    DrawText("ARROW KEYS / WHEEL: select  |  TAB: close",
             panel_x + 20, panel_y + panel_h - 30, 12, DARKGRAY);

    // Handle slot selection via keyboard
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_RIGHT)) {
        if (SelectedObject < 5) SelectedObject++;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_LEFT)) {
        if (SelectedObject > 1) SelectedObject--;
    }
    // Mouse wheel
    if (GetMouseWheelMove() < 0 && SelectedObject < 5) SelectedObject++;
    if (GetMouseWheelMove() > 0 && SelectedObject > 1) SelectedObject--;

    // Draw selection highlight
    int sel_y = panel_y + 45 + 22 + (SelectedObject - 1) * 40;
    DrawRectangleLines(panel_x + 260, sel_y, 200, 36, YELLOW);
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

    // Try connecting to dedicated server (hardcoded for now)
    g_client.set_on_chat_received([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });

    g_network_enabled = g_client.connect("127.0.0.1", 27015);
    if (g_network_enabled) {
        OZ_INFO("Network: connected to 127.0.0.1:27015");
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

        OmegaPlayer.OldX = OmegaTechData.MainCamera.position.x;
        OmegaPlayer.OldY = OmegaTechData.MainCamera.position.y;
        OmegaPlayer.OldZ = OmegaTechData.MainCamera.position.z;

        if (!OmegaInputController.InteractDown && !ShowSettings && !ShowInventory){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

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
                // Sync XP/level from server to local player
                OmegaPlayer.Level = g_client.get_level();
                OmegaPlayer.XP = g_client.get_xp();
                OmegaPlayer.XPToNext = g_client.get_xp_to_next();

                // Copy server NPCs into OmegaEnemy array for rendering
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