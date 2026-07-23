#include "Core.hpp"
#include "Log.hpp"
#include "Client/Client.hpp"
#include "Script/LightningEntityManager.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Native Win32 menu bar — replaces the old raygui F2 menu
// ---------------------------------------------------------------------------


/*
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    TODO: 
        REFACTOR THE Demo Pickup / Projectile system to be more efficient and less hacky and use real Engine systems instead of this hacky mess.
        This has to be linked entirely to the new LightningEntityManager system and use the new entity definitions for pickups and projectiles instead of hardcoded types aswell as correct PawmRegistry from ozone files
        .
        aswell usage of the new entity system for the local projectiles and pickups instead of this hacky mess.
        Real Pawn system should be used for pickups and projectiles instead of this hacky mess.
        ZoneInfos arent even integrated at all, same goes for Models, Textures, Sounds, and other assets. This is a very hacky mess and needs to be
        refactored to use the new systems instead of this hacky mess.
        Guns / projectiles/ Rendering / Everything is Hardcoded inside this file and needs to be refactored to use the new systems instead of this hacky mess.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

*/
#define IDM_FILE_LOAD   1001
#define IDM_FILE_SAVE   1002
#define IDM_FILE_QUIT   1003
#define IDM_SETTINGS    2001
#define IDM_ABOUT       3001

static void PumpWin32Messages() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_COMMAND) {
            switch (LOWORD(msg.wParam)) {
                case IDM_FILE_LOAD:
                    // Reload current world
                    SetSceneId = OmegaTechData.LevelIndex;
                    SetSceneFlag = true;
                    break;
                case IDM_FILE_SAVE:
                    SaveGame();
                    break;
                case IDM_FILE_QUIT:
                    CloseWindow();
                    break;
                case IDM_SETTINGS:
                    ShowSettings = !ShowSettings;
                    if (ShowSettings) {
                        ShowCursor();
                        EnableCursor();
                    } else {
                        HideCursor();
                        DisableCursor();
                    }
                    break;
                case IDM_ABOUT:
                    MessageBoxA(NULL,
                        "Angels95 v1.0\nOzWorld GameEngine Engine\nBased on OmegaTech\nTribeWarez 2026",
                        "About Angels95", MB_OK | MB_ICONINFORMATION);
                    break;
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

static void CreateNativeMenuBar() {
    HWND hWnd = (HWND)GetWindowHandle();
    if (!hWnd) return;
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuA(hFileMenu, MF_STRING, IDM_FILE_LOAD, "&Load World...");
    AppendMenuA(hFileMenu, MF_STRING, IDM_FILE_SAVE, "&Save Game");
    AppendMenuA(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFileMenu, MF_STRING, IDM_FILE_QUIT, "&Quit");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, "&File");
    HMENU hSettingsMenu = CreatePopupMenu();
    AppendMenuA(hSettingsMenu, MF_STRING, IDM_SETTINGS, "&Developer Settings...");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hSettingsMenu, "&Settings");
    HMENU hAboutMenu = CreatePopupMenu();
    AppendMenuA(hAboutMenu, MF_STRING, IDM_ABOUT, "&About Angels95...");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hAboutMenu, "&?");
    SetMenu(hWnd, hMenuBar);
}

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
// Fire weapon helper — delegates to LightningEntityManager
// ---------------------------------------------------------------------------
static void FireWeapon() {
    Camera3D& cam = OmegaTechData.MainCamera;
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 origin = Vector3Add(cam.position, Vector3Scale(forward, 2.0f));

    // Fire via selected weapon entity if available
    EntityInstance* ent = LightningEntityManager::Instance().SelectedEntity();
    if (ent && ent->def && ent->def->type == EntityType::WEAPON) {
        // Execute weapon's LightningScript on_fire action (future)
        // For now: entity system will handle projectile spawning via script
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
bool g_showCollisionDebug = false;
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
    } else if (strncmp(cmd, "/world ", 7) == 0) {
        int id = atoi(cmd + 7);
        if (id >= 1 && id <= 20) {
            OZ_INFO("World switch: %d -> %d", OmegaTechData.LevelIndex, id);
            SetSceneId = id;
            SetSceneFlag = true;
            // World3 uses a ClipBox floor — spawn well above the platform surface
            float spawnY = (id == 3) ? 8.0f : 20.0f;
            OmegaTechData.MainCamera.position = (Vector3){0.0f, spawnY, 0.0f};
            OmegaTechData.MainCamera.target = (Vector3){0.0f, spawnY, -10.0f};
        }
    } else if (strcmp(cmd, "/world") == 0) {
        fprintf(stderr, "WORLD current=%d (use /world N)\n", OmegaTechData.LevelIndex);
    } else if (strcmp(cmd, "/fly") == 0) {
        OmegaPlayer.isFlying = !OmegaPlayer.isFlying;
        if (OmegaPlayer.isFlying) {
            OmegaPlayer.onGround = false;
            OmegaPlayer.velocityY = 0.0f;
            OZ_INFO("FLY enabled");
        } else {
            OZ_INFO("FLY disabled");
        }
    } else if (strcmp(cmd, "/noclip") == 0) {
        OmegaPlayer.isNoClip = !OmegaPlayer.isNoClip;
        if (OmegaPlayer.isNoClip) {
            OmegaPlayer.onGround = false;
            OmegaPlayer.velocityY = 0.0f;
            OZ_INFO("NOCLIP enabled");
        } else {
            OZ_INFO("NOCLIP disabled");
        }
    } else if (strcmp(cmd, "/showcollisions") == 0) {
        g_showCollisionDebug = !g_showCollisionDebug;
        OZ_INFO("Collision debug %s", g_showCollisionDebug ? "ON" : "OFF");
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

int main(int argc, char** argv){
    // CLI args
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--world") == 0) {
            g_world_to_load = argv[i + 1];
            i++;
        }
    }

    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(1280 , 720 , "Angels95");
    SetTargetFPS(60);

    InitAudioDevice();

    if (!IsAudioDeviceReady()){
        CloseAudioDevice();
    }

    OmegaTechInit();
    CreateNativeMenuBar();
    PlaySplashScreen();

    static bool g_returnToMenu = false;

    // Outer loop: return to menu after gameplay
    while (!WindowShouldClose()) {
    PlayHomeScreen();
    if (WindowShouldClose()) break;
    g_returnToMenu = false;
    
    LoadWorld();
    g_client.set_on_chat_received([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });
    g_client.set_on_item_collected([](int item_id, int quantity) {
        OZ_INFO("Item collected: id=%d qty=%d (slot=%s)", item_id, quantity,
                item_id == 1 ? "Object1" : item_id == 2 ? "Object2" :
                item_id == 3 ? "Object3" : item_id == 4 ? "Object4" :
                item_id == 5 ? "Object5" : "other");
        if (item_id == 13) {
            gInventory.coins += quantity;
        } else if (item_id == 1) {
            // Health vial — auto-heal locally (server already applied too)
            OmegaPlayer.Health = std::min(OmegaPlayer.Health + 25.0f, OmegaPlayer.MaxHealth);
        } else if (item_id == 2) {
            // Mana vial — auto-restore locally
            OmegaPlayer.Mana = std::min(OmegaPlayer.Mana + 25.0f, OmegaPlayer.MaxMana);
        } else if (item_id > 0) {
            gInventory.AddToBackpack(item_id, quantity);
        }
        if (OmegaTechSoundData.UIClick.frameCount > 0)
            PlaySound(OmegaTechSoundData.UIClick);
    });

    g_client.set_on_player_hurt([](int damage, float remaining_health) {
        OmegaPlayer.Health = remaining_health;
        if (OmegaTechSoundData.Death.frameCount > 0 &&
            !IsSoundPlaying(OmegaTechSoundData.Death))
            PlaySound(OmegaTechSoundData.Death);
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
    
    while (!WindowShouldClose() && !g_returnToMenu)
    {
        PumpWin32Messages();

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

        // Pause menu state
        static bool g_gamePaused = false;
        {
            static bool pauseKeyWasDown = false;
            bool pauseKeyNow = IsKeyPressed(KEY_ESCAPE);
            if (pauseKeyNow && !pauseKeyWasDown) {
                g_gamePaused = !g_gamePaused;
                if (g_gamePaused) { ShowCursor(); EnableCursor(); }
                else if (!ShowSettings && !ShowInventory && !g_consoleOpen) { HideCursor(); DisableCursor(); }
            }
            pauseKeyWasDown = pauseKeyNow;
        }

        // Skip game input/update when paused
        if (g_gamePaused) {
            // Draw pause menu overlay
            int sw = GetScreenWidth(), sh = GetScreenHeight();
            DrawRectangle(0, 0, sw, sh, (Color){0,0,0,180});

            Texture2D heading = OmegaTechData.PauseHeading;
            if (heading.id > 0) {
                float hx = (sw - heading.width) / 2.0f;
                DrawTexture(heading, (int)hx, sh/2 - heading.height - 80, WHITE);
            }

            const char* labels[] = {"Resume", "Settings", "Main Menu", "Quit"};
            int btnCount = 4;
            int btnW = 220, btnH = 50, gap = 10;
            int totalH = btnCount * btnH + (btnCount - 1) * gap;
            int startY = sh/2 - totalH/2;

            for (int i = 0; i < btnCount; i++) {
                int bx = (sw - btnW) / 2;
                int by = startY + i * (btnH + gap);
                Rectangle r = {(float)bx, (float)by, (float)btnW, (float)btnH};
                bool hover = CheckCollisionPointRec(GetMousePosition(), r);
                bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

                Texture2D tex = clicked ? OmegaTechData.BtnClicked :
                                hover ? OmegaTechData.BtnHover : OmegaTechData.BtnNormal;
                // Draw button background
                if (tex.id > 0) {
                    DrawTexturePro(tex,
                        (Rectangle){0,0,(float)tex.width,(float)tex.height},
                        r, (Vector2){0,0}, 0, WHITE);
                } else {
                    DrawRectangleRec(r, (Color){50,50,70,220});
                    DrawRectangleLinesEx(r, 2, (Color){100,100,140,255});
                }
                DrawText(labels[i], bx + (btnW - MeasureText(labels[i], 18)) / 2,
                         by + (btnH - 18) / 2, 18, WHITE);

                    if (clicked) {
                    if (i == 0) { g_gamePaused = false; HideCursor(); DisableCursor(); }
                    else if (i == 1) { ToggleSettings(); g_gamePaused = false; }
                    else if (i == 2) { g_gamePaused = false; g_returnToMenu = true; }
                    else if (i == 3) { CloseWindow(); return 0; }
                    }
            }

            EndDrawing();
            continue;
        }

        // Capture left-mouse state before camera (handle after)
        static bool left_click_was_down = false;
        bool left_click_now = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool left_just_pressed = left_click_now && !left_click_was_down;

        OmegaPlayer.OldX = OmegaTechData.MainCamera.position.x;
        OmegaPlayer.OldY = OmegaTechData.MainCamera.position.y;
        OmegaPlayer.OldZ = OmegaTechData.MainCamera.position.z;

        // Save Y so we can override raylib's built-in Space/Shift vertical movement
        float savedCamY = OmegaTechData.MainCamera.position.y;

        if (!ShowSettings && !ShowInventory && !g_consoleOpen){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

        // --- Zone volume detection + movement effects ---
        {
            float dt = GetFrameTime();
            Vector3 playerPos = OmegaTechData.MainCamera.position;
            static std::string lastZoneName;

            // Query active zone at current position
            ZoneVolumeNode* activeZone = PawnSystem::Instance().CheckZoneCollision(playerPos, OmegaPlayer.PlayerBounds);
            OmegaPlayer.inWater = false;

            if (activeZone) {
                ZoneType zt = activeZone->zoneType;
                // Skip zones already handled elsewhere
                if (zt != ZoneType::ZONE_SKY && zt != ZoneType::ZONE_GAMEPLAY_SOUND) {
                    // Fire zone enter event using zone name (triggers .ozls script hooks)
                    if (activeZone->name != lastZoneName) {
                        LightningEntityManager::Instance().TriggerZoneAction(activeZone->name, "on_enter");
                        if (!lastZoneName.empty())
                            LightningEntityManager::Instance().TriggerZoneAction(lastZoneName, "on_exit");
                        lastZoneName = activeZone->name;
                    }
                }

                switch (zt) {
                    case ZoneType::ZONE_WATER:
                        OmegaPlayer.inWater = true;
                        break;
                    case ZoneType::ZONE_LADDER:
                        // Ladder: disable gravity, allow vertical movement with W/S
                        OmegaPlayer.velocityY = 0.0f;
                        OmegaPlayer.onGround = false;
                        if (IsKeyDown(KEY_W))
                            OmegaTechData.MainCamera.position.y += 6.0f * dt;
                        if (IsKeyDown(KEY_S))
                            OmegaTechData.MainCamera.position.y -= 6.0f * dt;
                        break;
                    case ZoneType::ZONE_REVERB:
                        // Placeholder: reverb DSP will be applied via audio system
                        break;
                    default:
                        break;
                }
            } else if (!lastZoneName.empty()) {
                LightningEntityManager::Instance().TriggerZoneAction(lastZoneName, "on_exit");
                lastZoneName.clear();
            }
        }

        // ---  Jump / Fly / Noclip Y management ---
        {
            float dt = GetFrameTime();

            if (OmegaPlayer.isNoClip) {
                // Noclip: let raylib control Y natively (space up / shift down)
            } else if (OmegaPlayer.isFlying) {
                // Flying: let raylib control Y, no terrain snap
            } else if (OmegaPlayer.inWater) {
                // Water: restore Y, reduced gravity, dampen fall
                OmegaTechData.MainCamera.position.y = savedCamY;

                if (IsKeyPressed(KEY_SPACE) && !g_consoleOpen && !ShowInventory) {
                    OmegaPlayer.velocityY = 5.0f; // swim upward
                }

                if (!OmegaPlayer.onGround) {
                    OmegaPlayer.velocityY += -8.0f * dt; // reduced gravity
                    OmegaPlayer.velocityY *= 0.95f;      // water drag
                    OmegaTechData.MainCamera.position.y += OmegaPlayer.velocityY * dt;
                }
            } else {
                // Normal / grounded: restore Y
                OmegaTechData.MainCamera.position.y = savedCamY;

                if (IsKeyPressed(KEY_SPACE) && OmegaPlayer.onGround && !g_consoleOpen && !ShowInventory) {
                    OmegaPlayer.velocityY = 8.0f;
                    OmegaPlayer.onGround = false;
                }

                // Gravity
                if (!OmegaPlayer.onGround) {
                    OmegaPlayer.velocityY += -20.0f * dt;
                    OmegaTechData.MainCamera.position.y += OmegaPlayer.velocityY * dt;
                }
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

        BeginDrawing();  

        ClearBackground(BLACK);

        // Apply post-processing shaders during final blit
        {
            bool shaderActive = false;
            if (PixelShader && OmegaTechData.PixelShader.id > 0) {
                BeginShaderMode(OmegaTechData.PixelShader);
                SetShaderValue(OmegaTechData.PixelShader, GetShaderLocation(OmegaTechData.PixelShader, "pixelWidth"), &PixelSize, SHADER_UNIFORM_FLOAT);
                SetShaderValue(OmegaTechData.PixelShader, GetShaderLocation(OmegaTechData.PixelShader, "pixelHeight"), &PixelSize, SHADER_UNIFORM_FLOAT);
                shaderActive = true;
            }
            if (JitterEnabled && OmegaTechData.JitterShader.id > 0) {
                if (!shaderActive) BeginShaderMode(OmegaTechData.JitterShader);
                float t = float(GetTime());
                SetShaderValue(OmegaTechData.JitterShader, GetShaderLocation(OmegaTechData.JitterShader, "uTime"), &t, SHADER_UNIFORM_FLOAT);
                SetShaderValue(OmegaTechData.JitterShader, GetShaderLocation(OmegaTechData.JitterShader, "uIntensity"), &JitterIntensity, SHADER_UNIFORM_FLOAT);
                shaderActive = true;
            }
            if (FogEnabled && OmegaTechData.FogShader.id > 0) {
                if (!shaderActive) BeginShaderMode(OmegaTechData.FogShader);
                float fc[4] = { FogTint.r / 255.0f, FogTint.g / 255.0f, FogTint.b / 255.0f, FogTint.a / 255.0f };
                SetShaderValue(OmegaTechData.FogShader, GetShaderLocation(OmegaTechData.FogShader, "fogColor"), fc, SHADER_UNIFORM_VEC4);
                SetShaderValue(OmegaTechData.FogShader, GetShaderLocation(OmegaTechData.FogShader, "fogIntensity"), &FogIntensity, SHADER_UNIFORM_FLOAT);
                shaderActive = true;
            }
            DrawTexturePro(Target.texture, (Rectangle){ 0, 0, Target.texture.width, -Target.texture.height }, (Rectangle){ 0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, (Vector2){ 0, 0 } , 0.f , WHITE);
            if (shaderActive) EndShaderMode();
        }
        // LightningScript dynamic hotbar
        LightningEntityManager::Instance().DrawHotbar();
        LightningEntityManager::Instance().HandleInput();

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
                for (size_t i = 0; i < npcs.size(); i++) {
                    Pawn* p = PawnSystem::Instance().Get(static_cast<int>(i + 1));
                    if (p) {
                        p->position = { npcs[i].position.x, npcs[i].position.y, npcs[i].position.z };
                        p->active = npcs[i].active;
                    } else if (npcs[i].active) {
                        PawnSystem::Instance().Spawn({ npcs[i].position.x, npcs[i].position.y, npcs[i].position.z }, "Walker");
                    }
                }

                // Auto-collect when walking over a pickup (also E); throttle requests
                {
                    static double last_collect_try = 0.0;
                    double t = GetTime();
                    bool want = IsKeyPressed(KEY_E) || (t - last_collect_try > 0.35);
                    if (want) {
                        const auto& pickups = g_client.pickups();
                        Vector3 cp = OmegaTechData.MainCamera.position;
                        float nearest_dist = IsKeyPressed(KEY_E) ? 5.0f : 2.0f;
                        int nearest_pickup = -1;
                        int nearest_world = 0;
                        for (const auto& p : pickups) {
                            if (!p.active) continue;
                            float dx = p.position.x - cp.x;
                            float dy = p.position.y - cp.y;
                            float dz = p.position.z - cp.z;
                            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                            if (dist < nearest_dist) {
                                nearest_dist = dist;
                                nearest_pickup = p.id;
                                nearest_world = p.world_index;
                            }
                        }
                        if (nearest_pickup >= 0) {
                            g_client.send_pickup_collect(nearest_pickup, nearest_world);
                            last_collect_try = t;
                        }
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

        // Inventory overlay
        if (ShowInventory) {
            DrawInventoryOverlay();
        }

        // Console overlay (always on top)
        DrawConsole();

        EndDrawing();

        if (IsKeyPressed(KEY_F11))ToggleFullscreen();

    } // end inner game loop

    // Cleanup before returning to menu
    g_client.disconnect();
    if (OmegaTechSoundData.MusicFound) {
        StopMusicStream(OmegaTechSoundData.BackgroundMusic);
        UnloadMusicStream(OmegaTechSoundData.BackgroundMusic);
        OmegaTechSoundData.MusicFound = false;
    }

    } // end outer menu/game loop
    
    UnloadRenderTexture(Target);
    EngineBillboard::Shutdown();
    CloseWindow();
}