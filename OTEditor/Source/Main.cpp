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
#include "../../Source/oz_ozone_loader.h"
#include "../../Source/oz_pawn_system.h"
#include "../../Source/PackageAssetLoader.hpp"
#include "../../Source/Server/WDLParser.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

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

static void ToggleSoundMgr()    { ShowSoundManager(!g_editorPanels.showSoundMgr);     g_menuActive = -1; }
static void ToggleTextureMgr()  { ShowTextureManager(!g_editorPanels.showTextureMgr); g_menuActive = -1; }
static void TogglePawnMgr()     { ShowPawnManager(!g_editorPanels.showPawnMgr);       g_menuActive = -1; }
static void ToggleScriptMgr()   { ShowScriptManager(!g_editorPanels.showScriptMgr);   g_menuActive = -1; }
static void ToggleModelBrowser(){ ShowModelBrowser(!g_editorPanels.showModelBrowser); g_menuActive = -1; }

static std::vector<MenuDef> g_menus = {
    {"File",      {{"New", FileNew}, {"Open...", FileOpen}, {"Save As...", FileSaveAs}}},
    {"Models",    {{"Model Browser...", ToggleModelBrowser}}},
    {"Sound",     {{"Sound Manager...", ToggleSoundMgr}}},
    {"Texture",   {{"Texture Manager...", ToggleTextureMgr}}},
    {"Pawn",      {{"Pawn Manager...", TogglePawnMgr}}},
    {"Script",    {{"Script Manager...", ToggleScriptMgr}}}
};

// Forward declarations for overlay UI functions
static void DrawOverlay();
static void RenderPreview();
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
    ClearScene();
    SetWorldDirectory(path.parent_path());
    g_documentPath = path;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (extension == ".ozone") return OzoneLoader::Instance().LoadFile(path.string().c_str());
    if (extension != ".wdl") return false;

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
    Model* model = nullptr;
    Texture2D* owned = nullptr;
#define MODEL_TEXTURE_CASE(N) case N: model = &WDLModels.Model##N; owned = &WDLModels.Model##N##Texture; break
    switch (target) {
        MODEL_TEXTURE_CASE(1); MODEL_TEXTURE_CASE(2); MODEL_TEXTURE_CASE(3); MODEL_TEXTURE_CASE(4);
        MODEL_TEXTURE_CASE(5); MODEL_TEXTURE_CASE(6); MODEL_TEXTURE_CASE(7); MODEL_TEXTURE_CASE(8);
        MODEL_TEXTURE_CASE(9); MODEL_TEXTURE_CASE(10); MODEL_TEXTURE_CASE(11); MODEL_TEXTURE_CASE(12);
        MODEL_TEXTURE_CASE(13); MODEL_TEXTURE_CASE(14); MODEL_TEXTURE_CASE(15); MODEL_TEXTURE_CASE(16);
        MODEL_TEXTURE_CASE(17); MODEL_TEXTURE_CASE(18); MODEL_TEXTURE_CASE(19); MODEL_TEXTURE_CASE(20);
        default: return false;
    }
#undef MODEL_TEXTURE_CASE
    Texture2D texture = LoadTexture(path);
    if (texture.id == 0 || !model || model->materialCount == 0) return false;
    if (owned->id > 0) UnloadTexture(*owned);
    *owned = texture;
    SetMaterialTexture(&model->materials[0], MATERIAL_MAP_DIFFUSE, texture);
    return true;
}

int main(int argc, char **argv){
    SetWindowState(FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "oz_editor");
    InitAudioDevice();
    SetTargetFPS(60);
    GuiLoadStyleDark();

    g_documentPath = argc > 1 && argv[1] ? fs::path(argv[1]) : fs::path("../GameData/World.wdl");
    SetWorldDirectory(g_documentPath.parent_path());

    // Load INI config
    g_config.Load("System/oz_editor.ini");

    // Initialize package-based asset loading
    PackageAssetLoader::Instance().Init();

#ifdef _WIN32
    CreateAllEditorWindows(GetModuleHandle(NULL), GetWindowHandle());
#endif

    // Load editor world
    Init();
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
            if(!OverlayEnabled && !IsMouseButtonDown(0) && !IsMouseButtonDown(1))
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
                if (EMID > 0 && EMID <= 20) {
                    switch (EMID) {
                        case 1:  DrawModelEx(WDLModels.Model1,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 2:  DrawModelEx(WDLModels.Model2,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 3:  DrawModelEx(WDLModels.Model3,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 4:  DrawModelEx(WDLModels.Model4,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 5:  DrawModelEx(WDLModels.Model5,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 6:  DrawModelEx(WDLModels.Model6,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 7:  DrawModelEx(WDLModels.Model7,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 8:  DrawModelEx(WDLModels.Model8,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 9:  DrawModelEx(WDLModels.Model9,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 10: DrawModelEx(WDLModels.Model10, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 11: DrawModelEx(WDLModels.Model11, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 12: DrawModelEx(WDLModels.Model12, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 13: DrawModelEx(WDLModels.Model13, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 14: DrawModelEx(WDLModels.Model14, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 15: DrawModelEx(WDLModels.Model15, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 16: DrawModelEx(WDLModels.Model16, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 17: DrawModelEx(WDLModels.Model17, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 18: DrawModelEx(WDLModels.Model18, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 19: DrawModelEx(WDLModels.Model19, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 20: DrawModelEx(WDLModels.Model20, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                    }
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

                wcout << WDLCommand << "\n";
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

        // Top menu bar (always visible)
        DrawMenuBar();

        if (OverlayEnabled) {
            DrawOverlay();
            if (!IsMouseButtonReleased(0) && GetCollision(0, 0, 144, 720, GetMouseX(), GetMouseY(), 5, 5))
                RenderPreview();
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

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (IsKeyPressed(KEY_TAB)) {
            OverlayEnabled = !OverlayEnabled;
            if (OverlayEnabled) EnableCursor(); else DisableCursor();
        }
    }

    // Cleanup
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
                DrawRectangleRec(dr, dh ? (Color){70, 70, 90, 255} : (Color){50, 50, 50, 255});
                DrawText(item.label, (int)cx + 6, dy + 4, 12, LIGHTGRAY);

                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dh) {
                    if (item.handler) item.handler();
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

// Help text boxes
bool TextmultiBox005EditMode = false;
char TextmultiBox005Text[128] = "Tab - Opens Overlay  1/2/3/4 - Switch Mode  Shift - Places Last Item";
bool TextmultiBox006EditMode = false;
char TextmultiBox006Text[128] = "U/J - X   H/K - Z   Y/I - Y   Hold MMB - Scale W/H/L";
bool TextmultiBox007EditMode = false;
char TextmultiBox007Text[128] = "Hold RMB - Move X/Y   Scroll - Z  T/G - Scale   O/L - Rotate";
bool TextmultiBox008EditMode = false;
char TextmultiBox008Text[128] = "Hold LMB - Resize W/H   Scroll - L   Enter/DblClick - Place";

void DrawOverlay(){
    if (IsKeyDown(KEY_H)){
        GuiWindowBox((Rectangle){288, 72, 672, 384}, "Help");
        GuiLabel((Rectangle){312, 112, 152, 24}, "oz_editor v.1.0");
        GuiLine((Rectangle){312, 136, 624, 24}, NULL);
        GuiLabel((Rectangle){312, 160, 120, 24}, "Editor Controls");
        if (GuiTextBox((Rectangle){320, 192, 184, 112}, TextmultiBox005Text, 128, TextmultiBox005EditMode)) TextmultiBox005EditMode = !TextmultiBox005EditMode;
        if (GuiTextBox((Rectangle){320, 304, 192, 88}, TextmultiBox006Text, 128, TextmultiBox006EditMode)) TextmultiBox006EditMode = !TextmultiBox006EditMode;
        if (GuiTextBox((Rectangle){520, 192, 168, 112}, TextmultiBox007Text, 128, TextmultiBox007EditMode)) TextmultiBox007EditMode = !TextmultiBox007EditMode;
        if (GuiTextBox((Rectangle){520, 304, 168, 88}, TextmultiBox008Text, 128, TextmultiBox008EditMode)) TextmultiBox008EditMode = !TextmultiBox008EditMode;
        GuiLabel((Rectangle){696, 240, 120, 24}, "Models - Scripts - Pickups");
        GuiLabel((Rectangle){696, 336, 120, 24}, "Nodes - Collisions - Env");
    }

    GuiWindowBox((Rectangle){0, 0, 144, 720}, "Model Menu");
    if (GuiButton((Rectangle){8, 32, 120, 24}, "Model 1")){EMID = 1; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 64, 120, 24}, "Model 2")){EMID = 2; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 96, 120, 24}, "Model 3")){EMID = 3; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 128, 120, 24}, "Model 4")){EMID = 4; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 160, 120, 24}, "Model 5")){EMID = 5; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 192, 120, 24}, "Model 6")){EMID = 6; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 224, 120, 24}, "Model 7")){EMID = 7; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 256, 120, 24}, "Model 8")){EMID = 8; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 288, 120, 24}, "Model 9")){EMID = 9; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 320, 120, 24}, "Model 10")){EMID = 10; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 352, 120, 24}, "Model 11")){EMID = 11; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 384, 120, 24}, "Model 12")){EMID = 12; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 416, 120, 24}, "Model 13")){EMID = 13; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 448, 120, 24}, "Model 14")){EMID = 14; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 480, 120, 24}, "Model 15")){EMID = 15; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 512, 120, 24}, "Model 16")){EMID = 16; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 544, 120, 24}, "Model 17")){EMID = 17; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 576, 120, 24}, "Model 18")){EMID = 18; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 608, 120, 24}, "Model 19")){EMID = 19; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 640, 120, 24}, "Model 20")){EMID = 20; g_placeMode = PlaceMode::MODEL;}
    GuiLine((Rectangle){8, 664, 120, 24}, NULL);

    GuiWindowBox((Rectangle){144, 624, 532, 96}, "Actions");
    if (GuiButton((Rectangle){152, 672, 80, 32}, "Undo")){
        if (!g_documentPath.empty() && g_documentPath.extension() == ".wdl") LoadWorldDocument(g_documentPath);
    }
    if (GuiButton((Rectangle){240, 672, 64, 32}, "Save")){
        if (!g_documentPath.empty() && g_documentPath.extension() == ".wdl") SaveWorldDocument(g_documentPath);
        else FileSaveAs();
    }
    if (GuiButton((Rectangle){336, 672, 112, 32}, "Reset Camera")) OTEditor.MainCamera.position = {0, 10, 0};
    if (GuiButton((Rectangle){456, 672, 40, 32}, "UP")) OTEditor.MainCamera.position.y += 2;
    if (GuiButton((Rectangle){496, 672, 48, 32}, "DOWN")) OTEditor.MainCamera.position.y -= 2;
    if (GuiButton((Rectangle){552, 672, 116, 32}, "Env Settings")) {
        ShowEnvPanel(!g_editorPanels.showEnvPanel);
        g_placeMode = PlaceMode::ENV;
    }

    GuiWindowBox((Rectangle){912 + 184, 232, 184, 304}, "Scripts");
    if (GuiButton((Rectangle){944+184, 264, 120, 24}, "Script 1")){EMID = 101; LoadEditor(TextFormat("%s/Scripts/Script1.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 288, 120, 24}, "Script 2")){EMID = 102; LoadEditor(TextFormat("%s/Scripts/Script2.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 312, 120, 24}, "Script 3")){EMID = 103; LoadEditor(TextFormat("%s/Scripts/Script3.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 336, 120, 24}, "Script 4")){EMID = 104; LoadEditor(TextFormat("%s/Scripts/Script4.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 360, 120, 24}, "Script 5")){EMID = 105; LoadEditor(TextFormat("%s/Scripts/Script5.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 384, 120, 24}, "Script 6")){EMID = 106; LoadEditor(TextFormat("%s/Scripts/Script6.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 408, 120, 24}, "Script 7")){EMID = 107; LoadEditor(TextFormat("%s/Scripts/Script7.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 432, 120, 24}, "Script 8")){EMID = 108; LoadEditor(TextFormat("%s/Scripts/Script8.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 456, 120, 24}, "Script 9")){EMID = 109; LoadEditor(TextFormat("%s/Scripts/Script9.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 480, 120, 24}, "Script 10")){EMID = 110; LoadEditor(TextFormat("%s/Scripts/Script10.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 504, 120, 24}, "Launch Script")) LoadEditor(TextFormat("%s/Scripts/Launch.ps", OTEditor.Path));

    if(GetCollision(912+184, 232, 184, 304, GetMouseX(), GetMouseY(), 5, 5)){
        GuiWindowBox((Rectangle){600, 50, 352, 500}, "Script Preview");
        GuiTextBox((Rectangle){600, 70, 352, 480}, ScriptEditorBuffer, sizeof(ScriptEditorBuffer), 1);
    }

    GuiWindowBox((Rectangle){912+184, 24, 184, 192}, "Collisions");
    if (GuiButton((Rectangle){944+184, 56, 120, 24}, "Box Collision")){
        EMID = 100; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){944+184, 88, 120, 24}, "Adv Collision")){
        EMID = -1; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){944+184, 120, 120, 24}, "Height Clip Box")){
        EMID = -2; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    CollisionToggle = GuiToggle((Rectangle){920+184, 176, 160, 32}, "Collision", CollisionToggle);

    // Win32 dialog buttons (replaces old inline panels)
    if (GuiButton((Rectangle){8, 572, 128, 24}, "Pickups...")) ShowPickupPanel(!g_editorPanels.showPickupPanel);
    if (GuiButton((Rectangle){8, 600, 128, 24}, "Nodes...")) ShowNodePanel(!g_editorPanels.showNodePanel);
    if (GuiButton((Rectangle){8, 628, 128, 24}, "Env...")) ShowEnvPanel(!g_editorPanels.showEnvPanel);

    // Click handler for model/pickup/node panels
    if (IsMouseButtonPressed(0) && (
        GetCollision(0, 0, 144, 720, GetMouseX(), GetMouseY(), 5, 5) ||
        GetCollision(912+184, 232, 184, 304, GetMouseX(), GetMouseY(), 5, 5)))
    {
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (IsMouseButtonPressed(0) && GetCollision(912+184, 24, 184, 192, GetMouseX(), GetMouseY(), 5, 5)){
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
}

int PreviewEMID = 0;

void RenderPreview(){
    DrawLine(288, 200, GetMouseX(), GetMouseY(), WHITE);
    BeginTextureMode(PreviewTarget);
    ClearBackground(BLACK);
    UpdateCamera(&OTEditor.PreviewCamera, CAMERA_ORBITAL);
    BeginMode3D(OTEditor.PreviewCamera);

    DrawGrid(10, 1.0f);

    PreviewEMID = int((GetMouseY() - 32) / 32);
    switch (PreviewEMID){
        case 1:  CurrentModel = WDLModels.Model1;  break;
        case 2:  CurrentModel = WDLModels.Model2;  break;
        case 3:  CurrentModel = WDLModels.Model3;  break;
        case 4:  CurrentModel = WDLModels.Model4;  break;
        case 5:  CurrentModel = WDLModels.Model5;  break;
        case 6:  CurrentModel = WDLModels.Model6;  break;
        case 7:  CurrentModel = WDLModels.Model7;  break;
        case 8:  CurrentModel = WDLModels.Model8;  break;
        case 9:  CurrentModel = WDLModels.Model9;  break;
        case 10: CurrentModel = WDLModels.Model10; break;
        case 11: CurrentModel = WDLModels.Model11; break;
        case 12: CurrentModel = WDLModels.Model12; break;
        case 13: CurrentModel = WDLModels.Model13; break;
        case 14: CurrentModel = WDLModels.Model14; break;
        case 15: CurrentModel = WDLModels.Model15; break;
        case 16: CurrentModel = WDLModels.Model16; break;
        case 17: CurrentModel = WDLModels.Model17; break;
        case 18: CurrentModel = WDLModels.Model18; break;
        case 19: CurrentModel = WDLModels.Model19; break;
        case 20: CurrentModel = WDLModels.Model20; break;
        default: break;
    }
    DrawModelEx(CurrentModel, {0,0,0}, {0,0,0}, 0, {1,1,1}, WHITE);
    DrawLine3D({0,0,0}, {3,0,0}, RED);
    DrawLine3D({0,0,0}, {0,3,0}, BLUE);
    DrawLine3D({0,0,0}, {0,0,3}, GREEN);

    EndMode3D();
    EndTextureMode();
    DrawTextureRec(PreviewTarget.texture, {0, 0, 256, -256}, {288, 200}, WHITE);
    DrawRectangleLines(288, 200, 256, 256, WHITE);
    if(CurrentModel.meshes != NULL){
        DrawText(TextFormat("Triangles:%i", CurrentModel.meshes[0].triangleCount), 288, 456, 15, WHITE);
        DrawText(TextFormat("Model ID:%i", PreviewEMID), 288, 471, 15, WHITE);
        int tc = CurrentModel.meshes[0].triangleCount;
        DrawText(tc > 2000 ? "Very Low Perf" : tc > 1000 ? "Low Perf" : tc > 500 ? "Med Perf" : tc > 100 ? "High Perf" : "Very High Perf",
                 288, 486, 15, tc > 2000 ? DARKBLUE : tc > 1000 ? BLUE : tc > 500 ? ORANGE : tc > 100 ? RED : PINK);
    }
}
