#include "Core.hpp"
#include "Log.hpp"
#include "Menu/SettingsMenu.hpp"
#include "ClientSettings.hpp"
#include "Client/JoinUri.hpp"
#include "Client/ProtocolHandler.hpp"
#include "Client/Client.hpp"
#include "Script/LightningEntityManager.hpp"
#include "Script/LightningEntityRegistry.hpp"
#include "Script/LightningEntityDef.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include "Pawn/AngelPlayer/GameUi.hpp"
#include "Pawn/AngelPlayer/InventoryBehaviour.hpp"
#include "Pawn/AngelPlayer/WeaponBehaviour.hpp"
#include "Pawn/AngelPlayer/PlayerController.hpp"
#include "Pawn/PickupPawns.hpp"
#include "Renderer/CombatFX.hpp"
#include "Renderer/Mesh/MeshCache.hpp"
#include "Renderer/ViewModel.hpp"
#include "Renderer/PlayerModel.hpp"
#include "Menu/SkillTree.hpp"
#include "UI/UiHandler.hpp"
#include <cmath>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
namespace fs = std::filesystem;

// Persist settings to System/Angels95.ini on every exit path (destructor runs
// at process teardown, after raylib has closed — stdio/stdlib only, so it is
// safe to run outside the window lifetime).
static struct ClientSettingsOnExit {
    ~ClientSettingsOnExit() { SaveClientSettings(); }
} g_saveClientSettings;

// The engine addresses every content path relative to the install root (the
// folder that holds GameData/ and System/). Launching System/Angels95.exe
// directly (double-click, shell shortcut, fullscreen launcher) leaves cwd
// inside System/, so the world OZONE, skybox and packages were never found
// even though network pawns from the server still spawned. Relocate to the
// root before anything reads a path.
static bool IsInstallRoot(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p / "GameData", ec) ||
           fs::exists(p / "System" / "Data", ec) ||
           fs::exists(p / "System" / "Angels95.ini", ec);
}

static void RelocateToInstallRoot() {
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (cwd.empty() || IsInstallRoot(cwd)) return;

    fs::path exeDir = fs::path(ProtocolHandler::ExecutablePath()).parent_path();
    std::vector<fs::path> candidates;
    if (!exeDir.empty()) {
        candidates.push_back(exeDir);
        candidates.push_back(exeDir.parent_path());
    }
    if (!cwd.parent_path().empty()) candidates.push_back(cwd.parent_path());

    // Prefer a root that actually ships loose GameData; package-only installs
    // (System/Data without GameData) are the fallback.
    for (bool requireGameData : {true, false}) {
        for (const auto& c : candidates) {
            if (c.empty()) continue;
            std::error_code existsEc;
            if (requireGameData && !fs::exists(c / "GameData", existsEc)) continue;
            if (!IsInstallRoot(c)) continue;
            fs::current_path(c, ec);
            std::fprintf(stderr, "[INFO] Working directory relocated to install root: %s\n",
                         c.string().c_str());
            return;
        }
    }
}

static OmegaClient g_client;
static bool g_network_enabled = false;
static bool ShowInventory = false;
static bool ShowSkillTree = false;

// Deep-link / CLI join target. SetServerJoinIP is a raw pointer consumed long
// after parsing, so the host string must live for the whole process.
static std::string g_cliJoinHost;

static bool ApplyJoinTarget(const char* value)
{
    std::string host;
    int port = JoinUri::kDefaultPort;
    if (!JoinUri::Parse(value, host, port, JoinUri::kDefaultPort) &&
        !JoinUri::ParseHostPort(value, host, port, JoinUri::kDefaultPort))
        return false;

    g_cliJoinHost = host;
    SetServerJoinIP = g_cliJoinHost.c_str();
    SetServerJoinPort = port;
    SetServerJoinFlag = true;
    g_skipMenu = true;
    OZ_INFO("Deep link: joining %s:%d", SetServerJoinIP, SetServerJoinPort);
    return true;
}

// Recoil & crosshair state now owned by AngelPlayer::WeaponBehaviour.


static Color unpack_color(uint32_t packed) {
    return (Color){
        (unsigned char)(packed & 0xFF),
        (unsigned char)((packed >> 8) & 0xFF),
        (unsigned char)((packed >> 16) & 0xFF),
        (unsigned char)((packed >> 24) & 0xFF)
    };
}

// ---------------------------------------------------------------------------
// Remote player rendering — drawn inside DrawWorld()'s BeginMode3D pass
// (called from Core.hpp). TODO: Move into Render/
// ---------------------------------------------------------------------------
// Per-remote-player animation state (playback lives outside the shared mesh).
struct RemoteAnimState {
    net::NetVec3 last{0, 0, 0};
    bool hasLast = false;
    float time = 0.0f;
    int clip = -1;
};

void DrawRemotePlayers3D() {
    if (!g_network_enabled || !g_client.is_connected()) return;

    // Shared player character (Player def model, else the Player convention).
    // STUB: falls back to the primitive capsule when no model loads.
    auto& pm = oz::PlayerModel::Instance();
    oz::Mesh* mesh = pm.Get();
    oz::SkeletalMesh* skel = pm.Skeletal();
    const bool animated = skel != nullptr;
    const EntityDef* pdef = LightningEntityRegistry::Instance().Find("Player");
    const float dt = GetFrameTime();

    static std::unordered_map<uint32_t, RemoteAnimState> anim;
    const auto& players = g_client.remote_players();
    for (const auto& rp : players) {
        if (!rp.active) continue;
        // rp.position is the sender's camera (eye) position; draw from the feet
        // so remote bodies rest on the ground instead of floating at eye level.
        float eyeH = (rp.stance == net::STANCE_CROUCH)
                   ? g_playerMovement.CrouchEyeHeight
                   : g_playerMovement.StandEyeHeight;
        Vector3 pos = {rp.position.x, rp.position.y - eyeH, rp.position.z};
        Color col = unpack_color(rp.color_packed);

        if (mesh) {
            int clip = -1;
            float clipTime = 0.0f;
            if (animated) {
                RemoteAnimState& a = anim[rp.player_id];
                float dx = rp.position.x - a.last.x;
                float dz = rp.position.z - a.last.z;
                bool moving = a.hasLast && (dx * dx + dz * dz) > 0.0004f;
                int wanted = moving
                    ? skel->FindClip(pdef ? pdef->animChase : "")
                    : skel->FindClip(pdef ? pdef->animIdle : "");
                if (wanted < 0) wanted = 0;
                if (wanted != a.clip) { a.clip = wanted; a.time = 0.0f; }
                a.time += dt * (rp.stance == net::STANCE_SPRINT ? 1.5f : 1.0f);
                a.last = rp.position;
                a.hasLast = true;
                clip = a.clip;
                clipTime = a.time;
            }
            oz::MeshTransform mt;
            mt.position = pos;
            mt.yaw = rp.yaw * RAD2DEG;
            // Replicated crouch: squash the model from the feet so it reads as
            // a lowered stance (static stub - no skeletal crouch clip yet).
            float yScale = (rp.stance == net::STANCE_CROUCH) ? 0.65f : 1.0f;
            mt.scale = {1.0f, yScale, 1.0f};
            pm.DrawInstance(mt, {0}, clip, clipTime);
        } else {
            const float height = (rp.stance == net::STANCE_CROUCH) ? 4.5f : 8.0f;
            const float radius = 1.5f;
            DrawCylinder(pos, radius, radius, height, 8, col);
            DrawSphere({pos.x, pos.y + height + 1.0f, pos.z}, 1.2f, col);
        }

        // Facing indicator — kept in both paths so players stay distinguishable.
        DrawLine3D({pos.x, pos.y + 4.0f, pos.z},
                   {pos.x + sinf(rp.yaw) * 3.0f, pos.y + 4.0f, pos.z + cosf(rp.yaw) * 3.0f},
                   col);
    }
}



// ---------------------------------------------------------------------------
// Inventory overlay (draw when Tab pressed)
// ---------------------------------------------------------------------------
// ---- Debug Console ----
// g_showCollisionDebug defined in Core.hpp
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
            if (IsPathFile(worldPath)) {
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
                    std::string oz  = entry.path().string() + "/World.ozone";
                    if (IsPathFile(oz.c_str()))
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
    } else if (strncmp(cmd, "/connect ", 9) == 0 || strcmp(cmd, "/connect") == 0) {
        const char* arg = (cmd[8] == ' ') ? cmd + 9 : "";
        while (*arg == ' ') arg++;
        std::string host;
        int port = JoinUri::kDefaultPort;
        if (!JoinUri::ParseHostPort(arg, host, port, JoinUri::kDefaultPort)) {
            OZ_WARN("Usage: /connect <ip|host>[:port]");
        } else if (g_network_enabled) {
            OZ_WARN("Already connected - use /disconnect first");
        } else {
            static std::string s_consoleJoinHost;
            s_consoleJoinHost = host;
            SetServerJoinIP = s_consoleJoinHost.c_str();
            SetServerJoinPort = port;
            SetServerJoinFlag = true;
            g_network_enabled = g_client.connect(SetServerJoinIP, SetServerJoinPort);
            OZ_INFO("Connecting to %s:%d", SetServerJoinIP, SetServerJoinPort);
        }
    } else if (strcmp(cmd, "/registerprotocol") == 0) {
        if (ProtocolHandler::EnsureRegistered())
            OZ_INFO("angels95:// protocol handler registered -> %s",
                    ProtocolHandler::ExecutablePath().c_str());
        else
            OZ_WARN("Could not register angels95:// protocol handler");
    } else if (strcmp(cmd, "/disconnect") == 0) {
        if (g_network_enabled) {
            g_client.disconnect();
            g_network_enabled = false;
            OZ_INFO("Disconnected from server");
        } else {
            OZ_WARN("Not connected to any server");
        }
    } else if (strcmp(cmd, "/servers") == 0) {
        OZ_INFO("Use the Multiplayer tab in the main menu to discover servers");
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

int main(int argc, char** argv){
    RelocateToInstallRoot();

    // CLI args
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        const std::string argLower = JoinUri::Lower(arg);
        if (strcmp(arg, "--world") == 0 && i + 1 < argc) {
            strncpy(g_world_to_load, argv[++i], sizeof(g_world_to_load) - 1);
            g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
            g_skipMenu = true;
        } else if (strcmp(arg, "--world-dir") == 0 && i + 1 < argc) {
            strncpy(g_world_dir_override, argv[++i], sizeof(g_world_dir_override) - 1);
            g_world_dir_override[sizeof(g_world_dir_override) - 1] = '\0';
        } else if ((strcmp(arg, "--join") == 0 || strcmp(arg, "--connect") == 0) && i + 1 < argc) {
            if (!ApplyJoinTarget(argv[++i]))
                OZ_WARN("Invalid --join target: %s", argv[i]);
        } else if (strncmp(arg, "--join=", 7) == 0 || strncmp(arg, "--connect=", 10) == 0) {
            const char* value = arg + ((arg[2] == 'j') ? 7 : 10);
            if (!ApplyJoinTarget(value))
                OZ_WARN("Invalid --join target: %s", value);
        } else if (argLower.rfind("angels95://", 0) == 0) {
            if (!ApplyJoinTarget(arg))
                OZ_WARN("Unrecognized join URI: %s", arg);
        }
    }

    // Load persisted user settings BEFORE window creation so window size,
    // VSync and MSAA are applied by InitWindow.
    LoadClientSettings();

    // Browser deep links (angels95://join/...) need an OS protocol handler.
    // HKCU / user .desktop only, so no elevation is required.
    if (ProtocolHandler::EnsureRegistered())
        OZ_INFO("Protocol handler: angels95:// -> %s", ProtocolHandler::ExecutablePath().c_str());
    else
        OZ_WARN("Protocol handler: angels95:// registration unavailable");
    if (VSYNCToggle) SetConfigFlags(FLAG_VSYNC_HINT);
    if (MXAAToggle)  SetConfigFlags(FLAG_MSAA_4X_HINT);

    InitWindow(ConfigWindowWidth, ConfigWindowHeight, "Angels95");
    SetExitKey(0);
    SetTargetFPS(60);

    InitAudioDevice();

    if (IsAudioDeviceReady()) {
        // Wire the zone reverb DSP into the master output mix.
        DspReverb::Reset();
        AttachAudioMixedProcessor(DspReverb::AudioCallback);
    } else {
        CloseAudioDevice();
    }
    ApplyMasterVolume();

    OmegaTechInit();
    GameUi::Instance().Init();
    InventoryBehaviour::Instance().SetMessageSink([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });
    InventoryBehaviour::Instance().SetFeedbackSink([]() {
        if (OmegaTechSoundData.UIClick.frameCount > 0)
            PlaySound(OmegaTechSoundData.UIClick);
    });
    WeaponBehaviour::Instance().SetClient(&g_client, &g_network_enabled);
    PickupPawns::Instance().SetClient(&g_client);
    oz::ui::CreateNativeMenuBar({
        []() { SetSceneId = OmegaTechData.LevelIndex; SetSceneFlag = true; },
        []() { SaveGame(); },
        []() { CloseWindow(); },
        []() {
            ShowSettings = !ShowSettings;
            if (ShowSettings) { ShowCursor(); EnableCursor(); }
            else { HideCursor(); DisableCursor(); }
        },
        []() { oz::ui::ShowAboutDialog(); },
    });
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
        InventoryBehaviour::Instance().OnItemCollected(item_id, quantity);
    });

    g_client.set_on_player_hurt([](int damage, float remaining_health) {
        LightningEntityManager::Instance().SetPlayerHealth(remaining_health);
        if (OmegaTechSoundData.Death.frameCount > 0 &&
            !IsSoundPlaying(OmegaTechSoundData.Death))
            PlaySound(OmegaTechSoundData.Death);
    });

    // Weapon pickup granted by the server (item_id 15) — add to hotbar.
    g_client.set_on_weapon_collected([](const char* weapon_def_name) {
        InventoryBehaviour::Instance().OnWeaponCollected(weapon_def_name);
    });

    // Ammo changes (fire/reload) are reported to the server for remote sync.
    LightningEntityManager::Instance().set_on_ammo_changed([](int slot, int ammo, int magazine, int action) {
        if (g_network_enabled) g_client.send_weapon_ammo(slot, ammo, magazine, action);
    });

    // Server tells us which world is active — switch to it on mismatch.
    g_client.set_on_scene_received([](const std::string& sceneJson) {
        // Tiny hand-parse of the fixed server template: "active_world":"<name>"
        static const std::string key = "\"active_world\":\"";
        size_t kpos = sceneJson.find(key);
        if (kpos == std::string::npos) return;
        size_t start = kpos + key.size();
        // Tolerate servers that emitted a second pair of quotes around the
        // value ("active_world":""Name"") before this was fixed.
        while (start < sceneJson.size() && sceneJson[start] == '"') ++start;
        size_t end = sceneJson.find('"', start);
        if (end == std::string::npos) return;
        std::string activeWorld = sceneJson.substr(start, end - start);
        if (activeWorld.empty() || activeWorld == g_world_to_load) return;
        OZ_INFO("Network: server active world is '%s' — switching", activeWorld.c_str());
        strncpy(g_world_to_load, activeWorld.c_str(), sizeof(g_world_to_load) - 1);
        g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
        SetSceneFlag = true;
    });

    if (SetServerJoinFlag && SetServerJoinIP) {
        // Final guard: never hand a malformed host to connect() (defends against
        // a corrupted menu textbox producing e.g. a one-character address).
        std::string host;
        int port = SetServerJoinPort;
        if (JoinUri::ParseHostPort(SetServerJoinIP, host, port, SetServerJoinPort) && !host.empty()) {
            g_network_enabled = g_client.connect(host.c_str(), (uint16_t)port);
            if (g_network_enabled)
                OZ_INFO("Network: connected to %s:%d", host.c_str(), port);
            else
                OZ_WARN("Network: connect to %s:%d failed", host.c_str(), port);
        } else {
            OZ_WARN("Network: refusing invalid join address '%s'", SetServerJoinIP);
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
        PlayerController::Instance().HandleKeyToggles(ShowInventory, ShowSkillTree, g_consoleOpen);

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
                else if (!ShowSettings && !ShowInventory && !ShowSkillTree && !g_consoleOpen) { HideCursor(); DisableCursor(); }
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

        // Game-over overlay when player has died 3 times
        if (PlayerController::Instance().DrawGameOver(OmegaTechData.Deaths,
                OmegaTechData.BtnClicked, OmegaTechData.BtnHover, OmegaTechData.BtnNormal,
                g_returnToMenu,
                []() { LoadWorld(); })) {
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

        // Save Y so the vertical physics below can own it (the horizontal
        // controller never touches Y).
        float savedCamY = OmegaTechData.MainCamera.position.y;

        // Custom first-person controller: mouse look + WASD at a speed derived
        // from the player entity's authored movement_speed, with hold-Shift
        // sprint and hold-Ctrl crouch.
        {
            bool uiBlocked = ShowSettings || ShowInventory || ShowSkillTree || g_consoleOpen;
            float moveSpeedScalar = LightningEntityManager::Instance().GetPlayerMovementSpeed();
            g_playerMovement.UpdateLookAndMove(
                OmegaTechData.MainCamera, GetFrameTime(), moveSpeedScalar, uiBlocked);
        }

        // Fixed 60 Hz timestep for movement physics (zone/water/jump/gravity).
        // Discrete 1/60s steps, at most 4 per render frame, with leftover time
        // carried across frames so the sim advances at a constant 60 Hz cadence
        // regardless of render FPS.
        int move_steps = PlayerController::Instance().ConsumeMoveSteps(GetFrameTime());
        const float kMoveDt = PlayerController::MoveDeltaSeconds();

        // --- Zone volume detection + movement effects (uses pre-computed player region) ---
        // --- Zone volume detection + movement effects (uses pre-computed player region) ---
        oz::physics::PhysicsInfo zonePhysics; // defaults; overridden per zone
        for (int s = 0; s < move_steps; ++s) {
        {
            const float dt = kMoveDt;
            Vector3 playerPos = OmegaTechData.MainCamera.position;
            static std::string lastZoneName;

            // Get active zones from pre-computed player region (set in UpdateEntities)
            auto& region = PawnSystem::Instance().GetPlayerRegion();
            ZoneVolumeNode* activeZone = (region.primaryZoneId >= 0)
                ? PawnSystem::Instance().GetZone(region.primaryZoneId) : nullptr;
            g_playerMovement.inWater = false;

            if (activeZone) {
                // Per-zone authored physics overrides (falls back to defaults
                // for worlds that do not export gravity= style kwargs).
                zonePhysics = activeZone->physics;

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
                            OmegaTechData.MainCamera.position.y += activeZone->physics.ladderSpeed * dt;
                        if (IsKeyDown(KEY_S))
                            OmegaTechData.MainCamera.position.y -= activeZone->physics.ladderSpeed * dt;
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
        }
        // --- Jump / Fly / Noclip Y management ---
        const bool verticalBlocked = g_consoleOpen || ShowInventory || ShowSkillTree;
        for (int s = 0; s < move_steps; ++s) {
            PlayerController::Instance().UpdateVertical(kMoveDt, OmegaTechData.MainCamera,
                                                        savedCamY, verticalBlocked, zonePhysics);
        }

        const bool uiBlocking = ShowInventory || ShowSkillTree || g_consoleOpen;

        // Weapon fire AFTER camera so left-click does not disrupt movement
        WeaponBehaviour::Instance().HandleInput(uiBlocking, OmegaTechData.MainCamera);

        // ADS / Zoom (right-click)
        WeaponBehaviour::Instance().SetAdsActive(!uiBlocking && IsMouseButtonDown(MOUSE_BUTTON_RIGHT));

        // Recoil recovery + camera application
        WeaponBehaviour::Instance().Update(OmegaTechData.MainCamera);

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
        if (IsKeyPressed(KEY_R)) oz::ViewModel::Instance().TriggerReload();
        LightningEntityManager::Instance().HandleInput();

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
            uint8_t stance = g_playerMovement.isCrouching ? net::STANCE_CROUCH
                           : (g_playerMovement.isSprinting ? net::STANCE_SPRINT
                                                           : net::STANCE_STAND);
            g_client.update(cam.position.x, cam.position.y, cam.position.z,
                            yaw, pitch, stance);

            if (g_client.is_connected()) {
                LightningEntityManager::Instance().SetPlayerLevel(g_client.get_level());
                LightningEntityManager::Instance().SetPlayerXP(g_client.get_xp());
                LightningEntityManager::Instance().SetPlayerXPToNext(g_client.get_xp_to_next());

                // Map each server NPC (world+partition+index) to the local pawn
                // spawned for it. The previous code assumed pawn id == npc
                // index+1, which breaks as soon as LoadWorld() calls DespawnAll()
                // (map switch): Get() then misses, so it spawned a brand-new pawn
                // AND script entity every single frame -> unbounded growth and
                // ~0 FPS. Entries are dropped when their pawn disappears.
                static std::unordered_map<uint64_t, int> s_npcPawnId;
                const auto& npcs = g_client.npcs();
                for (size_t i = 0; i < npcs.size(); i++) {
                    const uint64_t key =
                        ((uint64_t)(uint32_t)npcs[i].world_index     << 42) |
                        ((uint64_t)(uint32_t)npcs[i].npc_index       << 21) |
                         (uint64_t)(uint32_t)npcs[i].partition_index;

                    Pawn* p = nullptr;
                    auto it = s_npcPawnId.find(key);
                    if (it != s_npcPawnId.end()) {
                        p = PawnSystem::Instance().Get(it->second);
                        if (!p) s_npcPawnId.erase(it); // despawned/reloaded -> respawn once
                    }

                    if (p) {
                        p->position = { npcs[i].position.x, npcs[i].position.y, npcs[i].position.z };
                        p->active = npcs[i].active;
                    } else if (npcs[i].active) {
                        // Spawn with the server-reported def type (falls back to Walker)
                        const char* type = npcs[i].npc_type[0] ? npcs[i].npc_type : "Walker";
                        int pawnId = PawnSystem::Instance().Spawn({ npcs[i].position.x, npcs[i].position.y, npcs[i].position.z }, type);
                        if (pawnId < 0)
                            pawnId = PawnSystem::Instance().Spawn({ npcs[i].position.x, npcs[i].position.y, npcs[i].position.z }, "Walker");
                        if (pawnId >= 0) {
                            s_npcPawnId[key] = pawnId;
                            Pawn* np = PawnSystem::Instance().Get(pawnId);
                            if (np) {
                                np->networkControlled = true;
                                // Server owns AI/position; don't double-simulate locally.
                                if (strcmp(np->defName.c_str(), type) != 0) np->defName = type;
                            }
                        }
                    }
                }

                // Auto-collect when walking over a pickup (also E) — handled
                // by PickupPawns (networked pickups only; throttled internally).
                PickupPawns::Instance().Update(IsKeyPressed(KEY_E),
                                               OmegaTechData.MainCamera.position,
                                               GetTime());
            }
        }

        // HUD: player stats (always visible)
        InventoryBehaviour::Instance().DrawHud(OmegaTechData.MainCamera, OmegaTechData.Ticker,
                                               g_playerMovement.isCrouching, g_playerMovement.isSprinting);

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

        // Inventory overlay
        if (ShowInventory) {
            InventoryBehaviour::Instance().DrawOverlay();
        }

        // Ethereal skill tree overlay
        if (ShowSkillTree) {
            oz::skilltree::DrawOverlay(ShowSkillTree);
        }

        // Console overlay (always on top)
        DrawConsole();

        // Crosshair
        if (!ShowInventory && !ShowSkillTree && !g_consoleOpen) {
            int cx = GetScreenWidth() / 2;
            int cy = GetScreenHeight() / 2;
            const bool ads = WeaponBehaviour::Instance().AdsActive();
            int gap = (ads ? 2 : 5) + (int)(WeaponBehaviour::Instance().CrosshairBloom() * (ads ? 0.3f : 1.0f));
            int len = 12;
            Color col = {255, 255, 255, 180};
            DrawLine(cx - gap - len, cy, cx - gap, cy, col);
            DrawLine(cx + gap, cy, cx + gap + len, cy, col);
            DrawLine(cx, cy - gap - len, cx, cy - gap, col);
            DrawLine(cx, cy + gap, cx, cy + gap + len, col);
        }

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
    for (int i = 0; i < 6; i++) {
        if (OmegaTechData.SkyboxFace[i].meshCount > 0) {
            UnloadModel(OmegaTechData.SkyboxFace[i]);
            OmegaTechData.SkyboxFace[i] = Model{0};
        }
    }
    CloseWindow();
}