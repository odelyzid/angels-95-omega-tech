#include "Core.hpp"
#include "Log.hpp"
#include "Client/Client.hpp"
#include "Script/LightningEntityManager.hpp"
#include "Script/LightningEntityRegistry.hpp"
#include "Script/LightningEntityDef.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Native Win32 menu bar — replaces the old raygui F2 menu
// Window subclass intercepts WM_COMMAND from menus before raylib's WndProc
// ---------------------------------------------------------------------------

#define IDM_FILE_LOAD   1001
#define IDM_FILE_SAVE   1002
#define IDM_FILE_QUIT   1003
#define IDM_SETTINGS    2001
#define IDM_ABOUT       3001

static WNDPROC g_originalWndProc = nullptr;

static LRESULT CALLBACK ClientWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
            case IDM_FILE_LOAD:
                SetSceneId = OmegaTechData.LevelIndex;
                SetSceneFlag = true;
                return 0;
            case IDM_FILE_SAVE:
                SaveGame();
                return 0;
            case IDM_FILE_QUIT:
                CloseWindow();
                return 0;
            case IDM_SETTINGS:
                ShowSettings = !ShowSettings;
                if (ShowSettings) { ShowCursor(); EnableCursor(); }
                else { HideCursor(); DisableCursor(); }
                return 0;
            case IDM_ABOUT:
                MessageBoxA(NULL,
                    "Angels95 v1.0\nOzWorld GameEngine Engine\nBased on OmegaTech\nTribeWarez 2026",
                    "About Angels95", MB_OK | MB_ICONINFORMATION);
                return 0;
        }
    }
    return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
}

static void CreateNativeMenuBar() {
    HWND hWnd = (HWND)GetWindowHandle();
    if (!hWnd) return;

    // Subclass the raylib window so we intercept WM_COMMAND from menus
    g_originalWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ClientWndProc);

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
#endif // _WIN32

static OmegaClient g_client;
static bool g_network_enabled = false;
static bool ShowInventory = false;


// ---------------------------------------------------------------------------
// HUD: draw player stats bars (always visible) 
// TODO: move into PlayerUiHandler
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
    DrawText(TextFormat("Lv.%d", LightningEntityManager::Instance().GetPlayerLevel()), x, y, 14, WHITE);
    y += 18;

    // XP bar
    int xp_w = bar_w - 40;
    DrawText("XP", x, y, 12, LIGHTGRAY);
    int xpToNext = LightningEntityManager::Instance().GetPlayerXPToNext();
    if (xpToNext > 0) {
        int xp = LightningEntityManager::Instance().GetPlayerXP();
        float xp_pct = (float)xp / xpToNext;
        DrawRectangle(x + 30, y, xp_w, bar_h, (Color){30, 30, 30, 255});
        DrawRectangle(x + 30, y, (int)(xp_pct * xp_w), bar_h, SKYBLUE);
        DrawText(TextFormat("%d/%d", xp, xpToNext),
                 x + 34, y + 1, 10, WHITE);
    }
    y += bar_h + pad;

    // Health bar
    float hpMax = LightningEntityManager::Instance().GetPlayerMaxHealth();
    float hp = LightningEntityManager::Instance().GetPlayerHealth();
    float hp_pct = (hpMax > 0) ? (hp / hpMax) : 0;
    DrawText(TextFormat("HP %d/%d", (int)hp, (int)hpMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){50, 10, 10, 255});
    DrawRectangle(x, y + 14, (int)(hp_pct * bar_w), bar_h, RED);
    y += 14 + bar_h + pad;

    // Mana bar
    float mpMax = LightningEntityManager::Instance().GetPlayerMaxMana();
    float mp = LightningEntityManager::Instance().GetPlayerMana();
    float mp_pct = (mpMax > 0) ? (mp / mpMax) : 0;
    DrawText(TextFormat("MP %d/%d", (int)mp, (int)mpMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){10, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(mp_pct * bar_w), bar_h, BLUE);
    y += 14 + bar_h + pad;

    // Psychic Energy bar
    float peMax = LightningEntityManager::Instance().GetPlayerMaxPsychicEnergy();
    float pe = LightningEntityManager::Instance().GetPlayerPsychicEnergy();
    float pe_pct = (peMax > 0) ? (pe / peMax) : 0;
    DrawText(TextFormat("PE %d/%d", (int)pe, (int)peMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){40, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(pe_pct * bar_w), bar_h, PURPLE);
    y += 14 + bar_h + pad;

    // Current selected item/weapon (from EntityManager hotbar)
    {
        auto& lem = LightningEntityManager::Instance();
        EntityInstance* selEnt = lem.SelectedEntity();
        int selSlot = lem.SelectedSlot() + 1;
        const char* label = "Empty";
        if (selEnt && selEnt->def)
            label = selEnt->def->name.c_str();
        DrawText(TextFormat("Slot %d: %s", selSlot, label),
                 x, y, 12, YELLOW);
        y += 16;
    }

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
// TODO: Move into Render/
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
// TODO: Move away from Main into WeaponHandler
// ---------------------------------------------------------------------------
static void FireWeapon() {
    Camera3D& cam = OmegaTechData.MainCamera;
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 origin = Vector3Add(cam.position, Vector3Scale(forward, 2.0f));

    // Fire via entity system
    LightningEntityManager::Instance().FireSelectedWeapon(origin, forward);

    // Send to server
    if (g_network_enabled && g_client.is_connected()) {
        g_client.send_weapon_fire(
            origin.x, origin.y, origin.z,
            forward.x, forward.y, forward.z,
            1, 10);

        // Client-side NPC hit detection — find nearest NPC along fire ray
        int hitIdx = -1, hitPart = -1;
        float hitDist = 1e9f;
        const auto& cnpc = g_client.npcs();
        for (size_t i = 0; i < cnpc.size(); i++) {
            if (!cnpc[i].active) continue;
            Vector3 np = {cnpc[i].position.x, cnpc[i].position.y, cnpc[i].position.z};
            Vector3 toNpc = Vector3Subtract(np, origin);
            float t = Vector3DotProduct(toNpc, forward);
            if (t < 0) continue;
            Vector3 closest = Vector3Add(origin, Vector3Scale(forward, t));
            float d = Vector3Distance(closest, np);
            if (d < 2.0f && t < hitDist) {
                hitDist = t;
                hitIdx = static_cast<int>(i);
                hitPart = cnpc[i].partition_index;
            }
        }
        if (hitIdx >= 0) {
            g_client.send_npc_damage(0, hitIdx, hitPart, 10);
        }
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

        // Try matching weapon/entity name (LightningEntityRegistry)
        {
            auto& regAll = LightningEntityRegistry::Instance().GetAll();
            bool found = false;
            for (auto& [regName, def] : regAll) {
                const char* a = arg;
                const char* b = regName.c_str();
                bool match = true;
                while (*a && *b) {
                    char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
                    char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
                    if (ca != cb) { match = false; break; }
                    a++; b++;
                }
                if (match && *a == *b) {
                    int instIdx = LightningEntityManager::Instance().Spawn(def.name);
                    if (instIdx >= 0) {
                        for (int s = 0; s < LightningEntityManager::HOTBAR_SIZE; s++) {
                            if (LightningEntityManager::Instance().HotbarAt(s) < 0) {
                                LightningEntityManager::Instance().HotbarAssign(s, instIdx);
                                break;
                            }
                        }
                    }
                    found = true;
                    break;
                }
            }
            if (!found)
                OZ_INFO("No match for /summon '%s'", arg);
        }
    } else if (strncmp(cmd, "/world ", 7) == 0) {
        const char* name = cmd + 7;
        while (*name == ' ') name++;
        if (*name) {
            // Check if the world exists
            char worldPath[512];
            snprintf(worldPath, sizeof(worldPath), "GameData/Worlds/%s/World.ozone", name);
            char wdlPath[512];
            snprintf(wdlPath, sizeof(wdlPath), "GameData/Worlds/%s/World.wdl", name);
            if (IsPathFile(worldPath) || IsPathFile(wdlPath)) {
                strncpy(g_world_to_load, name, sizeof(g_world_to_load) - 1);
                g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
                OZ_INFO("World switch: %s -> %s", g_world_to_load, name);
                SetSceneFlag = true;
                OmegaTechData.MainCamera.position = (Vector3){0.0f, 20.0f, 0.0f};
                OmegaTechData.MainCamera.target = (Vector3){0.0f, 20.0f, -10.0f};
            } else {
                OZ_WARN("World '%s' not found in GameData/Worlds/", name);
            }
        }
    } else if (strcmp(cmd, "/worlds") == 0) {
        // List available worlds
        OZ_INFO("Available worlds in GameData/Worlds/:");
        if (fs::exists("GameData/Worlds")) {
            for (auto& entry : fs::directory_iterator("GameData/Worlds")) {
                if (entry.is_directory()) {
                    std::string dirName = entry.path().filename().string();
                    std::string wdl = entry.path().string() + "/World.wdl";
                    std::string oz  = entry.path().string() + "/World.ozone";
                    if (IsPathFile(wdl.c_str()) || IsPathFile(oz.c_str()))
                        OZ_INFO("  %s", dirName.c_str());
                }
            }
        }
    } else if (strcmp(cmd, "/world") == 0) {
        fprintf(stderr, "WORLD: current=%s (use /world <name> or /worlds to list)\n", g_world_to_load);
    } else if (strcmp(cmd, "/fly") == 0) {
        g_playerMovement.isFlying = !g_playerMovement.isFlying;
        if (g_playerMovement.isFlying) {
            g_playerMovement.onGround = false;
            g_playerMovement.velocityY = 0.0f;
            OZ_INFO("FLY enabled");
        } else {
            OZ_INFO("FLY disabled");
        }
    } else if (strcmp(cmd, "/noclip") == 0) {
        g_playerMovement.isNoClip = !g_playerMovement.isNoClip;
        if (g_playerMovement.isNoClip) {
            g_playerMovement.onGround = false;
            g_playerMovement.velocityY = 0.0f;
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

// ---- Inventory Overlay (Diablo I style TODO: -> Should be derived into PlayerUiHandler) ----
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

    const char* equipLabels[EQUIP_SLOT_COUNT] = {
        "Helmet", "Armor", "Legs", "Boots",
        "Jewelry 1", "Jewelry 2", "Accessory 1", "Accessory 2"
    };

    auto& lem = LightningEntityManager::Instance();
    for (int i = 0; i < EQUIP_SLOT_COUNT; i++) {
        int idx = lem.EquipmentAt(i);
        bool owned = (idx >= 0 && lem.Get(idx) && lem.Get(idx)->owned);
        Color c = owned ? WHITE : (Color){80, 80, 80, 255};
        Color bg = owned ? (Color){40, 50, 45, 255} : (Color){20, 25, 20, 255};

        DrawRectangle(ex, ey, slotW, slotH, bg);
        DrawRectangleLines(ex, ey, slotW, slotH, c);
        DrawText(equipLabels[i], ex + 6, ey + 12, 12, c);

        if (owned && lem.Get(idx)->iconIdx >= 0) {
            Texture2D* iconTex = (Texture2D*)lem.GetIcon(lem.Get(idx)->iconIdx);
            if (iconTex && iconTex->id > 0)
                DrawTextureEx(*iconTex, (Vector2){(float)ex + slotW - 34, (float)ey + 2}, 0, 1.5f, WHITE);
        }
        ey += slotH + 4;
    }

    // === BACKPACK (right side) -> Legacy ===
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
                    const char* iconAlias = nullptr;
                    switch (def->category) {
                        case ItemCategory::HEALTH_VIAL:    iconAlias = "HealthVial"; break;
                        case ItemCategory::MANA_VIAL:      iconAlias = "ManaVial"; break;
                        case ItemCategory::ENERGY_CRYSTAL: iconAlias = "EnergyCrystal"; break;
                        case ItemCategory::KEY:            iconAlias = "Key"; break;
                        case ItemCategory::COIN:           iconAlias = "Coin"; break;
                        case ItemCategory::POWERUP:        iconAlias = "Powerup"; break;
                        default: break;
                    }
                    if (iconAlias) {
                        Texture2D t = AssetMapper::Instance().GetTexture(iconAlias);
                        if (t.id > 0) {
                            static Texture2D s_cachedIcon = t;
                            s_cachedIcon = t;
                            icon = &s_cachedIcon;
                        }
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
    DrawText(TextFormat("Level: %d", LightningEntityManager::Instance().GetPlayerLevel()), sx, sy, 14, WHITE); sy += 22;
    DrawText(TextFormat("XP: %d/%d", LightningEntityManager::Instance().GetPlayerXP(), LightningEntityManager::Instance().GetPlayerXPToNext()), sx, sy, 14, WHITE); sy += 22;

    float hpInv = LightningEntityManager::Instance().GetPlayerHealth();
    float hpMaxInv = LightningEntityManager::Instance().GetPlayerMaxHealth();
    DrawRectangle(sx, sy, 150, 10, (Color){50, 0, 0, 255});
    float hpPct = hpMaxInv > 0 ? hpInv / hpMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * hpPct), 10, RED);
    DrawText(TextFormat("HP: %.0f/%.0f", hpInv, hpMaxInv), sx + 1, sy + 12, 12, RED); sy += 28;

    float mpInv = LightningEntityManager::Instance().GetPlayerMana();
    float mpMaxInv = LightningEntityManager::Instance().GetPlayerMaxMana();
    DrawRectangle(sx, sy, 150, 10, (Color){0, 0, 50, 255});
    float mpPct = mpMaxInv > 0 ? mpInv / mpMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * mpPct), 10, BLUE);
    DrawText(TextFormat("MP: %.0f/%.0f", mpInv, mpMaxInv), sx + 1, sy + 12, 12, BLUE); sy += 28;

    float peInv = LightningEntityManager::Instance().GetPlayerPsychicEnergy();
    float peMaxInv = LightningEntityManager::Instance().GetPlayerMaxPsychicEnergy();
    DrawRectangle(sx, sy, 150, 10, (Color){30, 0, 30, 255});
    float pePct = peMaxInv > 0 ? peInv / peMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * pePct), 10, PURPLE);
    DrawText(TextFormat("PE: %.0f/%.0f", peInv, peMaxInv), sx + 1, sy + 12, 12, PURPLE);

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

    // Hotbar preview at bottom (from EntityManager)
    int hx = px + 20;
    int hy = py + panel_h - 48;
    DrawText("HOTBAR:", hx, hy, 12, DARKGRAY);
    hx += 60;
    for (int i = 0; i < LightningEntityManager::HOTBAR_SIZE && i < 8; i++) {
        int idx = lem.HotbarAt(i);
        bool hasItem = (idx >= 0 && lem.Get(idx) && lem.Get(idx)->def);
        Color c = hasItem ? WHITE : (Color){50, 50, 50, 255};
        DrawRectangle(hx, hy, 32, 32, (Color){20, 20, 30, 255});
        DrawRectangleLines(hx, hy, 32, 32, c);
        if (hasItem && lem.Get(idx)->iconIdx >= 0) {
            Texture2D* iconTex = (Texture2D*)lem.GetIcon(lem.Get(idx)->iconIdx);
            if (iconTex && iconTex->id > 0)
                DrawTextureEx(*iconTex, (Vector2){(float)hx + 4, (float)hy + 4}, 0, 1.0f, WHITE);
        }
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
            strncpy(g_world_to_load, argv[i + 1], sizeof(g_world_to_load) - 1);
            g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
            i++;
        }
    }

    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(1280 , 720 , "Angels95");
    SetExitKey(0);
    SetTargetFPS(60);

    InitAudioDevice();

    if (!IsAudioDeviceReady()){
        CloseAudioDevice();
    }

    OmegaTechInit();
#ifdef _WIN32
    CreateNativeMenuBar();
#endif
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
        const char* name = "unknown";
        const ItemDBEntry* def = GetItemDef(item_id);
        if (def) name = def->name;
        OmegaTechTextSystem.Write(TextFormat("Collected: %s x%d", name, quantity));
        if (item_id == 13) {
            gInventory.coins += quantity;
        } else if (item_id == 1) {
            float curHp = LightningEntityManager::Instance().GetPlayerHealth();
            LightningEntityManager::Instance().SetPlayerHealth(std::min(curHp + 25.0f, LightningEntityManager::Instance().GetPlayerMaxHealth()));
        } else if (item_id == 2) {
            float curMp = LightningEntityManager::Instance().GetPlayerMana();
            LightningEntityManager::Instance().SetPlayerMana(std::min(curMp + 25.0f, LightningEntityManager::Instance().GetPlayerMaxMana()));
        } else if (item_id > 0) {
            gInventory.AddToBackpack(item_id, quantity);
        }
        if (OmegaTechSoundData.UIClick.frameCount > 0)
            PlaySound(OmegaTechSoundData.UIClick);
    });

    g_client.set_on_player_hurt([](int damage, float remaining_health) {
        LightningEntityManager::Instance().SetPlayerHealth(remaining_health);
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

        g_playerMovement.OldX = OmegaTechData.MainCamera.position.x;
        g_playerMovement.OldY = OmegaTechData.MainCamera.position.y;
        g_playerMovement.OldZ = OmegaTechData.MainCamera.position.z;

        // Save Y so we can override raylib's built-in Space/Shift vertical movement
        float savedCamY = OmegaTechData.MainCamera.position.y;

        if (!ShowSettings && !ShowInventory && !g_consoleOpen){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

        // --- Zone volume detection + movement effects (uses pre-computed player region) ---
        {
            float dt = GetFrameTime();
            Vector3 playerPos = OmegaTechData.MainCamera.position;
            static std::string lastZoneName;

            // Get active zones from pre-computed player region (set in UpdateEntities)
            auto& region = PawnSystem::Instance().GetPlayerRegion();
            ZoneVolumeNode* activeZone = (region.primaryZoneId >= 0)
                ? PawnSystem::Instance().GetZone(region.primaryZoneId) : nullptr;
            g_playerMovement.inWater = false;

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
                        g_playerMovement.inWater = true;
                        break;
                    case ZoneType::ZONE_LADDER:
                        // Ladder: disable gravity, allow vertical movement with W/S
                        g_playerMovement.velocityY = 0.0f;
                        g_playerMovement.onGround = false;
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

            if (g_playerMovement.isNoClip) {
                // Noclip: let raylib control Y natively (space up / shift down)
            } else if (g_playerMovement.isFlying) {
                // Flying: let raylib control Y, no terrain snap
            } else if (g_playerMovement.inWater) {
                // Water: restore Y, reduced gravity, dampen fall
                OmegaTechData.MainCamera.position.y = savedCamY;

                if (IsKeyPressed(KEY_SPACE) && !g_consoleOpen && !ShowInventory) {
                    g_playerMovement.velocityY = 5.0f; // swim upward
                }

                if (!g_playerMovement.onGround) {
                    g_playerMovement.velocityY += -8.0f * dt; // reduced gravity
                    g_playerMovement.velocityY *= 0.95f;      // water drag
                    OmegaTechData.MainCamera.position.y += g_playerMovement.velocityY * dt;
                }
            } else {
                // Normal / grounded: restore Y
                OmegaTechData.MainCamera.position.y = savedCamY;

                if (IsKeyPressed(KEY_SPACE) && g_playerMovement.onGround && !g_consoleOpen && !ShowInventory) {
                    g_playerMovement.velocityY = 8.0f;
                    g_playerMovement.onGround = false;
                }

                // Gravity
                if (!g_playerMovement.onGround) {
                    g_playerMovement.velocityY += -20.0f * dt;
                    OmegaTechData.MainCamera.position.y += g_playerMovement.velocityY * dt;
                }
            }
        }

        // Weapon fire AFTER camera so left-click does not disrupt movement
        if (!ShowInventory && !g_consoleOpen && left_just_pressed) {
            EntityInstance* wep = LightningEntityManager::Instance().SelectedEntity();
            if (wep && wep->def && wep->def->type == EntityType::WEAPON) {
                FireWeapon();
            }
        }

        left_click_was_down = left_click_now;

        OmegaInputController.UpdateInputs();
        
        // TODO: Legacy Function -> Refactor into Renderer/LitLightning.hpp
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
            Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            float yaw = atan2f(fwd.x, fwd.z);
            float pitch = asinf(fwd.y);
            g_client.update(cam.position.x, cam.position.y, cam.position.z,
                            yaw, pitch);

            if (g_client.is_connected()) {
                LightningEntityManager::Instance().SetPlayerLevel(g_client.get_level());
                LightningEntityManager::Instance().SetPlayerXP(g_client.get_xp());
                LightningEntityManager::Instance().SetPlayerXPToNext(g_client.get_xp_to_next());

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
                // TODO: also should be moved to be handled by PickupPawns
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

        // Screen flash on pickup collect (fades out)
        {
            auto& fb = PawnSystem::Instance().m_pickupFeedback;
            if (fb.flashTimer > 0.0f) {
                fb.flashTimer -= GetFrameTime();
                Color flashColor = {0, 0, 0, 0};
                const ItemDBEntry* def = GetItemDef(fb.itemId);
                if (def) {
                    switch (def->category) {
                        case ItemCategory::HEALTH_VIAL: flashColor = (Color){255, 50, 50, (unsigned char)(80 * fb.flashTimer * 2)}; break;
                        case ItemCategory::MANA_VIAL:  flashColor = (Color){50, 100, 255, (unsigned char)(80 * fb.flashTimer * 2)}; break;
                        case ItemCategory::ENERGY_CRYSTAL: flashColor = (Color){200, 50, 255, (unsigned char)(80 * fb.flashTimer * 2)}; break;
                        case ItemCategory::COIN:       flashColor = (Color){255, 215, 0, (unsigned char)(80 * fb.flashTimer * 2)}; break;
                        default:                       flashColor = (Color){255, 255, 255, (unsigned char)(60 * fb.flashTimer * 2)}; break;
                    }
                }
                if (flashColor.a > 0)
                    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), flashColor);
            }
        }

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