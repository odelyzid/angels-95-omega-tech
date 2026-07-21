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
    y += 14 + bar_h + pad;

    // Current selected item/weapon
    const char* slotLabel = nullptr;
    if (SelectedObject >= 1 && SelectedObject <= 5) {
        switch (SelectedObject) {
            case 1: slotLabel = OmegaTechGameObjects.Object1Owned ? "Weapon 1" : "Empty"; break;
            case 2: slotLabel = OmegaTechGameObjects.Object2Owned ? "Weapon 2" : "Empty"; break;
            case 3: slotLabel = OmegaTechGameObjects.Object3Owned ? "Weapon 3" : "Empty"; break;
            case 4: slotLabel = OmegaTechGameObjects.Object4Owned ? "Weapon 4" : "Empty"; break;
            case 5: slotLabel = OmegaTechGameObjects.Object5Owned ? "Weapon 5" : "Empty"; break;
        }
    } else if (SelectedObject >= 6 && SelectedObject <= 8) {
        int bpIdx = SelectedObject - 6;
        int count = 0;
        for (int b = 0; b < BACKPACK_SLOTS; b++) {
            if (gInventory.backpack[b].itemId != -1) {
                if (count == bpIdx) {
                    const ItemDBEntry* def = GetItemDef(gInventory.backpack[b].itemId);
                    slotLabel = def ? def->name : "Item";
                    break;
                }
                count++;
            }
        }
        if (!slotLabel) slotLabel = "Empty";
    }
    DrawText(TextFormat("Slot %d: %s", SelectedObject, slotLabel ? slotLabel : "None"),
             x, y, 12, YELLOW);
    y += 16;

    // Weapon fire indicator (brief pulse)
    static double lastFireTime = 0;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        lastFireTime = GetTime();
    }
    if (GetTime() - lastFireTime < 0.15) {
        DrawText("FIRE", x, y, 20, RED);
    }

    // Coordinates (top-right)
    Camera3D& cam = OmegaTechData.MainCamera;
    float yaw = -atan2f(cam.target.x - cam.position.x, cam.target.z - cam.position.z) * RAD2DEG;
    float pitch = asinf((cam.target.y - cam.position.y) /
        Vector3Distance(cam.position, cam.target)) * RAD2DEG;
    int rx = sw - 280;
    DrawText(TextFormat("Pos: %.1f %.1f %.1f", cam.position.x, cam.position.y, cam.position.z),
             rx, 10, 14, LIGHTGRAY);
    DrawText(TextFormat("Rot: %.0f %.0f", yaw, pitch),
             rx, 28, 14, LIGHTGRAY);
    if ((OmegaTechData.Ticker % 60) == 0) {
        fprintf(stderr, "POS: %.1f %.1f %.1f  ROT: %.0f %.0f\n",
                cam.position.x, cam.position.y, cam.position.z, yaw, pitch);
    }
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
// ---- Debug Console ----
static bool g_consoleOpen = false;
static char g_consoleBuf[256] = "";
static int g_consoleCursor = 0;
static wstring g_consoleHistory[16];
static int g_consoleHistoryCount = 0;
static int g_consoleHistoryPos = 0;

static void ExecuteConsoleCommand(const char* cmd) {
    if (!cmd || !cmd[0]) return;
    // Store in history
    if (g_consoleHistoryCount < 16) {
        size_t len = strlen(cmd);
        for (size_t i = 0; i < len; i++) g_consoleHistory[g_consoleHistoryCount] += (wchar_t)cmd[i];
        g_consoleHistoryCount++;
    }
    g_consoleHistoryPos = g_consoleHistoryCount;

    // Parse command
    if (strncmp(cmd, "/summon ", 8) == 0) {
        const char* arg = cmd + 8;
        // Skip leading spaces
        while (*arg == ' ') arg++;
        if (!*arg) return;

        // Try matching item name
        int itemId = gInventory.SummonItem(arg);
        if (itemId >= 0) {
            gInventory.AddToBackpack(itemId, 1);
            return;
        }

        // Try matching weapon name
        struct { const char* name; int slot; } weapons[] = {
            {"wand", 1}, {"weapon1", 1}, {"object1", 1},
            {"weapon2", 2}, {"object2", 2},
            {"weapon3", 3}, {"object3", 3},
            {"weapon4", 4}, {"object4", 4},
            {"weapon5", 5}, {"object5", 5},
        };
        for (auto& w : weapons) {
            const char* a = arg;
            const char* b = w.name;
            bool match = true;
            while (*a && *b) {
                char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
                char cb = *b;
                if (ca != cb) { match = false; break; }
                a++; b++;
            }
            if (match && *a == *b) {
                switch (w.slot) {
                    case 1: OmegaTechGameObjects.Object1Owned = true; break;
                    case 2: OmegaTechGameObjects.Object2Owned = true; break;
                    case 3: OmegaTechGameObjects.Object3Owned = true; break;
                    case 4: OmegaTechGameObjects.Object4Owned = true; break;
                    case 5: OmegaTechGameObjects.Object5Owned = true; break;
                }
                return;
            }
        }
    }
}

static void DrawConsole() {
    if (!g_consoleOpen) return;
    int sw = GetScreenWidth();
    int ch = 200;
    int cy = GetScreenHeight() - ch;

    DrawRectangle(0, cy, sw, ch, (Color){0, 0, 0, 200});
    DrawRectangleLines(0, cy, sw, ch, (Color){100, 100, 180, 200});

    // History
    int lineY = cy + ch - 40;
    for (int i = g_consoleHistoryCount - 1; i >= 0 && lineY > cy + 5; i--) {
        std::string hist(g_consoleHistory[i].begin(), g_consoleHistory[i].end());
        DrawText(hist.c_str(), 10, lineY - 18, 14, LIGHTGRAY);
        lineY -= 18;
    }

    // Input line
    DrawText("> ", 10, cy + ch - 22, 14, GREEN);
    DrawText(g_consoleBuf, 28, cy + ch - 22, 14, WHITE);
    // Cursor blink
    if ((int)(GetTime() * 4) % 2 == 0) {
        int cx = 28 + MeasureText(g_consoleBuf, 14);
        DrawText("_", cx, cy + ch - 22, 14, WHITE);
    }
}

static void HandleConsoleInput() {
    if (!g_consoleOpen) return;
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126 && g_consoleCursor < 255) {
            g_consoleBuf[g_consoleCursor++] = (char)key;
            g_consoleBuf[g_consoleCursor] = '\0';
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && g_consoleCursor > 0) {
        g_consoleBuf[--g_consoleCursor] = '\0';
    }
    if (IsKeyPressed(KEY_ENTER)) {
        ExecuteConsoleCommand(g_consoleBuf);
        g_consoleBuf[0] = '\0';
        g_consoleCursor = 0;
    }
    if (IsKeyPressed(KEY_UP) && g_consoleHistoryPos > 0) {
        g_consoleHistoryPos--;
        std::string hist(g_consoleHistory[g_consoleHistoryPos].begin(), g_consoleHistory[g_consoleHistoryPos].end());
        strncpy(g_consoleBuf, hist.c_str(), 255);
        g_consoleCursor = strlen(g_consoleBuf);
    }
    if (IsKeyPressed(KEY_DOWN) && g_consoleHistoryPos < g_consoleHistoryCount) {
        g_consoleHistoryPos++;
        if (g_consoleHistoryPos >= g_consoleHistoryCount) {
            g_consoleBuf[0] = '\0';
            g_consoleCursor = 0;
        } else {
            std::string hist(g_consoleHistory[g_consoleHistoryPos].begin(), g_consoleHistory[g_consoleHistoryPos].end());
            strncpy(g_consoleBuf, hist.c_str(), 255);
            g_consoleCursor = strlen(g_consoleBuf);
        }
    }
}

// ---- Pickup ground rendering ----
// Server-side PickupType values: HEALTH=0, MANA=1, PSYCHIC=2, ARMOR=3, WEAPON=4, AMMO=5, KEY=6, COIN=7, POWERUP=8
static Texture2D* IconForPickupType(int type) {
    switch (type) {
        case 0: return &OmegaTechGameObjects.HealthVialIcon;
        case 1: return &OmegaTechGameObjects.ManaVialIcon;
        case 2: return &OmegaTechGameObjects.EnergyCrystalIcon;
        case 6: return &OmegaTechGameObjects.KeyIcon;
        case 7: return &OmegaTechGameObjects.CoinIcon;
        case 8: return &OmegaTechGameObjects.PowerupIcon;
        default: return nullptr;
    }
}

static void DrawPickupBillboard(Texture2D* icon, Vector3 pos) {
    if (!icon || icon->id == 0) return;
    float bob = sinf((float)GetTime() * 3.0f) * 0.15f;
    Vector3 p = {pos.x, pos.y + 0.9f + bob, pos.z};
    float sz = 1.2f;
    DrawBillboardPro(OmegaTechData.MainCamera, *icon,
        (Rectangle){0, 0, (float)icon->width, (float)icon->height},
        p, (Vector3){0, 1, 0}, (Vector2){sz, sz}, (Vector2){0.5f, 0.5f}, 0, WHITE);
}

// Offline demo pickups near spawn so sprites can be verified without a server
struct LocalDemoPickup { Vector3 pos; int type; bool active; };
static LocalDemoPickup g_demoPickups[] = {
    {{  4.0f, 18.0f, -8.0f}, 0, true},  // health
    {{  6.0f, 18.0f, -8.0f}, 1, true},  // mana
    {{  8.0f, 18.0f, -8.0f}, 2, true},  // psychic
    {{ 10.0f, 18.0f, -8.0f}, 6, true},  // key
    {{ 12.0f, 18.0f, -8.0f}, 7, true},  // coin
    {{ 14.0f, 18.0f, -8.0f}, 8, true},  // powerup
};
static constexpr int g_demoPickupCount = 6;

static void UpdateDemoPickupCollect() {
    if (g_network_enabled && g_client.is_connected()) return;
    Vector3 cam = OmegaTechData.MainCamera.position;
    for (int i = 0; i < g_demoPickupCount; i++) {
        if (!g_demoPickups[i].active) continue;
        float dx = cam.x - g_demoPickups[i].pos.x;
        float dz = cam.z - g_demoPickups[i].pos.z;
        if (dx * dx + dz * dz > 2.25f) continue;
        g_demoPickups[i].active = false;
        int itemId = 1;
        switch (g_demoPickups[i].type) {
            case 0: itemId = 1; break;
            case 1: itemId = 2; break;
            case 2: itemId = 3; break;
            case 6: itemId = 12; break;
            case 7: itemId = 13; gInventory.coins += 1; break;
            case 8: itemId = 14; break;
            default: break;
        }
        if (g_demoPickups[i].type != 7)
            gInventory.AddToBackpack(itemId, 1);
        PlaySound(OmegaTechSoundData.UIClick);
        fprintf(stderr, "DEMO pickup type=%d -> item %d\n", g_demoPickups[i].type, itemId);
    }
}

static void DrawGroundPickups() {
    if (g_network_enabled && g_client.is_connected()) {
        const auto& pickups = g_client.pickups();
        for (const auto& p : pickups) {
            if (!p.active) continue;
            DrawPickupBillboard(IconForPickupType(p.type),
                (Vector3){p.position.x, p.position.y, p.position.z});
        }
        return;
    }
    for (int i = 0; i < g_demoPickupCount; i++) {
        if (!g_demoPickups[i].active) continue;
        DrawPickupBillboard(IconForPickupType(g_demoPickups[i].type), g_demoPickups[i].pos);
    }
}

// ---- Inventory Overlay (Diablo I style) ----
static int g_invSelectedBpSlot = -1;

static void DrawInventoryOverlay() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int panel_w = 720;
    const int panel_h = 520;
    const int px = (sw - panel_w) / 2;
    const int py = (sh - panel_h) / 2;

    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});
    DrawRectangle(px, py, panel_w, panel_h, (Color){25, 25, 35, 245});
    DrawRectangleLines(px, py, panel_w, panel_h, (Color){180, 180, 200, 255});

    DrawText("INVENTORY", px + 15, py + 10, 22, WHITE);
    DrawText(TextFormat("Coins: %d", gInventory.coins), px + panel_w - 150, py + 14, 16, GOLD);

    int ex = px + 20;
    int ey = py + 50;
    int slotH = 38;
    int slotW = 150;

    // === EQUIPMENT (left side) ===
    DrawText("EQUIPMENT", ex, ey - 18, 12, LIGHTGRAY);

    struct EquipDraw {
        const char* label;
        bool* owned;
        Texture2D* icon;
    };

    EquipDraw equipDraws[EQUIP_SLOT_COUNT] = {
        {"Helmet",     &OmegaTechGameObjects.HelmetOwned,     &OmegaTechGameObjects.HelmetIcon},
        {"Armor",      &OmegaTechGameObjects.Armory1Owned,    &OmegaTechGameObjects.Armory1Icon},
        {"Legs",       &OmegaTechGameObjects.LegsOwned,       &OmegaTechGameObjects.LegsIcon},
        {"Boots",      &OmegaTechGameObjects.BootsOwned,      &OmegaTechGameObjects.BootsIcon},
        {"Jewelry 1",  &OmegaTechGameObjects.Jewelry1Owned,   &OmegaTechGameObjects.Jewelry1Icon},
        {"Jewelry 2",  &OmegaTechGameObjects.Jewelry2Owned,   &OmegaTechGameObjects.Jewelry2Icon},
        {"Accessory 1",&OmegaTechGameObjects.Accessory1Owned, &OmegaTechGameObjects.Accessory1Icon},
        {"Accessory 2",&OmegaTechGameObjects.Accessory2Owned, &OmegaTechGameObjects.Accessory2Icon},
    };

    for (int i = 0; i < EQUIP_SLOT_COUNT; i++) {
        bool owned = *equipDraws[i].owned;
        Color c = owned ? WHITE : (Color){80, 80, 80, 255};
        Color bg = owned ? (Color){40, 50, 45, 255} : (Color){20, 25, 20, 255};

        DrawRectangle(ex, ey, slotW, slotH, bg);
        DrawRectangleLines(ex, ey, slotW, slotH, c);
        DrawText(equipDraws[i].label, ex + 6, ey + 12, 12, c);

        if (owned && equipDraws[i].icon->id > 0) {
            DrawTextureEx(*equipDraws[i].icon, (Vector2){(float)ex + slotW - 34, (float)ey + 2}, 0, 1.5f, WHITE);
        }
        ey += slotH + 4;
    }

    // === BACKPACK (right side) ===
    int bx = px + 190;
    int by = py + 50;
    int cellSize = 52;
    int cellGap = 4;

    DrawText("BACKPACK", bx, by - 18, 12, LIGHTGRAY);

    for (int row = 0; row < BACKPACK_ROWS; row++) {
        for (int col = 0; col < BACKPACK_COLS; col++) {
            int idx = row * BACKPACK_COLS + col;
            int cx = bx + col * (cellSize + cellGap);
            int cy = by + row * (cellSize + cellGap);

            int itemId = gInventory.backpack[idx].itemId;
            int qty = gInventory.backpack[idx].quantity;
            bool hasItem = itemId >= 0 && qty > 0;

            Color bgC = hasItem ? (Color){40, 40, 55, 255} : (Color){15, 15, 20, 255};
            Color borderC = hasItem ? WHITE : (Color){50, 50, 50, 255};

            if (g_invSelectedBpSlot == idx) {
                borderC = (Color){255, 255, 0, 255};
            }

            DrawRectangle(cx, cy, cellSize, cellSize, bgC);
            DrawRectangleLines(cx, cy, cellSize, cellSize, borderC);

            if (hasItem) {
                const ItemDBEntry* def = GetItemDef(itemId);
                if (def) {
                    Texture2D* icon = nullptr;
                    switch (def->category) {
                        case ItemCategory::HEALTH_VIAL:    icon = &OmegaTechGameObjects.HealthVialIcon; break;
                        case ItemCategory::MANA_VIAL:      icon = &OmegaTechGameObjects.ManaVialIcon; break;
                        case ItemCategory::ENERGY_CRYSTAL: icon = &OmegaTechGameObjects.EnergyCrystalIcon; break;
                        case ItemCategory::KEY:            icon = &OmegaTechGameObjects.KeyIcon; break;
                        case ItemCategory::COIN:           icon = &OmegaTechGameObjects.CoinIcon; break;
                        case ItemCategory::POWERUP:        icon = &OmegaTechGameObjects.PowerupIcon; break;
                        default: break;
                    }
                    if (icon && icon->id > 0) {
                        float scale = (float)cellSize / (float)icon->width * 0.7f;
                        DrawTextureEx(*icon, (Vector2){(float)cx + 6, (float)cy + 4}, 0, scale, WHITE);
                    }
                    // Quantity text
                    if (qty > 1) {
                        DrawText(TextFormat("%d", qty), cx + cellSize - 20, cy + cellSize - 16, 12, WHITE);
                    }
                }
            }
        }
    }

    // === STATS (between equipment and backpack) ===
    int sx = px + 530;
    int sy = py + 50;
    DrawText("STATS", sx, sy - 18, 12, LIGHTGRAY);

    sy += 4;
    DrawText(TextFormat("Level: %d", OmegaPlayer.Level), sx, sy, 14, WHITE); sy += 22;
    DrawText(TextFormat("XP: %d/%d", OmegaPlayer.XP, OmegaPlayer.XPToNext), sx, sy, 14, WHITE); sy += 22;

    DrawRectangle(sx, sy, 150, 10, (Color){50, 0, 0, 255});
    float hpPct = OmegaPlayer.MaxHealth > 0 ? OmegaPlayer.Health / OmegaPlayer.MaxHealth : 0;
    DrawRectangle(sx, sy, (int)(150 * hpPct), 10, RED);
    DrawText(TextFormat("HP: %.0f/%.0f", OmegaPlayer.Health, OmegaPlayer.MaxHealth), sx + 1, sy + 12, 12, RED); sy += 28;

    DrawRectangle(sx, sy, 150, 10, (Color){0, 0, 50, 255});
    float mpPct = OmegaPlayer.MaxMana > 0 ? OmegaPlayer.Mana / OmegaPlayer.MaxMana : 0;
    DrawRectangle(sx, sy, (int)(150 * mpPct), 10, BLUE);
    DrawText(TextFormat("MP: %.0f/%.0f", OmegaPlayer.Mana, OmegaPlayer.MaxMana), sx + 1, sy + 12, 12, BLUE); sy += 28;

    DrawRectangle(sx, sy, 150, 10, (Color){30, 0, 30, 255});
    float pePct = OmegaPlayer.MaxPsychicEnergy > 0 ? OmegaPlayer.PsychicEnergy / OmegaPlayer.MaxPsychicEnergy : 0;
    DrawRectangle(sx, sy, (int)(150 * pePct), 10, PURPLE);
    DrawText(TextFormat("PE: %.0f/%.0f", OmegaPlayer.PsychicEnergy, OmegaPlayer.MaxPsychicEnergy), sx + 1, sy + 12, 12, PURPLE);

    // Selected item info
    if (g_invSelectedBpSlot >= 0) {
        int itemId = gInventory.backpack[g_invSelectedBpSlot].itemId;
        if (itemId >= 0) {
            const ItemDBEntry* def = GetItemDef(itemId);
            if (def) {
                DrawText(def->name, sx, sy + 40, 14, WHITE);
                DrawText(def->description, sx, sy + 58, 12, LIGHTGRAY);
            }
        }
    }

    // Hotbar preview at bottom
    int hx = px + 20;
    int hy = py + panel_h - 48;
    DrawText("HOTBAR:", hx, hy, 12, DARKGRAY);
    hx += 60;
    for (int i = 0; i < 5; i++) {
        bool owned = false;
        Texture2D* icon = nullptr;
        switch (i) {
            case 0: owned = OmegaTechGameObjects.Object1Owned; icon = &OmegaTechGameObjects.Object1Icon; break;
            case 1: owned = OmegaTechGameObjects.Object2Owned; icon = &OmegaTechGameObjects.Object2Icon; break;
            case 2: owned = OmegaTechGameObjects.Object3Owned; icon = &OmegaTechGameObjects.Object3Icon; break;
            case 3: owned = OmegaTechGameObjects.Object4Owned; icon = &OmegaTechGameObjects.Object4Icon; break;
            case 4: owned = OmegaTechGameObjects.Object5Owned; icon = &OmegaTechGameObjects.Object5Icon; break;
        }
        Color c = owned ? WHITE : (Color){50, 50, 50, 255};
        DrawRectangle(hx, hy, 32, 32, (Color){20, 20, 30, 255});
        DrawRectangleLines(hx, hy, 32, 32, c);
        if (owned && icon && icon->id > 0)
            DrawTextureEx(*icon, (Vector2){(float)hx + 4, (float)hy + 4}, 0, 1.0f, WHITE);
        hx += 36;
    }

    // Controls hint
    DrawText("TAB: close  |  CLICK item to use  |  E: pickup  |  ^: console",
             px + 20, py + panel_h - 22, 12, DARKGRAY);
}

int main(){
    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(1280 , 720 , "Angels95");
    SetTargetFPS(60);

    InitAudioDevice();

    if (!IsAudioDeviceReady()){
        CloseAudioDevice();
    }

    OmegaTechInit();
    PlaySplashScreen();
    PlayHomeScreen();
    
    LoadWorld();

    // Connect to server (if Join Game was selected, use custom IP)
    g_client.set_on_chat_received([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });
    g_client.set_on_item_collected([](int item_id, int quantity) {
        if (item_id == 13) {
            gInventory.coins += quantity;
        } else {
            gInventory.AddToBackpack(item_id, quantity);
        }
    });

    if (SetServerJoinFlag && SetServerJoinIP) {
        g_network_enabled = g_client.connect(SetServerJoinIP, 27015);
        if (g_network_enabled) {
            OZ_INFO("Network: connected to %s:27015", SetServerJoinIP);
        }
        SetServerJoinFlag = false;
    }

    HideCursor(); 
    DisableCursor();  
    
    while (!WindowShouldClose())
    {
        // Console toggle with ^ (grave) key
        if (IsKeyPressed(KEY_GRAVE)) {
            g_consoleOpen = !g_consoleOpen;
            if (g_consoleOpen) {
                ShowCursor();
                EnableCursor();
            } else if (!ShowInventory) {
                HideCursor();
                DisableCursor();
            }
        }

        // Tab key toggles inventory
        if (IsKeyPressed(KEY_TAB)) {
            if (!g_consoleOpen) {
                ShowInventory = !ShowInventory;
                if (ShowInventory) {
                    ShowCursor();
                    EnableCursor();
                } else {
                    HideCursor();
                    DisableCursor();
                }
            }
        }

        // Console input processing
        if (g_consoleOpen) {
            HandleConsoleInput();
        }

        // Capture left-mouse state before camera (handle after)
        static bool left_click_was_down = false;
        bool left_click_now = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool left_just_pressed = left_click_now && !left_click_was_down;

        OmegaPlayer.OldX = OmegaTechData.MainCamera.position.x;
        OmegaPlayer.OldY = OmegaTechData.MainCamera.position.y;
        OmegaPlayer.OldZ = OmegaTechData.MainCamera.position.z;

        if (!ShowSettings && !ShowInventory && !g_consoleOpen){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

        // Weapon fire AFTER camera so left-click does not disrupt movement
        if (!ShowInventory && !g_consoleOpen && left_just_pressed) {
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

        UpdateDemoPickupCollect();

        // Draw ground pickups in a second 3D pass to the render target
        BeginTextureMode(Target);
        BeginMode3D(OmegaTechData.MainCamera);
        DrawGroundPickups();
        EndMode3D();
        EndTextureMode();
        
        BeginDrawing();  

        ClearBackground(BLACK);

        DrawTexturePro(Target.texture, (Rectangle){ 0, 0, Target.texture.width, -Target.texture.height }, (Rectangle){ 0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, (Vector2){ 0, 0 } , 0.f , WHITE);
        UpdateObjectBar();

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

        // Console overlay (always on top)
        DrawConsole();

        EndDrawing();

        if (IsKeyPressed(KEY_F11))ToggleFullscreen();

    }
    
    g_client.disconnect();
    CloseWindow();
}