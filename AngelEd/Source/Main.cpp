#include "../../Source/WindowsCompat.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Editor.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "Win32Dialogs.hpp"
#include "EditorIcons.hpp"
#include "../../Source/IniConfig.hpp"
#include "../../Source/OzOzoneLoader.hpp"
#include "../../Source/Pawn/OzPawnSystem.hpp"
#include "../../Source/Package/PackageAssetLoader.hpp"
#include "../../Source/Server/WDLParser.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

// WDLModels definition (extern declared in Editor.hpp)
GameModels WDLModels;

// CSG sidebar bridge globals
int g_csgOpFromSidebar = 0;
bool g_csgCollisionFromSidebar = false;

void CsgSidebarPlace(float px, float py, float pz, float w, float h, float d, float rot, float scale) {
    OmegaTechEditor.X = px; OmegaTechEditor.Y = py; OmegaTechEditor.Z = pz;
    OmegaTechEditor.W = w;  OmegaTechEditor.H = h;  OmegaTechEditor.L = d;
    OmegaTechEditor.R = rot; OmegaTechEditor.S = scale;
    OmegaTechEditor.DrawModel = true;
}

// Editor log file (appended to System/AngelEd.log)
static FILE* g_editorLog = nullptr;
static void EditorLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    if (!g_editorLog) {
        g_editorLog = fopen("System/AngelEd.log", "a");
        if (!g_editorLog) g_editorLog = fopen("AngelEd.log", "a");
    }
    if (g_editorLog) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
        fprintf(g_editorLog, "[%s] ", ts);
        va_start(args, fmt);
        vfprintf(g_editorLog, fmt, args);
        fprintf(g_editorLog, "\n");
        fflush(g_editorLog);
        va_end(args);
    }
}

enum class PlaceMode { MODEL, PICKUP, NODE, ENV };
static PlaceMode g_placeMode = PlaceMode::MODEL;

// INI config
static IniConfig g_config;
static fs::path g_documentPath;
static fs::path g_pendingOpenPath;
static bool g_pendingNew = false;
static Sound g_previewSound = {0};
static bool g_previewSoundLoaded = false;

// Panel toggle helpers (keyboard-driven, no top menu bar)
static void ToggleSoundMgr()    { ShowSoundManager(!g_editorPanels.showSoundMgr); }
static void ToggleTextureMgr()  { ShowTextureManager(!g_editorPanels.showTextureMgr); }
static void TogglePawnMgr()     { ShowPawnManager(!g_editorPanels.showPawnMgr); }
static void ToggleScriptMgr()   { ShowScriptManager(!g_editorPanels.showScriptMgr); }
static void ToggleModelBrowser(){ ShowModelBrowser(!g_editorPanels.showModelBrowser); }
static void TogglePickupPanel() { ShowPickupPanel(!g_editorPanels.showPickupPanel); }
static void ToggleNodePanel()   { ShowNodePanel(!g_editorPanels.showNodePanel); }
static void ToggleEnvPanel()    { ShowEnvPanel(!g_editorPanels.showEnvPanel); g_placeMode = PlaceMode::ENV; }
static void ToggleHeightmapEditor() { ShowHeightmapEditor(!g_editorPanels.showHeightmapEditor); }
static void ToggleCollision()   { CollisionToggle = !CollisionToggle; }
static void ResetCamera()       { OTEditor.MainCamera.position = {0, 10, 0}; }
static void CamUp()             { OTEditor.MainCamera.position.y += 2; }
static void CamDown()           { OTEditor.MainCamera.position.y -= 2; }

// Model preview for Win32 dialog (render-to-texture)
static RenderTexture2D g_previewRT = {0};
static int g_lastPreviewSel = -1;
static bool g_previewNeedsUpdate = false;

static void SetWorldDirectory(const fs::path& directory) {
    std::string path = directory.string();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += fs::path::preferred_separator;
    std::strncpy(OTEditor.Path, path.c_str(), sizeof(OTEditor.Path) - 1);
    OTEditor.Path[sizeof(OTEditor.Path) - 1] = '\0';
}

static void StopSoundPreview() {
    if (!g_previewSoundLoaded) return;
    StopSound(g_previewSound);
    UnloadSound(g_previewSound);
    g_previewSound = {0};
    g_previewSoundLoaded = false;
}

static void ClearScene() {
    StopSoundPreview();
    OTEditor.WorldData.clear();
    OTEditor.OtherData.clear();
    CacheWDL();
    OzoneLoader::Instance().Unload();
    auto& pawns = PawnSystem::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    pawns.ClearZones();
    OmegaTechEditor.DrawModel = false;
}

static ZoneType ParseWDLZoneType(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (name == "ladder" || name == "1") return ZoneType::ZONE_LADDER;
    if (name == "sky" || name == "2") return ZoneType::ZONE_SKY;
    if (name == "reverb" || name == "3") return ZoneType::ZONE_REVERB;
    return ZoneType::ZONE_WATER;
}

static void SyncWDLNodes() {
    static const char* pickupNames[] = {"HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"};
    auto& pawns = PawnSystem::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    pawns.ClearZones();

    std::string content(OTEditor.WorldData.begin(), OTEditor.WorldData.end());
    for (const auto& element : WDLParser::parse_string(content)) {
        if (element.type == WDLElementType::SPAWN && element.args.size() >= 3) {
            pawns.AddPlayerStart({0, {element.args[0], element.args[1], element.args[2]}, element.yaw});
        } else if (element.type == WDLElementType::PICKUP && element.args.size() >= 3) {
            PickupNode node;
            node.position = {element.args[0], element.args[1], element.args[2]};
            node.typeName = element.pickupType;
            try {
                int type = std::stoi(node.typeName);
                if (type >= 0 && type < 6) node.typeName = pickupNames[type];
            } catch (...) {
                EditorLog("WARN: Failed to parse pickup type '%s', using as-is", node.typeName.c_str());
            }
            pawns.AddPickup(node);
        } else if (element.type == WDLElementType::NPC && element.args.size() >= 3) {
            pawns.Spawn({element.args[0], element.args[1], element.args[2]}, element.entityType.c_str());
        } else if (element.type == WDLElementType::ZONE_INFO && element.args.size() >= 6) {
            ZoneVolumeNode node;
            node.bounds = {{element.args[0], element.args[1], element.args[2]},
                           {element.args[3], element.args[4], element.args[5]}};
            node.zoneType = ParseWDLZoneType(element.zoneType);
            node.intensity = element.intensity;
            pawns.AddZone(node);
        }
    }
}

static bool LoadWorldDocument(const fs::path& path) {
    EditorLog("Loading world: %s", path.string().c_str());
    ClearScene();
    SetWorldDirectory(path.parent_path());
    g_documentPath = path;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (extension == ".ozone") {
        EditorLog("OZONE format: %s", path.string().c_str());
        return OzoneLoader::Instance().LoadFile(path.string().c_str());
    }
    if (extension != ".wdl") return false;

    // Reload models from the new world's Models/ directory
    WDLModels.LoadModels(OTEditor.Path);
    {
        std::vector<std::string> names;
        for (int i = 0; i < WDLModels.GetModelCount(); i++)
            if (WDLModels.GetModelName(i)) names.push_back(WDLModels.GetModelName(i));
        SetTextureTargetNames(names);
    }

    OTEditor.WorldData = LoadFile(path.string().c_str());
    CacheWDL();
    SyncWDLNodes();
    return true;
}

static void AppendOzoneEntities(std::wofstream& output) {
    auto& pawns = PawnSystem::Instance();
    for (const auto& start : pawns.GetPlayerStarts())
        output << L"Spawn:" << start.position.x << L":" << start.position.y << L":" << start.position.z << L":" << start.yaw << L":\n";
    for (const auto& pickup : pawns.GetPickups())
        output << L"Pickup:" << std::wstring(pickup.typeName.begin(), pickup.typeName.end()) << L":"
               << pickup.position.x << L":" << pickup.position.y << L":" << pickup.position.z << L":\n";
    for (const auto& pawn : pawns.GetPawns()) {
        if (!pawn.active || pawn.defName.empty()) continue;
        output << L"NPC:" << std::wstring(pawn.defName.begin(), pawn.defName.end()) << L":"
               << pawn.position.x << L":" << pawn.position.y << L":" << pawn.position.z << L":\n";
    }
    for (const auto& zone : pawns.GetZones())
        output << L"ZoneInfo:" << (int)zone.zoneType << L":"
               << zone.bounds.min.x << L":" << zone.bounds.min.y << L":" << zone.bounds.min.z << L":"
               << zone.bounds.max.x << L":" << zone.bounds.max.y << L":" << zone.bounds.max.z << L":"
               << zone.intensity << L":\n";
}

static bool SaveWorldDocument(const fs::path& path) {
    if (path.extension() != ".wdl") return false;
    std::wofstream output(path);
    if (!output.is_open()) return false;
    output << OTEditor.WorldData;
    if (g_documentPath.extension() == ".ozone") AppendOzoneEntities(output);
    g_documentPath = path;
    SetWorldDirectory(path.parent_path());
    return true;
}

static void FileNew() {
    g_pendingNew = true;
}

static void FileOpen() {
    std::string path;
    if (ChooseOpenWorldFile(path)) g_pendingOpenPath = fs::path(path);
}

static void FileSaveAs() {
    std::string path;
    if (ChooseSaveWorldFile(path)) SaveWorldDocument(fs::path(path));
}

static bool ApplyTextureToModel(int target, const char* path) {
    LoadedModel* lm = WDLModels.GetModelByWDLId(target);
    if (!lm || lm->model.materialCount == 0) return false;
    Texture2D texture = LoadTextureWithFallback(path);
    if (texture.id == 0) return false;
    if (lm->texture.id > 0) UnloadTexture(lm->texture);
    lm->texture = texture;
    SetMaterialTexture(&lm->model.materials[0], MATERIAL_MAP_DIFFUSE, texture);
    return true;
}

int main(int argc, char **argv){
    // Auto-detect repo root: if cwd ends with /System, go up one level
    {
        auto cwd = fs::current_path();
        std::string dir = cwd.filename().string();
        std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
        if (dir == "system")
            fs::current_path(cwd.parent_path());
    }

    EditorLog("=== AngelEd starting ===");
    SetWindowState(FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "AngelEd");
    SetTraceLogLevel(LOG_WARNING);
    InitAudioDevice();
    SetTargetFPS(60);
    GuiLoadStyleDark();

    // Load editor toolbar icons
    EditorIcons::Instance().Load();

    g_documentPath = argc > 1 && argv[1] ? fs::path(argv[1]) : fs::path("../GameData/World.wdl");
    SetWorldDirectory(g_documentPath.parent_path());

    // Load INI config
    g_config.Load("System/AngelEd.ini");

    // Initialize package-based asset loading
    PackageAssetLoader::Instance().Init();

#ifdef _WIN32
    CreateAllEditorWindows(GetModuleHandle(NULL), GetWindowHandle());
#endif

    // Load editor world
    Init();
    {
        std::vector<std::string> names;
        for (int i = 0; i < WDLModels.GetModelCount(); i++)
            if (WDLModels.GetModelName(i)) names.push_back(WDLModels.GetModelName(i));
        SetTextureTargetNames(names);
    }
    CacheWDL();
    ShowCsgSidebar(true);

    // Register default pawn definitions
    {
        auto& ps = PawnSystem::Instance();
        ps.RegisterDef({"Walker", 1.5f, 6.0f, 1.5f, 10.0f, 100});
        ps.RegisterDef({"Skaarj", 2.5f, 10.0f, 2.0f, 20.0f, 150});
        ps.RegisterDef({"Brute", 1.0f, 4.0f, 1.5f, 30.0f, 250});
        ps.RegisterDef({"Floater", 1.2f, 8.0f, 3.0f, 15.0f, 80});
        PawnManagerAddPawn("Walker", "GameData/Models/Walker.obj");
        PawnManagerAddPawn("Skaarj", "GameData/Models/Skaarj.obj");
        PawnManagerAddPawn("Brute", "GameData/Models/Brute.obj");
        PawnManagerAddPawn("Floater", "GameData/Models/Floater.obj");
    }

    if (argc > 1 && argv[1]) {
        LoadWorldDocument(g_documentPath);
    } else {
        SyncWDLNodes();
        fs::path ozonePath = g_documentPath.parent_path() / "World.ozone";
        if (fs::exists(ozonePath)) OzoneLoader::Instance().LoadFile(ozonePath.string().c_str());
    }

    // Suppress raylib's texture-not-found warnings from .obj material refs
    SetTraceLogLevel(LOG_WARNING);

    // Create model preview render texture
    g_previewRT = LoadRenderTexture(256, 256);

    EnableCursor();

    int LastClickTime = 0;
    bool DoubleClick = false;

    while (!WindowShouldClose())
    {
        // Process Win32 messages for child panels
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (g_pendingNew) {
            ClearScene();
            g_documentPath.clear();
            g_pendingNew = false;
        }
        if (!g_pendingOpenPath.empty()) {
            LoadWorldDocument(g_pendingOpenPath);
            g_pendingOpenPath.clear();
        }
        if (g_editorPanels.actionStopSoundPreview) {
            StopSoundPreview();
            g_editorPanels.actionStopSoundPreview = false;
        }
        if (!g_editorPanels.actionPreviewSoundPath.empty()) {
            StopSoundPreview();
            g_previewSound = LoadSound(g_editorPanels.actionPreviewSoundPath.c_str());
            g_previewSoundLoaded = g_previewSound.frameCount > 0;
            if (g_previewSoundLoaded) PlaySound(g_previewSound);
            g_editorPanels.actionPreviewSoundPath.clear();
        }
        if (g_editorPanels.actionTextureTarget > 0 && !g_editorPanels.actionTexturePath.empty()) {
            ApplyTextureToModel(g_editorPanels.actionTextureTarget, g_editorPanels.actionTexturePath.c_str());
            g_editorPanels.actionTextureTarget = -1;
            g_editorPanels.actionTexturePath.clear();
        }

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        {
            if (LastClickTime != 0) DoubleClick = true;
            else LastClickTime = 1;
        }
        if (LastClickTime != 0){
            LastClickTime ++;
            if (LastClickTime == 30){ LastClickTime = 0; DoubleClick = false; }
        }

        // Mouse-controlled camera: only move when middle-mouse-button is held.
        // Leaves the cursor free for clicking Win32 panels.
        // MMB          = pan horizontal (X/Z)
        // Shift + MMB  = pan vertical (Y)
        // Alt + MMB    = orbit around target
        // Scroll wheel = dolly forward/backward
        {
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                Vector2 delta = GetMouseDelta();
                float sensitivity = 0.1f;
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                bool alt   = IsKeyDown(KEY_LEFT_ALT)   || IsKeyDown(KEY_RIGHT_ALT);

                if (shift) {
                    // Shift + MMB = pan up/down
                    OTEditor.MainCamera.position.y -= delta.y * sensitivity;
                    OTEditor.MainCamera.target.y -= delta.y * sensitivity;
                } else if (alt) {
                    // Alt + MMB = orbit around target
                    Vector3 v = Vector3Subtract(OTEditor.MainCamera.position, OTEditor.MainCamera.target);
                    float radius = Vector3Length(v);
                    if (radius > 0.01f) {
                        Vector2 ang;
                        ang.x = atan2f(v.x, v.z); // yaw
                        ang.y = asinf(v.y / radius); // pitch
                        ang.x -= delta.x * 0.01f;
                        ang.y += delta.y * 0.01f;
                        if (ang.y > PI/2 - 0.01f) ang.y = PI/2 - 0.01f;
                        if (ang.y < -PI/2 + 0.01f) ang.y = -PI/2 + 0.01f;
                        v.x = radius * cosf(ang.y) * sinf(ang.x);
                        v.y = radius * sinf(ang.y);
                        v.z = radius * cosf(ang.y) * cosf(ang.x);
                        OTEditor.MainCamera.position = Vector3Add(OTEditor.MainCamera.target, v);
                    }
                } else {
                    // Plain MMB = pan X/Z
                    OTEditor.MainCamera.position.x -= delta.x * sensitivity;
                    OTEditor.MainCamera.position.z -= delta.y * sensitivity;
                    OTEditor.MainCamera.target.x -= delta.x * sensitivity;
                    OTEditor.MainCamera.target.z -= delta.y * sensitivity;
                }
            }

            // Scroll wheel = dolly forward/backward
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                Vector3 dir = Vector3Normalize(Vector3Subtract(OTEditor.MainCamera.target, OTEditor.MainCamera.position));
                OTEditor.MainCamera.position.x += dir.x * wheel * 2.0f;
                OTEditor.MainCamera.position.y += dir.y * wheel * 2.0f;
                OTEditor.MainCamera.position.z += dir.z * wheel * 2.0f;
                OTEditor.MainCamera.target.x += dir.x * wheel * 2.0f;
                OTEditor.MainCamera.target.y += dir.y * wheel * 2.0f;
                OTEditor.MainCamera.target.z += dir.z * wheel * 2.0f;
            }
        }
        
        // Apply ZoneProperties fog/ambient/particle settings
        {
            ZoneProperties zp = GetZoneProperties();
            if (zp.applyFog) {
                if (OTEditor.LitFogShader.id > 0) {
                    float fogColor[3] = {(float)zp.fogR / 255.0f, (float)zp.fogG / 255.0f, (float)zp.fogB / 255.0f};
                    float fogDensity = zp.fogDensity;
                    float fogIntensity = 1.0f;
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogColorLoc, fogColor, SHADER_UNIFORM_VEC3);
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogDensityLoc, &fogDensity, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogIntensityLoc, &fogIntensity, SHADER_UNIFORM_FLOAT);
                }
                OTEditor.FogColor = (Color){ (unsigned char)zp.fogR, (unsigned char)zp.fogG, (unsigned char)zp.fogB, 255 };
                OTEditor.FogDensity = zp.fogDensity;
            }
            if (zp.applyAmbient) {
                OTEditor.AmbientColor = (Color){ (unsigned char)zp.ambR, (unsigned char)zp.ambG, (unsigned char)zp.ambB, 255 };
                OTEditor.AmbientIntensity = zp.ambIntensity;
                if (OTEditor.AmbientLoc >= 0 && OTEditor.LitFogShader.id > 0) {
                    float ambient[4] = {(float)zp.ambR / 255.0f * zp.ambIntensity,
                                        (float)zp.ambG / 255.0f * zp.ambIntensity,
                                        (float)zp.ambB / 255.0f * zp.ambIntensity, 1.0f};
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.AmbientLoc, ambient, SHADER_UNIFORM_VEC4);
                }
            }
            ClearZoneApplyFlags();
        }

        ClearBackground(BLACK);

        // Apply lighting mode before 3D rendering
        if (OTEditor.ViewMode == LightingMode::WIREFRAME) {
            rlDisableBackfaceCulling();
            rlDisableDepthMask();
        }

        BeginMode3D(OTEditor.MainCamera);

        // UNLIT mode: render without lit shader (default raylib unlit)
        if (OTEditor.ViewMode == LightingMode::UNLIT) {
            // Default raylib shader is already unlit - just skip LitFogShader
        }

        DrawGrid(1000, 10.0f);

        CWDLProcess();
        WDLProcess();

        // OZONE world geometry
        OzoneLoader::Instance().Draw(OTEditor.MainCamera);

        // Sky zone rendering in editor
        {
            PawnSystem::Instance().UpdateSkyZone(
                OTEditor.MainCamera.position,
                (BoundingBox){{OTEditor.MainCamera.position.x-1,OTEditor.MainCamera.position.y-10,OTEditor.MainCamera.position.z-1},
                              {OTEditor.MainCamera.position.x+1,OTEditor.MainCamera.position.y,OTEditor.MainCamera.position.z+1}});
            if (PawnSystem::Instance().IsInSkyZone()) {
                rlDisableDepthMask();
                OzoneLoader::Instance().DrawZoneGeometry(
                    OTEditor.MainCamera,
                    PawnSystem::Instance().GetSkyZoneBounds());
                rlEnableDepthMask();
            }
        }

        // Pawn system ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â draw active pawns as billboards
        PawnSystem::Instance().DrawAll(OTEditor.MainCamera);
        PawnSystem::Instance().DrawEntities(OTEditor.MainCamera);

        // --- Placement visuals ---
        if (OmegaTechEditor.DrawModel)
        {
            float px = OmegaTechEditor.X, py = OmegaTechEditor.Y, pz = OmegaTechEditor.Z;
            float ps = OmegaTechEditor.S, pr = OmegaTechEditor.R;

            if (g_placeMode == PlaceMode::MODEL) {
                if (EMID > 0) {
                    LoadedModel* lm = WDLModels.GetModelByWDLId(EMID);
                    if (lm) DrawModelEx(lm->model, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE);
                } else if (EMID == -1) {
                    DrawBoundingBox((BoundingBox){{px,py,pz},{OmegaTechEditor.W,OmegaTechEditor.H,OmegaTechEditor.L}}, ORANGE);
                    DrawCubeWires({OmegaTechEditor.W,OmegaTechEditor.H,OmegaTechEditor.L}, ps, ps, ps, PINK);
                } else if (EMID == -2) {
                    DrawBoundingBox((BoundingBox){{px,py,pz},{OmegaTechEditor.W,OmegaTechEditor.H-5,OmegaTechEditor.L}}, ORANGE);
                    DrawCubeWires({OmegaTechEditor.W,OmegaTechEditor.H-5,OmegaTechEditor.L}, ps, ps, ps, PINK);
                }
            } else if (g_placeMode == PlaceMode::PICKUP) {
                // Draw pickup preview as a colored cube
                Color c = GREEN;
                switch (OmegaTechEditor.ActivePickupType) {
                    case EditorPickupType::HEALTH_VIAL:    c = RED;  break;
                    case EditorPickupType::MANA_VIAL:      c = BLUE; break;
                    case EditorPickupType::ENERGY_CRYSTAL: c = PURPLE; break;
                    case EditorPickupType::KEY:            c = GOLD; break;
                    case EditorPickupType::COIN:           c = YELLOW; break;
                    case EditorPickupType::POWERUP:        c = MAGENTA; break;
                }
                DrawCube({px, py + 0.5f, pz}, 0.4f, 0.6f, 0.4f, c);
                DrawCubeWires({px, py + 0.5f, pz}, 0.4f, 0.6f, 0.4f, (Color){c.r,c.g,c.b,80});
            } else if (g_placeMode == PlaceMode::NODE) {
                Color c = BLUE;
                switch (OmegaTechEditor.ActiveNodeType) {
                    case EditorNodeType::SPAWN: c = BLUE;    break;
                    case EditorNodeType::NPC:   c = MAGENTA; break;
                    case EditorNodeType::LIGHT: c = YELLOW;  break;
                }
                DrawCube({px, py, pz}, 0.5f, 0.2f, 0.5f, c);
                DrawCubeWires({px, py, pz}, 0.5f, 0.2f, 0.5f, (Color){c.r,c.g,c.b,80});
            }

            DrawCubeWires({px, py, pz}, ps, ps, ps, (EMID <= 100 || EMID == -3) ? PINK : ORANGE);
            DrawLine3D({px, py, pz}, {px + ps*3, py, pz}, RED);
            DrawLine3D({px, py, pz}, {px, py + ps*3, pz}, BLUE);
            DrawLine3D({px, py, pz}, {px, py, pz + ps*3}, GREEN);

            // Movement controls
            if (!IsMouseButtonDown(2)) {
                if (IsKeyPressed(KEY_U)) OmegaTechEditor.X -= 2.0f;
                if (IsKeyPressed(KEY_J)) OmegaTechEditor.X += 2.0f;
                if (IsKeyPressed(KEY_H)) OmegaTechEditor.Z += 2.0f;
                if (IsKeyPressed(KEY_K)) OmegaTechEditor.Z -= 2.0f;
                if (IsKeyPressed(KEY_Y)) OmegaTechEditor.Y -= 2.0f;
                if (IsKeyPressed(KEY_I)) OmegaTechEditor.Y += 2.0f;
            } else {
                if (IsKeyPressed(KEY_U)) OmegaTechEditor.W -= 2.0f;
                if (IsKeyPressed(KEY_J)) OmegaTechEditor.W += 2.0f;
                if (IsKeyPressed(KEY_H)) OmegaTechEditor.L += 2.0f;
                if (IsKeyPressed(KEY_K)) OmegaTechEditor.L -= 2.0f;
                if (IsKeyPressed(KEY_Y)) OmegaTechEditor.H -= 2.0f;
                if (IsKeyPressed(KEY_I)) OmegaTechEditor.H += 2.0f;
            }

            if (IsKeyPressed(KEY_O)) OmegaTechEditor.R += 90.0f;
            if (IsKeyPressed(KEY_L)) OmegaTechEditor.R -= 90.0f;
            if (IsKeyDown(KEY_T)) OmegaTechEditor.S += 0.5f;
            if (IsKeyDown(KEY_G)) OmegaTechEditor.S -= 0.5f;

            // Commit placement
            if (IsKeyPressed(KEY_ENTER) || DoubleClick)
            {
                wstring WDLCommand;
                if (g_placeMode == PlaceMode::MODEL) {
                    if (CollisionToggle) WDLCommand += L":C:";
                    if (EMID > 0) {
                        if (EMID != 100) {
                            WDLCommand += (EMID < 100 ? L"Model" : L"Script") + to_wstring(EMID < 100 ? EMID : EMID - 100) + L":";
                        } else {
                            WDLCommand += L"Collision:";
                        }
                        WDLCommand += to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                      to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                      to_wstring(OmegaTechEditor.R) + L":";
                    } else {
                        if (EMID == -1) {
                            WDLCommand += L"AdvCollision:" +
                                to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                to_wstring(OmegaTechEditor.R) + L":" + to_wstring(OmegaTechEditor.W) + L":" +
                                to_wstring(OmegaTechEditor.H) + L":" + to_wstring(OmegaTechEditor.L) + L":";
                        }
                        if (EMID == -2) {
                            WDLCommand += L"ClipBox:" +
                                to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                to_wstring(OmegaTechEditor.R) + L":" + to_wstring(OmegaTechEditor.W) + L":" +
                                to_wstring(OmegaTechEditor.H) + L":" + to_wstring(OmegaTechEditor.L) + L":";
                        }
                    }
                } else if (g_placeMode == PlaceMode::PICKUP) {
                    WDLCommand += L"Pickup:" + to_wstring((int)OmegaTechEditor.ActivePickupType) + L":" +
                        to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                        to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                        to_wstring(OmegaTechEditor.R) + L":";
                } else if (g_placeMode == PlaceMode::NODE) {
                    wstring nodePrefix;
                    switch (OmegaTechEditor.ActiveNodeType) {
                        case EditorNodeType::SPAWN: nodePrefix = L"Spawn:";   break;
                        case EditorNodeType::NPC:   nodePrefix = L"NPC:";     break;
                        case EditorNodeType::LIGHT: nodePrefix = L"Light:";   break;
                        case EditorNodeType::ZONE: nodePrefix = L"ZoneInfo:";   break;
                    }
                    // Node format: type:subtype:x:y:z:0:0:
                    WDLCommand += nodePrefix + to_wstring((int)OmegaTechEditor.ActiveNodeType) + L":" +
                        to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                        to_wstring(OmegaTechEditor.Z) + L":0:0:";
                }

                {
                    std::wstring wstr(WDLCommand);
                    std::string cmd(wstr.begin(), wstr.end());
                    EditorLog("Placed: %s", cmd.c_str());
                }
                OTEditor.WorldData += WDLCommand;
                OmegaTechEditor.DrawModel = false;
                CacheWDL();
            }

            if (IsMouseButtonDown(0)) {
                OmegaTechEditor.X += GetMouseDelta().x / 8;
                OmegaTechEditor.Y += GetMouseDelta().y / 8;
                OmegaTechEditor.Z -= (GetMouseWheelMove() * 2);
            }
            if (IsMouseButtonDown(1)) {
                OmegaTechEditor.W += GetMouseDelta().x / 8;
                OmegaTechEditor.H += GetMouseDelta().y / 8;
                OmegaTechEditor.L -= (GetMouseWheelMove() * 2);
            }
        }

        // Terrain-following camera
        {
            float gy = SampleHeightmapGroundY(OTEditor.MainCamera.position.x, OTEditor.MainCamera.position.z);
            if (gy > -50000.0f)
                OTEditor.MainCamera.position.y = gy + 2.0f;
        }

        EndMode3D();

        // Reset lighting mode state
        if (OTEditor.ViewMode == LightingMode::WIREFRAME) {
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
        }

        EndTextureMode();

        BeginDrawing();
        // Top toolbar with icons
        {
            int tw = GetScreenWidth();
            int tbH = 28;
            DrawRectangle(0, 0, tw, tbH, (Color){35, 35, 40, 220});
            DrawLine(0, tbH, tw, tbH, (Color){60, 60, 70, 255});

            auto& ico = EditorIcons::Instance();
            int bx = 4, iy = 4, is = 20;

            auto tBtn = [&](const char* icon, const char* label, int cmd) {
                int lw = (label ? (int)strlen(label) * 7 + 4 : 0);
                int bw = (icon ? 24 : 0) + lw + 4;
                Rectangle r = {(float)bx, 2, (float)bw, (float)tbH - 4};
                bool hover = CheckCollisionPointRec(GetMousePosition(), r);
                DrawRectangleRec(r, hover ? (Color){55,55,65,255} : (Color){45,45,50,255});
                if (icon && ico.Has(icon)) ico.Draw(icon, bx+2, iy, is, WHITE);
                if (label) DrawText(label, bx+(icon?26:4), 7, 12, LIGHTGRAY);
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    if (cmd==0) ToggleModelBrowser(); else if(cmd==1) ToggleSoundMgr();
                    else if(cmd==2) ToggleTextureMgr(); else if(cmd==3) TogglePawnMgr();
                    else if(cmd==4) ToggleScriptMgr(); else if(cmd==5) ToggleEnvPanel();
                    else if(cmd==6) TogglePickupPanel(); else if(cmd==7) ToggleNodePanel();
                    else if(cmd==8) ToggleHeightmapEditor();
                    else if(cmd==10) { FileNew(); } else if(cmd==11) { FileOpen(); }
                    else if(cmd==12) { FileSaveAs(); }
                    // CSG operations (toolbar icons 20-23)
                    else if (cmd >= 20 && cmd <= 23) { OmegaTechEditor.CSGOperation = cmd - 19; }
                    // Brush primitives (toolbar icons 30-34)
                    else if (cmd >= 30 && cmd <= 33) { g_editorPanels.actionCsgPlace = cmd - 30; g_placeMode = PlaceMode::MODEL; }
                    else if (cmd == 34) { ToggleHeightmapEditor(); }
                }
                bx += bw + 2;
            };

            tBtn(nullptr,"New",10); tBtn(nullptr,"Open",11); tBtn(nullptr,"Save",12);
            bx += 6;
            tBtn("BBGeneric","Mod",0); tBtn(nullptr,"Snd",1); tBtn(nullptr,"Tex",2);
            tBtn(nullptr,"Pawn",3); tBtn(nullptr,"Scr",4); bx += 6;

            for (int li=0;li<3;li++) {
                const char* labs[]={"Lit","Unlit","Wire"};
                int lw=38; Color lc=(int)OTEditor.ViewMode==li?(Color){70,90,120,255}:(Color){45,45,50,255};
                DrawRectangle(bx,2,lw,tbH-4,lc);
                DrawText(labs[li],bx+4,7,12,(int)OTEditor.ViewMode==li?WHITE:LIGHTGRAY);
                if(CheckCollisionPointRec(GetMousePosition(),{(float)bx,2,(float)lw,(float)tbH-4})&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    OTEditor.ViewMode=(LightingMode)li;
                bx+=lw+2;
            }
            bx+=6;
            tBtn("ModeAdd",nullptr,20); tBtn("ModeSubtract",nullptr,21);
            tBtn("ModeIntersect",nullptr,22); tBtn("ModeDeintersect",nullptr,23);
            bx+=4;
            tBtn("BBCube",nullptr,30); tBtn("BBCylinder",nullptr,31);
            tBtn("BBSphere",nullptr,32); tBtn("BBSheet",nullptr,33);
            tBtn("BBTerrain",nullptr,34); bx+=6;
            tBtn("AddVolume","Zone",5); tBtn("ModeCamera","Node",7);
            tBtn("PolyTexInfo","Pickup",6);
        }

        // Draw the 3D viewport render target (offset by sidebar width)
        int sbW = 200;
        DrawTexturePro(Target.texture, (Rectangle){0, 0, (float)Target.texture.width, -(float)Target.texture.height},
                       (Rectangle){(float)sbW, 28, (float)(GetScreenWidth() - sbW), (float)(GetScreenHeight() - 28)}, (Vector2){0,0}, 0, WHITE);
        DrawFPS(GetScreenWidth() - 60, 36);

        // Left sidebar overlay (always visible) ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â CSG brushes + tools
        {
            const int sbW = 200;
            Color bg = {25, 25, 30, 220};
            DrawRectangle(0, 28, sbW, GetScreenHeight() - 28, bg);
            DrawRectangleLines(0, 28, sbW, GetScreenHeight() - 28, (Color){60, 60, 70, 255});

            int y = 36;
            DrawText("CSG Brushes", 10, y, 16, WHITE); y += 24;

            // Primitive type display with icons
            {
                const char* primLabels[] = {"Box", "Cylinder", "Sphere", "Pyramid", "Plane"};
                const char* primIcons[] = {"BBCube", "BBCylinder", "BBSphere", "BBGeneric", "BBSheet"};
                int prim = g_editorPanels.actionCsgPlace >= 0 ? g_editorPanels.actionCsgPlace : 0;
                auto& ico = EditorIcons::Instance();
                for (int pi = 0; pi < 5; pi++) {
                    int px = 10 + pi * 36;
                    Color pc = (pi == prim) ? (Color){70, 90, 120, 255} : (Color){40, 40, 45, 255};
                    DrawRectangle(px, y, 32, 24, pc);
                    if (ico.Has(primIcons[pi])) ico.Draw(primIcons[pi], px + 6, y + 2, 16, WHITE);
                    else DrawText(primLabels[pi], px + 2, y + 6, 10, LIGHTGRAY);
                    if (CheckCollisionPointRec(GetMousePosition(), {(float)px, (float)y, 32, 24}) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        { g_editorPanels.actionCsgPlace = pi; g_placeMode = PlaceMode::MODEL; }
                }
                y += 28;
            }

            // CSG operation with icons
            {
                const char* csgLabels[] = {"SOLID", "ADD", "SUB", "INTERSECT", "DE_RESC"};
                const char* csgIcons[] = {nullptr, "ModeAdd", "ModeSubtract", "ModeIntersect", "ModeDeintersect"};
                int op = OmegaTechEditor.CSGOperation;
                auto& ico = EditorIcons::Instance();
                for (int oi = 0; oi < 5; oi++) {
                    int px = 10 + oi * 36;
                    Color oc = (oi == op) ? (Color){70, 90, 120, 255} : (Color){40, 40, 45, 255};
                    DrawRectangle(px, y, 32, 24, oc);
                    if (oi > 0 && ico.Has(csgIcons[oi])) ico.Draw(csgIcons[oi], px + 6, y + 2, 16, WHITE);
                    else DrawText(csgLabels[oi], px + 2, y + 6, 10, LIGHTGRAY);
                    if (CheckCollisionPointRec(GetMousePosition(), {(float)px, (float)y, 32, 24}) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        { OmegaTechEditor.CSGOperation = oi; }
                }
                y += 28;
            }

            // Placement info
            DrawText(TextFormat("Pos: %.1f %.1f %.1f", OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z),
                     10, y, 12, SKYBLUE); y += 16;
            DrawText(TextFormat("Size: %.1f x %.1f x %.1f", OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L),
                     10, y, 12, SKYBLUE); y += 16;
            DrawText(TextFormat("Rot: %.0f  Scale: %.1f", OmegaTechEditor.R, OmegaTechEditor.S),
                     10, y, 12, SKYBLUE); y += 22;

            // Collision volume stats
            int volCount = OzoneLoader::Instance().GetCollisionVolumes().size();
            int chunkCount = OzoneLoader::Instance().GetChunkManager().CellCount();
            DrawText(TextFormat("Collision: %d vols", volCount), 10, y, 12, volCount > 500 ? RED : DARKGREEN); y += 14;
            DrawText(TextFormat("Chunks: %d", chunkCount), 10, y, 12, DARKGREEN); y += 22;

            // Mode display
            DrawText(TextFormat("Mode: %s", g_placeMode == PlaceMode::MODEL ? "MODEL" :
                   g_placeMode == PlaceMode::PICKUP ? "PICKUP" :
                   g_placeMode == PlaceMode::NODE ? "NODE" : "ENV"), 10, y, 13, WHITE); y += 18;
            DrawText("1=Model 2=Pickup 3=Node 4=Env", 10, y, 10, DARKGRAY); y += 18;

            // Heightmap button
            DrawText("---", 10, y, 12, GRAY); y += 18;
            DrawText("[H] Heightmap Editor", 10, y, 12, YELLOW); y += 16;
            DrawText("[F5] Model Browser  [F6] Sound", 10, y, 10, DARKGRAY); y += 13;
            DrawText("[F7] Textures  [F8] Pawn Manager", 10, y, 10, DARKGRAY); y += 13;
            DrawText("[F12] Zone Properties", 10, y, 10, DARKGRAY); y += 16;

            // Camera info
            DrawText("---", 10, y, 12, GRAY); y += 18;
            DrawText("Camera:", 10, y, 13, WHITE); y += 16;
            DrawText(TextFormat("%.1f %.1f %.1f", OTEditor.MainCamera.position.x,
                     OTEditor.MainCamera.position.y, OTEditor.MainCamera.position.z),
                     10, y, 11, PURPLE); y += 14;
            DrawText("MMB=Pan S+MMB=Y Alt+MMB=Orbit", 10, y, 9, DARKGRAY); y += 12;
            DrawText("Scroll=Dolly Home=Reset", 10, y, 9, DARKGRAY);
        }

        EndDrawing();

    #ifdef _WIN32
    // --- Model preview render-to-texture (for Win32 Model Browser dialog) ---
        if (g_editorPanels.selectedModel >= 0 &&
            g_editorPanels.selectedModel < (int)g_editorPanels.modelEntries.size() &&
            g_editorPanels.selectedModel != g_lastPreviewSel) {
            g_lastPreviewSel = g_editorPanels.selectedModel;
            g_previewNeedsUpdate = true;
        }
        if (g_previewNeedsUpdate && g_lastPreviewSel >= 0 &&
            g_lastPreviewSel < (int)g_editorPanels.modelEntries.size()) {
            auto& entry = g_editorPanels.modelEntries[g_lastPreviewSel];
            if (!entry.loaded) {
                Model mdl = LoadModelWithFallback(entry.path.c_str());
                std::string texPath = entry.path;
                texPath.replace(texPath.end() - 4, texPath.end(), "_texture.png");
                std::string texPath2 = entry.path;
                texPath2.replace(texPath2.end() - 4, texPath2.end(), ".png");
                Texture2D tex = {0};
                if (fs::exists(texPath)) tex = LoadTextureWithFallback(texPath.c_str());
                else if (fs::exists(texPath2)) tex = LoadTextureWithFallback(texPath2.c_str());
                else tex = LoadTextureWithFallback(entry.path.c_str());
                if (tex.id > 0) mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
                if (mdl.meshes != nullptr) {
                    entry.triangles = mdl.meshes[0].triangleCount;
                    entry.vertices = mdl.meshes[0].vertexCount;
                }
                UnloadModel(mdl);
                if (tex.id > 0) UnloadTexture(tex);
                entry.loaded = true;
            }
            Camera3D prevCam = {0};
            prevCam.position = {5.0f, 4.0f, 5.0f};
            prevCam.target = {0, 0, 0};
            prevCam.up = {0, 1, 0};
            prevCam.fovy = 45.0f;
            prevCam.projection = CAMERA_PERSPECTIVE;

            BeginTextureMode(g_previewRT);
            ClearBackground((Color){40, 40, 50, 255});
            BeginMode3D(prevCam);
            DrawGrid(10, 1.0f);
            Model mdl = LoadModelWithFallback(entry.path.c_str());
            std::string texPath = entry.path;
            texPath.replace(texPath.end() - 4, texPath.end(), "_texture.png");
            std::string texPath2 = entry.path;
            texPath2.replace(texPath2.end() - 4, texPath2.end(), ".png");
            Texture2D tex = {0};
            if (fs::exists(texPath)) tex = LoadTextureWithFallback(texPath.c_str());
            else if (fs::exists(texPath2)) tex = LoadTextureWithFallback(texPath2.c_str());
            else tex = LoadTextureWithFallback(entry.path.c_str());
            if (tex.id > 0) mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
            if (mdl.meshes != nullptr) {
                rlDisableBackfaceCulling();
                DrawModel(mdl, {0, 0, 0}, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            }
            EndMode3D();
            EndTextureMode();
            UnloadModel(mdl);
            if (tex.id > 0) UnloadTexture(tex);

            Image img = LoadImageFromTexture(g_previewRT.texture);
            ImageFlipVertical(&img);
            unsigned char* px = (unsigned char*)img.data;
            for (int i = 0; i < img.width * img.height; i++) {
                unsigned char tmp = px[i*4];
                px[i*4] = px[i*4+2];
                px[i*4+2] = tmp;
            }
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = img.width;
            bmi.bmiHeader.biHeight = -img.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            HDC hdc = GetDC((HWND)GetWindowHandle());
            void* bits = nullptr;
            HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
            if (bits) memcpy(bits, img.data, img.width * img.height * 4);
            ReleaseDC((HWND)GetWindowHandle(), hdc);
            UpdateModelPreview(hBmp, img.width, img.height);
            UnloadImage(img);
            g_previewNeedsUpdate = false;
        }
#endif

        // Handle action flags from Win32 dialogs
        if (g_editorPanels.actionPickupType >= 0) {
            g_placeMode = PlaceMode::PICKUP;
            OmegaTechEditor.ActivePickupType = (EditorPickupType)g_editorPanels.actionPickupType;
            OmegaTechEditor.DrawModel = true;
            OmegaTechEditor.X = OTEditor.MainCamera.position.x;
            OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
            OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
            OmegaTechEditor.R = 1;
            g_editorPanels.actionPickupType = -1;
        }
        if (g_editorPanels.actionNodeType >= 0) {
            g_placeMode = PlaceMode::NODE;
            OmegaTechEditor.ActiveNodeType = (EditorNodeType)g_editorPanels.actionNodeType;
            OmegaTechEditor.DrawModel = true;
            OmegaTechEditor.X = OTEditor.MainCamera.position.x;
            OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
            OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
            OmegaTechEditor.R = 1;
            g_editorPanels.actionNodeType = -1;
        }
        if (g_editorPanels.actionPlaceModel >= 0) {
            g_placeMode = PlaceMode::MODEL;
            OmegaTechEditor.DrawModel = true;
            OmegaTechEditor.X = OTEditor.MainCamera.position.x;
            OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
            OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
            OmegaTechEditor.R = 1;
            EMID = 0; // 0 = user-selected obj
            g_editorPanels.actionPlaceModel = -1;
        }
        if (g_editorPanels.actionCsgPlace >= 0) {
            g_placeMode = PlaceMode::MODEL;
            OmegaTechEditor.CSGOperation = g_csgOpFromSidebar;
            CollisionToggle = g_csgCollisionFromSidebar;
            OmegaTechEditor.DrawModel = true;
            int primType = g_editorPanels.actionCsgPlace;
            // Map primitive type to EMID
            // 0=box, 1=cyl, 2=sph, 3=pyr, 4=pln ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â use 200+ offset for OZONE primitives
            EMID = 200 + primType;
            g_editorPanels.actionCsgPlace = -1;
        }
        if (g_editorPanels.actionRefreshBrowser) {
            ScanModelBrowserFiles();
            g_editorPanels.actionRefreshBrowser = false;
        }
        // Heightmap generate handler
        if (g_editorPanels.actionGenerateHeightmap) {
            auto& img = g_editorPanels.actionHeightmapImage;
            auto& tex = g_editorPanels.actionHeightmapTexture;
            if (!img.empty()) {
                std::vector<float> args = {
                    g_editorPanels.actionHmPosX, g_editorPanels.actionHmPosY, g_editorPanels.actionHmPosZ,
                    g_editorPanels.actionHmScale,
                    g_editorPanels.actionHmSx, g_editorPanels.actionHmSy, g_editorPanels.actionHmSz
                };
                OzoneLoader::Instance().BuildHeightmap(img, tex, args);
                EditorLog("Heightmap generated from %s", img.c_str());
            }
            g_editorPanels.actionGenerateHeightmap = false;
        }
        // Light Properties apply handler — stores properties for export
        if (g_editorPanels.actionApplyLight) {
            EditorLog("Light properties set: color=(%.0f,%.0f,%.0f) intensity=%.1f radius=%.0f type=%d effect=%d",
                g_editorPanels.lightColorR, g_editorPanels.lightColorG, g_editorPanels.lightColorB,
                g_editorPanels.lightIntensity, g_editorPanels.lightRadius,
                g_editorPanels.lightType, g_editorPanels.lightEffect);
            g_editorPanels.actionApplyLight = false;
        }

        if (g_editorPanels.actionSpawnPawn >= 0) {
            int idx = g_editorPanels.actionSpawnPawn;
            if (idx >= 0 && idx < GetPawnCount()) {
                Vector3 pos = OTEditor.MainCamera.position;
                PawnSystem::Instance().Spawn(pos, GetPawnName(idx));
            }
            g_editorPanels.actionSpawnPawn = -1;
        }

        // Mode switching
        if (IsKeyPressed(KEY_ONE))   g_placeMode = PlaceMode::MODEL;
        if (IsKeyPressed(KEY_TWO))   g_placeMode = PlaceMode::PICKUP;
        if (IsKeyPressed(KEY_THREE)) g_placeMode = PlaceMode::NODE;
        if (IsKeyPressed(KEY_FOUR))  { g_placeMode = PlaceMode::ENV; ShowEnvPanel(!g_editorPanels.showEnvPanel); }

        // Primitive type cycling (1-5 for box/cyl/sph/pyr/pln, only in MODEL mode)
        if (g_placeMode == PlaceMode::MODEL) {
            int prim = g_editorPanels.actionCsgPlace;
            if (prim < 0 || prim > 4) prim = 0;
            if (IsKeyPressed(KEY_FIVE))  { prim = (prim + 1) % 5; g_editorPanels.actionCsgPlace = prim; }
        }

        // CSG operation cycling (G key)
        if (IsKeyPressed(KEY_G) && g_placeMode == PlaceMode::MODEL) {
            OmegaTechEditor.CSGOperation = (OmegaTechEditor.CSGOperation + 1) % 5;
        }

        // Heightmap editor toggle (H key)
        if (IsKeyPressed(KEY_H)) ToggleHeightmapEditor();

        // Panel keyboard shortcuts (F5-F12 replace old top menu bar)
        if (IsKeyPressed(KEY_F5))  ToggleModelBrowser();
        if (IsKeyPressed(KEY_F6))  ToggleSoundMgr();
        if (IsKeyPressed(KEY_F7))  ToggleTextureMgr();
        if (IsKeyPressed(KEY_F8))  TogglePawnMgr();
        if (IsKeyPressed(KEY_F9))  ToggleScriptMgr();
        if (IsKeyPressed(KEY_F10)) TogglePickupPanel();
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();  // supersedes old F11 handling
        if (IsKeyPressed(KEY_F12)) ToggleEnvPanel();

        // Camera shortcuts
        if (IsKeyPressed(KEY_HOME)) ResetCamera();
        if (IsKeyPressed(KEY_PAGE_UP)) CamUp();
        if (IsKeyPressed(KEY_PAGE_DOWN)) CamDown();

    }

    // Cleanup
    EditorLog("=== AngelEd shutting down ===");
    EditorIcons::Instance().Unload();
    if (g_editorLog) fclose(g_editorLog);
    StopSoundPreview();
    OzoneLoader::Instance().Unload();
    UnloadRenderTexture(g_previewRT);
#ifdef _WIN32
    DestroyAllEditorWindows();
#endif
    CloseAudioDevice();
    CloseWindow();
}

// =====================================================================
// Top menu bar with dropdowns
// =====================================================================
// All panel managers are implemented as Win32 native dialogs
// in Win32Dialogs.cpp. Panels toggled via keyboard shortcuts (F5-F12).
