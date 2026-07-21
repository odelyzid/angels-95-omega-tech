#include "../../Source/WindowsCompat.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Editor.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "Win32Dialogs.hpp"
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

// --- Top menu bar state ---
static int g_menuActive = -1;      // which dropdown is open (-1 = none)

struct MenuItem { const char* label; void (*handler)(); };
struct MenuDef { const char* label; std::vector<MenuItem> items; };

static void FileNew();
static void FileOpen();
static void FileSaveAs();
static void ClearScene();
static bool LoadWorldDocument(const fs::path& path);
static bool SaveWorldDocument(const fs::path& path);

// --- Menu handler helpers ---
static void ActionUndo() {
    if (!g_documentPath.empty() && g_documentPath.extension() == ".wdl")
        LoadWorldDocument(g_documentPath);
    g_menuActive = -1;
}
static void ActionSave() {
    if (!g_documentPath.empty() && g_documentPath.extension() == ".wdl")
        SaveWorldDocument(g_documentPath);
    else FileSaveAs();
    g_menuActive = -1;
}
static void ActionResetCam() { OTEditor.MainCamera.position = {0, 10, 0}; g_menuActive = -1; }
static void ActionCamUp()    { OTEditor.MainCamera.position.y += 2;   g_menuActive = -1; }
static void ActionCamDown()  { OTEditor.MainCamera.position.y -= 2; g_menuActive = -1; }

static void ToggleSoundMgr()    { ShowSoundManager(!g_editorPanels.showSoundMgr);     g_menuActive = -1; }
static void ToggleTextureMgr()  { ShowTextureManager(!g_editorPanels.showTextureMgr); g_menuActive = -1; }
static void TogglePawnMgr()     { ShowPawnManager(!g_editorPanels.showPawnMgr);       g_menuActive = -1; }
static void ToggleScriptMgr()   { ShowScriptManager(!g_editorPanels.showScriptMgr);   g_menuActive = -1; }
static void ToggleModelBrowser(){ ShowModelBrowser(!g_editorPanels.showModelBrowser); g_menuActive = -1; }
static void TogglePickupPanel() { ShowPickupPanel(!g_editorPanels.showPickupPanel);   g_menuActive = -1; }
static void ToggleNodePanel()   { ShowNodePanel(!g_editorPanels.showNodePanel);       g_menuActive = -1; }
static void ToggleEnvPanel()    { ShowEnvPanel(!g_editorPanels.showEnvPanel);         g_placeMode = PlaceMode::ENV; g_menuActive = -1; }

static void PlaceBoxCollision() {
    EMID = 100; g_placeMode = PlaceMode::MODEL;
    OmegaTechEditor.DrawModel = true;
    OmegaTechEditor.X = OTEditor.MainCamera.position.x;
    OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
    OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
    OmegaTechEditor.R = 1;
    g_menuActive = -1;
}
static void PlaceAdvCollision() {
    EMID = -1; g_placeMode = PlaceMode::MODEL;
    OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
    OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
    OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
    OmegaTechEditor.X = OTEditor.MainCamera.position.x;
    OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
    OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
    OmegaTechEditor.R = 1;
    OmegaTechEditor.DrawModel = true;
    g_menuActive = -1;
}
static void PlaceHeightClipBox() {
    EMID = -2; g_placeMode = PlaceMode::MODEL;
    OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
    OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
    OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
    OmegaTechEditor.X = OTEditor.MainCamera.position.x;
    OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
    OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
    OmegaTechEditor.R = 1;
    OmegaTechEditor.DrawModel = true;
    g_menuActive = -1;
}
static void ToggleCollision() {
    CollisionToggle = !CollisionToggle;
    g_menuActive = -1;
}

static std::vector<MenuDef> g_menus = {
    {"File", {
        {"New", FileNew},
        {"Open...", FileOpen},
        {"Save As...", FileSaveAs},
        {"---", nullptr},
        {"Undo", ActionUndo},
        {"Save", ActionSave}
    }},
    {"Models", {
        // Items rebuilt dynamically via RebuildModelsMenu()
    }},
    {"Pickups", {
        {"Pickup Panel...", TogglePickupPanel}
    }},
    {"Nodes", {
        {"Node Panel...", ToggleNodePanel}
    }},
    {"View", {
        {"Reset Camera", ActionResetCam},
        {"Camera Up",    ActionCamUp},
        {"Camera Down",  ActionCamDown},
        {"---", nullptr},
        {"Environment Settings...", ToggleEnvPanel},
        {"Sound Manager...",        ToggleSoundMgr},
        {"Texture Manager...",      ToggleTextureMgr},
        {"Script Manager...",       ToggleScriptMgr}
    }},
    {"Pawn", {
        {"Pawn Manager...", TogglePawnMgr}
    }}
};

static void ToggleHeightmapEditor() { ShowHeightmapEditor(!g_editorPanels.showHeightmapEditor); g_menuActive = -1; }

// Rebuild the Models submenu to show loaded model count
static void RebuildModelsMenu() {
    if (g_menus.size() < 2) return;
    MenuDef& modelsMenu = g_menus[1];
    modelsMenu.items.clear();
    modelsMenu.items.push_back({"Model Browser...", ToggleModelBrowser});
    int n = WDLModels.GetModelCount();
    if (n > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d models loaded)", n);
        modelsMenu.items.push_back({strdup(buf), nullptr});
        modelsMenu.items.push_back({"---", nullptr});
    }
    modelsMenu.items.push_back({"Box Collision",    PlaceBoxCollision});
    modelsMenu.items.push_back({"Adv Collision",    PlaceAdvCollision});
    modelsMenu.items.push_back({"Height Clip Box",  PlaceHeightClipBox});
    modelsMenu.items.push_back({"Toggle Collision", ToggleCollision});
    modelsMenu.items.push_back({"---", nullptr});
    modelsMenu.items.push_back({"Heightmap Editor...", ToggleHeightmapEditor});
}

// Forward declarations for overlay UI functions
static void DrawMenuBar();

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
            } catch (...) {}
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
    RebuildModelsMenu();

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
    g_menuActive = -1;
}

static void FileOpen() {
    std::string path;
    if (ChooseOpenWorldFile(path)) g_pendingOpenPath = fs::path(path);
    g_menuActive = -1;
}

static void FileSaveAs() {
    std::string path;
    if (ChooseSaveWorldFile(path)) SaveWorldDocument(fs::path(path));
    g_menuActive = -1;
}

static bool ApplyTextureToModel(int target, const char* path) {
    LoadedModel* lm = WDLModels.GetModelByWDLId(target);
    if (!lm || lm->model.materialCount == 0) return false;
    Texture2D texture = LoadTexture(path);
    if (texture.id == 0) return false;
    if (lm->texture.id > 0) UnloadTexture(lm->texture);
    lm->texture = texture;
    SetMaterialTexture(&lm->model.materials[0], MATERIAL_MAP_DIFFUSE, texture);
    return true;
}

int main(int argc, char **argv){
    EditorLog("=== AngelEd starting ===");
    SetWindowState(FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "AngelEd");
    InitAudioDevice();
    SetTargetFPS(60);
    GuiLoadStyleDark();

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
    RebuildModelsMenu();
    CacheWDL();

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

    // Create model preview render texture
    g_previewRT = LoadRenderTexture(256, 256);

    DisableCursor();

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

        for (int i = 0; i <= 7; i++){
            if(!IsMouseButtonDown(0) && !IsMouseButtonDown(1))
                UpdateCamera(&OTEditor.MainCamera, CAMERA_FIRST_PERSON);
        }
        
        // Apply EnvPanel fog/ambient settings
        {
            EnvSettings env = GetEnvSettings();
            if (env.applyFog) {
                // Update lit fog shader uniforms
                if (OTEditor.LitFogShader.id > 0) {
                    float fogColor[3] = {(float)env.fogR / 255.0f, (float)env.fogG / 255.0f, (float)env.fogB / 255.0f};
                    float fogDensity = env.fogDensity;
                    float fogIntensity = 1.0f;

                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogColorLoc, fogColor, SHADER_UNIFORM_VEC3);
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogDensityLoc, &fogDensity, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.FogIntensityLoc, &fogIntensity, SHADER_UNIFORM_FLOAT);
                }
                OTEditor.FogColor = (Color){ (unsigned char)env.fogR, (unsigned char)env.fogG, (unsigned char)env.fogB, 255 };
                OTEditor.FogDensity = env.fogDensity;
            }
            if (env.applyAmbient) {
                OTEditor.AmbientColor = (Color){ (unsigned char)env.ambR, (unsigned char)env.ambG, (unsigned char)env.ambB, 255 };
                OTEditor.AmbientIntensity = env.ambIntensity;
                if (OTEditor.AmbientLoc >= 0 && OTEditor.LitFogShader.id > 0) {
                    float ambient[4] = {(float)env.ambR / 255.0f * env.ambIntensity,
                                        (float)env.ambG / 255.0f * env.ambIntensity,
                                        (float)env.ambB / 255.0f * env.ambIntensity,
                                        1.0f};
                    SetShaderValue(OTEditor.LitFogShader, OTEditor.AmbientLoc, ambient, SHADER_UNIFORM_VEC4);
                }
            }
            ClearEnvApplyFlags();
        }

        ClearBackground(BLACK);

        BeginMode3D(OTEditor.MainCamera);

        DrawGrid(1000, 10.0f);

        CWDLProcess();
        WDLProcess();

        // OZONE world geometry
        OzoneLoader::Instance().Draw(OTEditor.MainCamera);

        // Pawn system — draw active pawns as billboards
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
        EndTextureMode();

        BeginDrawing();
        DrawTexturePro(Target.texture, (Rectangle){0, 0, (float)Target.texture.width, -(float)Target.texture.height},
                       (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()}, (Vector2){0,0}, 0, WHITE);
        DrawFPS(10, 34);
        DrawText(TextFormat("%.1f, %.1f, %.1f", OTEditor.MainCamera.position.x, OTEditor.MainCamera.position.y, OTEditor.MainCamera.position.z), 10, 52, 15, PURPLE);
        DrawText(TextFormat("Mode: %s  (1=MODEL 2=PICKUP 3=NODE 4=ENV)",
                g_placeMode == PlaceMode::MODEL ? "MODEL" :
                g_placeMode == PlaceMode::PICKUP ? "PICKUP" :
                g_placeMode == PlaceMode::NODE ? "NODE" : "ENV"), 10, 72, 15, WHITE);

        // CSG operation display
        {
            static const char* csgLabels[] = {"SOLID", "ADD", "SUB", "INTERSECT", "DE_RESC"};
            int op = OmegaTechEditor.CSGOperation;
            if (op < 0 || op > 4) op = 0;
            DrawText(TextFormat("CSG: %s", csgLabels[op]), 10, 92, 15, ORANGE);
        }

        // Top menu bar (always visible)
        DrawMenuBar();

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
                Model mdl = LoadModel(entry.path.c_str());
                std::string texPath = entry.path;
                texPath.replace(texPath.end() - 4, texPath.end(), "_texture.png");
                std::string texPath2 = entry.path;
                texPath2.replace(texPath2.end() - 4, texPath2.end(), ".png");
                Texture2D tex = {0};
                if (fs::exists(texPath)) tex = LoadTexture(texPath.c_str());
                else if (fs::exists(texPath2)) tex = LoadTexture(texPath2.c_str());
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
            Model mdl = LoadModel(entry.path.c_str());
            std::string texPath = entry.path;
            texPath.replace(texPath.end() - 4, texPath.end(), "_texture.png");
            std::string texPath2 = entry.path;
            texPath2.replace(texPath2.end() - 4, texPath2.end(), ".png");
            Texture2D tex = {0};
            if (fs::exists(texPath)) tex = LoadTexture(texPath.c_str());
            else if (fs::exists(texPath2)) tex = LoadTexture(texPath2.c_str());
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
        if (g_editorPanels.actionRefreshBrowser) {
            ScanModelBrowserFiles();
            g_editorPanels.actionRefreshBrowser = false;
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

        // CSG operation cycling (G key)
        if (IsKeyPressed(KEY_G) && g_placeMode == PlaceMode::MODEL) {
            OmegaTechEditor.CSGOperation = (OmegaTechEditor.CSGOperation + 1) % 5;
        }

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

    }

    // Cleanup
    EditorLog("=== AngelEd shutting down ===");
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
static void DrawMenuBar() {
    int sw = GetScreenWidth();
    const int BAR_H = 24;
    const int ITEM_W = 80;

    // Draw bar background
    DrawRectangle(0, 0, sw, BAR_H, (Color){40, 40, 40, 255});
    DrawLine(0, BAR_H, sw, BAR_H, (Color){80, 80, 80, 255});

    int cx = 4;
    for (int m = 0; m < (int)g_menus.size(); m++) {
        // Menu label button
        Rectangle r = {(float)cx, 2, (float)ITEM_W, (float)BAR_H - 4};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), r);
        Color bg = (m == g_menuActive) ? (Color){60, 60, 80, 255}
                  : hovered ? (Color){55, 55, 55, 255}
                  : (Color){40, 40, 40, 255};
        DrawRectangleRec(r, bg);
        DrawText(g_menus[m].label, cx + 8, 5, 12, WHITE);

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered) {
            g_menuActive = (g_menuActive == m) ? -1 : m;
        }

        // Draw dropdown if active
        if (m == g_menuActive) {
            int dy = BAR_H;
            for (auto& item : g_menus[m].items) {
                Rectangle dr = {(float)cx, (float)dy, 180, 22};
                bool dh = CheckCollisionPointRec(GetMousePosition(), dr);
                if (item.label == std::string("---")) {
                    DrawRectangleRec(dr, (Color){50, 50, 50, 255});
                    DrawLine((int)cx + 4, (int)dy + 11, (int)cx + 176, (int)dy + 11, (Color){100, 100, 100, 255});
                } else {
                    DrawRectangleRec(dr, dh ? (Color){70, 70, 90, 255} : (Color){50, 50, 50, 255});
                    DrawText(item.label, (int)cx + 6, dy + 4, 12, LIGHTGRAY);
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dh) {
                        if (item.handler) item.handler();
                    }
                }
                dy += 22;
            }
            // Outline the dropdown
            DrawRectangleLines(cx, BAR_H, 180, (int)g_menus[m].items.size() * 22, (Color){100, 100, 120, 255});
        }
        cx += ITEM_W + 2;
    }

    // Click outside closes menu
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && g_menuActive >= 0) {
        bool inside = false;
        for (int m = 0; m < (int)g_menus.size(); m++)
            if (CheckCollisionPointRec(GetMousePosition(), {4 + (float)m*(ITEM_W+2), 2, (float)ITEM_W, (float)BAR_H-4}))
                inside = true;
        if (!inside) {
            // Also check inside open dropdown:
            if (g_menuActive >= 0 && g_menuActive < (int)g_menus.size()) {
                Rectangle dr = {4 + (float)g_menuActive*(ITEM_W+2), (float)BAR_H, 180, (float)g_menus[g_menuActive].items.size() * 22};
                if (CheckCollisionPointRec(GetMousePosition(), dr))
                    inside = true;
            }
            if (!inside) g_menuActive = -1;
        }
    }
}

// (Manager panel windows are now implemented as Win32 native dialogs
//  in Win32Dialogs.cpp. The raygui versions have been removed.)
// In-viewport overlay has been replaced by top menu bar + Win32 dialogs.
