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
#include "../../Source/Physics/OzBsp.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

// Menu command IDs
enum EditorMenuCmd {
    IDM_NEW = 1001,
    IDM_OPEN,
    IDM_SAVE,
    IDM_SAVE_AS,
    IDM_PLAY_TEST,
    IDM_EXIT,
    IDM_MODEL_BRW = 1100,
    IDM_SOUND_MGR,
    IDM_TEXTURE_MGR,
    IDM_PAWN_MGR,
    IDM_SCRIPT_MGR,
    IDM_ZONE_PROPS,
    IDM_NODE_PANEL,
    IDM_PICKUP_PANEL,
    IDM_LIGHT_PROPS,
    IDM_HEIGHTMAP,
    IDM_FULLSCREEN,
    IDM_RESET_CAM,
    IDM_VIEW_TOP,
    IDM_VIEW_BOTTOM,
    IDM_VIEW_RIGHT,
    IDM_VIEW_LEFT,
    IDM_VIEW_PERSPECTIVE,
    IDM_ABOUT,
};

// Forward declarations
static void EditorLog(const char* fmt, ...);

// WDLModels definition (extern declared in Editor.hpp)
GameModels WDLModels;

// CSG processor — accumulates brush operations for collision geometry
static CsgProcessor g_csgProc;

// ---------------------------------------------------------------------------
// Entity selection system (right-click raycast)
// ---------------------------------------------------------------------------
enum class SelType { NONE, BRUSH, MODEL, NPC, PICKUP, LIGHT, ZONE };
struct EditorSelection {
    SelType type = SelType::NONE;
    int index = -1;
    std::string name;
    Vector3 pos{0,0,0};
    float scale = 1.0f;
    float rotation = 0.0f;
};
static EditorSelection g_sel;

// Right-click state: drag vs click detection
static bool g_rbDown = false;
static Vector2 g_rbDownPos{0,0};
static bool g_showContextMenu = false;
static int g_contextMenuChoice = -1;

static RayCollision RaycastTestBrushes(Ray ray) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
    for (size_t i = 0; i < vols.size(); i++) {
        RayCollision hit = GetRayCollisionBox(ray, vols[i].aabb);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            g_sel = { SelType::BRUSH, (int)i, "Brush", 
                      {(vols[i].aabb.min.x + vols[i].aabb.max.x)/2,
                       (vols[i].aabb.min.y + vols[i].aabb.max.y)/2,
                       (vols[i].aabb.min.z + vols[i].aabb.max.z)/2} };
        }
    }
    return best;
}

static RayCollision RaycastTestModels(Ray ray) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    for (size_t i = 0; i < WDLModels.models.size(); i++) {
        auto& m = WDLModels.models[i];
        if (!m.loaded || m.model.meshCount == 0) continue;
        BoundingBox box = GetMeshBoundingBox(m.model.meshes[0]);
        box.min = Vector3Add(box.min, (Vector3){0,0,0}); // model position offset if stored
        box.max = Vector3Add(box.max, (Vector3){0,0,0});
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            g_sel = { SelType::MODEL, (int)i, m.name.empty() ? "Model" : m.name, {0,0,0} };
        }
    }
    return best;
}

static RayCollision RaycastTestPawns(Ray ray) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& pawns = PawnSystem::Instance().GetPawns();
    for (auto& p : pawns) {
        if (!p.active) continue;
        BoundingBox box = { {p.position.x - 1, p.position.y - 1, p.position.z - 1},
                            {p.position.x + 1, p.position.y + 1, p.position.z + 1} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            g_sel = { SelType::NPC, (int)p.id, p.defName, p.position };
        }
    }
    return best;
}

static RayCollision RaycastTestPickups(Ray ray) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& pickups = PawnSystem::Instance().GetPickups();
    for (auto& pk : pickups) {
        if (!pk.active) continue;
        BoundingBox box = { {pk.position.x - 0.3f, pk.position.y - 0.3f, pk.position.z - 0.3f},
                            {pk.position.x + 0.3f, pk.position.y + 0.3f, pk.position.z + 0.3f} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            g_sel = { SelType::PICKUP, (int)pk.id, pk.typeName, pk.position };
        }
    }
    return best;
}

static RayCollision RaycastTestZones(Ray ray) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& zones = PawnSystem::Instance().GetZones();
    for (auto& z : zones) {
        RayCollision hit = GetRayCollisionBox(ray, z.bounds);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            g_sel = { SelType::ZONE, (int)z.id, "ZoneVolume", {
                (z.bounds.min.x + z.bounds.max.x) * 0.5f,
                (z.bounds.min.y + z.bounds.max.y) * 0.5f,
                (z.bounds.min.z + z.bounds.max.z) * 0.5f
            }};
        }
    }
    return best;
}

static void EditorPickEntity() {
    Ray ray = GetMouseRay(GetMousePosition(), OTEditor.MainCamera);
    g_sel = { SelType::NONE, -1, "", {0,0,0} };
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };

    auto test = [&](RayCollision hit) {
        if (hit.hit && hit.distance < best.distance) { best = hit; }
    };
    test(RaycastTestBrushes(ray));
    test(RaycastTestModels(ray));
    test(RaycastTestPawns(ray));
    test(RaycastTestPickups(ray));
    test(RaycastTestZones(ray));

    if (best.hit) {
        EditorLog("Selected: %s (type=%d idx=%d dist=%.1f)",
                  g_sel.name.c_str(), (int)g_sel.type, g_sel.index, best.distance);
    }
}

static void DrawContextMenu() {
    if (!g_showContextMenu) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int mw = 180, mh = 140;
    int mx = (sw - mw) / 2, my = (sh - mh) / 2;
    DrawRectangle(mx, my, mw, mh, (Color){40,40,50,240});
    DrawRectangleLines(mx, my, mw, mh, (Color){100,100,120,255});

    const char* title = g_sel.name.empty() ? "Entity" : g_sel.name.c_str();
    DrawText(title, mx + 8, my + 6, 12, WHITE);
    DrawLine(mx, my + 22, mx + mw, my + 22, (Color){80,80,100,255});

    struct CmItem { const char* label; int action; };
    CmItem items[] = {
        {"Properties", 1},
        {"Delete", 2},
        {"Duplicate", 3},
        {"Cancel", 0}
    };
    int yy = my + 28;
    for (auto& item : items) {
        Rectangle r = {(float)mx + 4, (float)yy, (float)mw - 8, 22};
        Color c = (CheckCollisionPointRec(GetMousePosition(), r)) ? (Color){60,70,100,255} : (Color){0,0,0,0};
        DrawRectangleRec(r, c);
        DrawText(item.label, mx + 10, yy + 4, 11, LIGHTGRAY);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), r)) {
            g_contextMenuChoice = item.action;
            g_showContextMenu = false;
        }
        yy += 24;
    }
}

// Properties panel state
static bool g_showPropsPanel = false;
static char g_propsBuf[6][32] = {{0}}; // editable fields
static int g_propsFieldCount = 0;
static int g_propsFocusField = -1;
static int g_propsCursorBlink = 0;

static void DrawPropertiesPanel() {
    if (!g_showPropsPanel) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int pw = 300, ph = 220;
    int px = (sw - pw) / 2, py = (sh - ph) / 2;
    DrawRectangle(px, py, pw, ph, (Color){45,45,55,240});
    DrawRectangleLines(px, py, pw, ph, (Color){100,100,120,255});

    int yy = py + 10;
    DrawText("Entity Properties", px + 8, yy, 14, WHITE); yy += 22;
    DrawLine(px, yy, px + pw, yy, (Color){80,80,100,255}); yy += 6;

    Rectangle closeR = {(float)px + pw - 70, (float)(py + ph - 28), 60, 20};
    Rectangle applyR = {(float)px + 10, (float)(py + ph - 28), 60, 20};

    const char* labels[] = {"X:", "Y:", "Z:", "W:", "H:", "L:"};
    for (int i = 0; i < g_propsFieldCount && i < 6; i++) {
        DrawText(labels[i], px + 10, yy + 2, 12, LIGHTGRAY);
        Rectangle r = {(float)px + 50, (float)yy, 100, 18};
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        bool focus = (i == g_propsFocusField);
        Color c = focus ? (Color){50,65,90,255} : (hover ? (Color){60,70,90,255} : (Color){30,35,45,255});
        DrawRectangleRec(r, c);
        DrawRectangleLinesEx(r, 1, focus ? (Color){150,180,220,255} : (Color){80,80,100,255});
        std::string disp = g_propsBuf[i];
        if (focus) {
            g_propsCursorBlink++;
            if ((g_propsCursorBlink / 20) % 2 == 0) disp += "_";
        }
        DrawText(disp.c_str(), px + 54, yy + 2, 11, WHITE);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hover) {
            g_propsFocusField = i;
            g_propsCursorBlink = 0;
        } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !hover
                   && !CheckCollisionPointRec(GetMousePosition(), closeR)
                   && !CheckCollisionPointRec(GetMousePosition(), applyR)) {
            g_propsFocusField = -1;
        }
        yy += 22;
    }

    // Keyboard input for focused field
    if (g_propsFocusField >= 0 && g_propsFocusField < g_propsFieldCount) {
        int len = (int)strlen(g_propsBuf[g_propsFocusField]);
        int key = GetCharPressed();
        while (key > 0) {
            if ((key >= '0' && key <= '9') || key == '.' || key == '-' || key == '+') {
                if (len < 31) {
                    g_propsBuf[g_propsFocusField][len] = (char)key;
                    g_propsBuf[g_propsFocusField][len + 1] = '\0';
                }
            }
            key = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
            g_propsBuf[g_propsFocusField][len - 1] = '\0';
        }
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ENTER)) {
            g_propsFocusField = (g_propsFocusField + 1) % g_propsFieldCount;
        }
    }

    // Close button
    Color closeC = CheckCollisionPointRec(GetMousePosition(), closeR) ? (Color){80,50,50,255} : (Color){50,50,60,255};
    DrawRectangleRec(closeR, closeC);
    DrawRectangleLinesEx(closeR, 1, (Color){120,80,80,255});
    DrawText("Close", px + pw - 58, py + ph - 26, 11, WHITE);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), closeR))
        g_showPropsPanel = false;

    // Apply button
    Color applyC = CheckCollisionPointRec(GetMousePosition(), applyR) ? (Color){50,80,50,255} : (Color){50,60,50,255};
    DrawRectangleRec(applyR, applyC);
    DrawRectangleLinesEx(applyR, 1, (Color){80,120,80,255});
    DrawText("Apply", px + 16, py + ph - 26, 11, WHITE);
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), applyR)) {
        float vals[6];
        for (int i = 0; i < g_propsFieldCount && i < 6; i++)
            vals[i] = (float)atof(g_propsBuf[i]);
        if (g_sel.type == SelType::NPC) {
            Pawn* p = PawnSystem::Instance().Get(g_sel.index);
            if (p) p->position = {vals[0], vals[1], vals[2]};
        } else if (g_sel.type == SelType::PICKUP) {
            auto& pickups = PawnSystem::Instance().GetPickups();
            for (auto& pk : pickups) {
                if ((int)pk.id == g_sel.index) {
                    pk.position = {vals[0], vals[1], vals[2]};
                    break;
                }
            }
        } else if (g_sel.type == SelType::BRUSH && g_propsFieldCount >= 6) {
            auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
            if (g_sel.index >= 0 && g_sel.index < (int)vols.size()) {
                Vector3 center = {vals[0], vals[1], vals[2]};
                Vector3 size = {vals[3], vals[4], vals[5]};
                vols[g_sel.index].aabb.min = {
                    center.x - size.x * 0.5f,
                    center.y - size.y * 0.5f,
                    center.z - size.z * 0.5f
                };
                vols[g_sel.index].aabb.max = {
                    center.x + size.x * 0.5f,
                    center.y + size.y * 0.5f,
                    center.z + size.z * 0.5f
                };
                OzoneLoader::Instance().RebuildCollisionVolumes();
                EditorLog("Applied brush properties idx=%d (pos=%.1f,%.1f,%.1f size=%.1f,%.1f,%.1f)",
                          g_sel.index,
                          vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]);
            }
        }
        EditorLog("Applied properties to %s idx=%d", g_sel.name.c_str(), g_sel.index);
        g_showPropsPanel = false;
    }
}

// ---------------------------------------------------------------------------
// Native Win32 Menu Bar
// ---------------------------------------------------------------------------
static void CreateEditorMenuBar() {
#ifdef _WIN32
    HWND hWnd = (HWND)GetWindowHandle();
    if (!hWnd) return;

    HMENU hMenu = CreateMenu();

    // File menu
    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, IDM_NEW, "&New\tN");
    AppendMenuA(hFile, MF_STRING, IDM_OPEN, "&Open...\tO");
    AppendMenuA(hFile, MF_STRING, IDM_SAVE, "&Save\tS");
    AppendMenuA(hFile, MF_STRING, IDM_SAVE_AS, "Save &As...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_PLAY_TEST, "&Play Test\tP");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_EXIT, "E&xit\tQ");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

    // View menu (panels)
    HMENU hView = CreatePopupMenu();
    AppendMenuA(hView, MF_STRING, IDM_MODEL_BRW, "Model &Browser\tF5");
    AppendMenuA(hView, MF_STRING, IDM_SOUND_MGR, "&Sound Manager\tF6");
    AppendMenuA(hView, MF_STRING, IDM_TEXTURE_MGR, "&Texture Manager\tF7");
    AppendMenuA(hView, MF_STRING, IDM_PAWN_MGR, "&Pawn Manager\tF8");
    AppendMenuA(hView, MF_STRING, IDM_SCRIPT_MGR, "&Script Manager\tF9");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, IDM_ZONE_PROPS, "&Zone Properties\tF12");
    AppendMenuA(hView, MF_STRING, IDM_NODE_PANEL, "&Node Panel");
    AppendMenuA(hView, MF_STRING, IDM_PICKUP_PANEL, "&Pickups\tF10");
    AppendMenuA(hView, MF_STRING, IDM_LIGHT_PROPS, "&Light Properties");
    AppendMenuA(hView, MF_STRING, IDM_HEIGHTMAP, "&Heightmap Editor\tH");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "&View");

    // Camera menu
    HMENU hCam = CreatePopupMenu();
    AppendMenuA(hCam, MF_STRING, IDM_RESET_CAM, "&Reset Camera\tHome");
    AppendMenuA(hCam, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_TOP, "&Top\tNumpad 7");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_BOTTOM, "&Bottom\tNumpad 1");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_RIGHT, "&Right\tNumpad 3");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_LEFT, "&Left\tNumpad 9");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_PERSPECTIVE, "&Perspective\tNumpad 5");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hCam, "&Camera");

    // Settings menu
    HMENU hSettings = CreatePopupMenu();
    AppendMenuA(hSettings, MF_STRING, IDM_FULLSCREEN, "Toggle &Fullscreen\tF11");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSettings, "&Settings");

    // Help menu
    HMENU hHelp = CreatePopupMenu();
    AppendMenuA(hHelp, MF_STRING, IDM_ABOUT, "&About AngelEd");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");

    SetMenu(hWnd, hMenu);
#endif
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

// Orthographic / view preset state
static bool g_orthoView = false;
static Camera3D g_perspectiveCamState = {0};

static void SetViewPreset(const Vector3& pos, const Vector3& target, const Vector3& up) {
    if (!g_orthoView) {
        g_perspectiveCamState = OTEditor.MainCamera;
        g_orthoView = true;
    }
    OTEditor.MainCamera.position = pos;
    OTEditor.MainCamera.target = target;
    OTEditor.MainCamera.up = up;
    OTEditor.MainCamera.projection = CAMERA_ORTHOGRAPHIC;
}

static void SetViewPerspective() {
    if (g_orthoView) {
        // Restore saved state
        OTEditor.MainCamera = g_perspectiveCamState;
        g_orthoView = false;
    }
    OTEditor.MainCamera.projection = CAMERA_PERSPECTIVE;
}

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

    // Auto-load .oztex packages from the world directory (for textures used by this level)
    {
        fs::path worldDir = path.parent_path();
        int loadedPkg = 0;
        if (fs::exists(worldDir)) {
            for (auto& entry : fs::recursive_directory_iterator(worldDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".oztex" || ext == ".ozpak") {
                        auto& loader = PackageAssetLoader::Instance();
                        if (loader.LoadPackageFile(entry.path().string().c_str())) {
                            loadedPkg++;
                            EditorLog("Loaded package: %s", entry.path().filename().string().c_str());
                        }
                    }
                }
            }
        }
        if (loadedPkg > 0) {
            EditorLog("Auto-loaded %d texture package(s) from world directory", loadedPkg);
        }
    }

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

static void ExportToOzone(std::ostream& output) {
    // Header
    output << "# OZONE world exported from AngelEd\n";
    output << "# Format: ozone v1.0\n\n";

    // Export collision volumes as box primitives
    auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
    for (size_t i = 0; i < vols.size(); i++) {
        auto& v = vols[i];
        Vector3 center = {(v.aabb.min.x + v.aabb.max.x) * 0.5f,
                          (v.aabb.min.y + v.aabb.max.y) * 0.5f,
                          (v.aabb.min.z + v.aabb.max.z) * 0.5f};
        float w = v.aabb.max.x - v.aabb.min.x;
        float h = v.aabb.max.y - v.aabb.min.y;
        float d = v.aabb.max.z - v.aabb.min.z;
        if (w < 0.01f) w = 1.0f;
        if (h < 0.01f) h = 1.0f;
        if (d < 0.01f) d = 1.0f;
        // Use CSG op from stored data (default to add)
        const char* csgPrefix = "add";
        output << csgPrefix << " box " << center.x << " " << center.y << " " << center.z
               << " " << w << " " << h << " " << d << " 0\n";
    }

    // Heightmap
    if (WDLModels.HeightMapReady) {
        output << "heightmap Models/HeightMap.png Models/HeightMapTexture.png "
               << WDLModels.HeightMapPosition.x << " " << WDLModels.HeightMapPosition.y << " " << WDLModels.HeightMapPosition.z
               << " " << WDLModels.HeightMapScale
               << " " << WDLModels.HeightMapSize.x << " " << WDLModels.HeightMapSize.y << " " << WDLModels.HeightMapSize.z << "\n";
    }

    output << "\n# Entities\n";

    // Player starts
    auto& pawns = PawnSystem::Instance();
    for (auto& start : pawns.GetPlayerStarts()) {
        output << "playerstart " << start.position.x << " " << start.position.y << " " << start.position.z << " " << start.yaw << "\n";
    }

    // Pickups
    for (auto& pickup : pawns.GetPickups()) {
        output << "pickup " << pickup.typeName << " " << pickup.position.x << " " << pickup.position.y << " " << pickup.position.z;
        if (pickup.respawnTime > 0.01f) output << " " << pickup.respawnTime;
        output << "\n";
    }

    // NPCs
    for (auto& pawn : pawns.GetPawns()) {
        if (!pawn.active || pawn.defName.empty()) continue;
        output << "npc " << pawn.defName << " " << pawn.position.x << " " << pawn.position.y << " " << pawn.position.z << "\n";
    }

    // Zone volumes
    for (auto& zone : pawns.GetZones()) {
        const char* zt = "water";
        switch (zone.zoneType) {
            case ZoneType::ZONE_LADDER: zt = "ladder"; break;
            case ZoneType::ZONE_SKY: zt = "sky"; break;
            case ZoneType::ZONE_REVERB: zt = "reverb"; break;
            case ZoneType::ZONE_GAMEPLAY_SOUND: zt = "sound"; break;
            default: zt = "water"; break;
        }
        output << "zone " << zt
               << " " << zone.bounds.min.x << " " << zone.bounds.min.y << " " << zone.bounds.min.z
               << " " << zone.bounds.max.x << " " << zone.bounds.max.y << " " << zone.bounds.max.z
               << " " << zone.intensity;
        // Export env overrides if any are set
        auto& eo = zone.envOverrides;
        if (eo.applyFog || eo.applyAmbient || eo.reverbMix > 0.0f) {
            output << " " << eo.fogR << " " << eo.fogG << " " << eo.fogB
                   << " " << eo.fogDensity << " " << eo.fogStart << " " << eo.fogEnd
                   << " " << eo.ambR << " " << eo.ambG << " " << eo.ambB
                   << " " << eo.ambIntensity
                   << " " << eo.reverbMix << " " << eo.reverbDecay;
        }
        output << "\n";
    }

    // Emitters
    for (auto& emitter : pawns.GetEmitters()) {
        const char* et = (emitter.type == EmitterType::SOUND) ? "sound" : "music";
        output << "emitter " << et << " " << emitter.position.x << " " << emitter.position.y << " " << emitter.position.z << "\n";
    }

    output << "\n# End of OZONE export\n";
}

static bool SaveWorldDocument(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".ozone") {
        std::ofstream output(path);
        if (!output.is_open()) return false;
        ExportToOzone(output);
        g_documentPath = path;
        SetWorldDirectory(path.parent_path());
        return true;
    }

    if (ext != ".wdl") return false;
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
    CreateEditorMenuBar();
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

    // Load pawn definitions from config files (data-driven)
    {
        auto& ps = PawnSystem::Instance();
        const char* defsDir = "GameData/Global/PawnDefs";
        if (fs::exists(defsDir)) {
            int loaded = 0;
            for (auto& entry : fs::directory_iterator(defsDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".cfg") continue;
                std::ifstream f(entry.path());
                if (!f.is_open()) continue;
                std::string name, sp, sc;
                float speed = 1.5f, aggroRange = 6.0f, attackRange = 1.5f, damage = 10.0f;
                int maxHealth = 100;
                std::string line;
                while (std::getline(f, line)) {
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                    size_t eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = line.substr(0, eq);
                    std::string val = line.substr(eq + 1);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t\r") + 1);
                    if (key == "name") name = val;
                    else if (key == "speed") speed = std::stof(val);
                    else if (key == "aggroRange") aggroRange = std::stof(val);
                    else if (key == "attackRange") attackRange = std::stof(val);
                    else if (key == "damage") damage = std::stof(val);
                    else if (key == "maxHealth") maxHealth = std::stoi(val);
                    else if (key == "sprite_path") sp = val;
                    else if (key == "scream_path") sc = val;
                }
                if (!name.empty()) {
                    PawnDef def;
                    def.name = strdup(name.c_str());
                    def.speed = speed;
                    def.aggroRange = aggroRange;
                    def.attackRange = attackRange;
                    def.damage = damage;
                    def.maxHealth = maxHealth;
                    def.sprite_path = sp.empty() ? nullptr : strdup(sp.c_str());
                    def.scream_path = sc.empty() ? nullptr : strdup(sc.c_str());
                    ps.RegisterDef(def);
                    PawnManagerAddPawn(name.c_str(), (std::string("GameData/Models/") + name + ".obj").c_str());
                    loaded++;
                    EditorLog("Loaded pawn def: %s", name.c_str());
                }
            }
            EditorLog("Loaded %d pawn definitions from %s", loaded, defsDir);
        } else {
            // Legacy fallback if no config directory exists
            EditorLog("WARN: %s not found, using hardcoded defaults", defsDir);
            ps.RegisterDef({"Walker", 1.5f, 6.0f, 1.5f, 10.0f, 100});
            ps.RegisterDef({"Skaarj", 2.5f, 10.0f, 2.0f, 20.0f, 150});
            ps.RegisterDef({"Brute", 1.0f, 4.0f, 1.5f, 30.0f, 250});
            ps.RegisterDef({"Floater", 1.2f, 8.0f, 3.0f, 15.0f, 80});
            PawnManagerAddPawn("Walker", "GameData/Models/Walker.obj");
            PawnManagerAddPawn("Skaarj", "GameData/Models/Skaarj.obj");
            PawnManagerAddPawn("Brute", "GameData/Models/Brute.obj");
            PawnManagerAddPawn("Floater", "GameData/Models/Floater.obj");
        }
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
        // Process Win32 messages for child panels + menu bar
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_COMMAND) {
                int id = LOWORD(msg.wParam);
                switch (id) {
                    case IDM_NEW:           FileNew(); break;
                    case IDM_OPEN:          { std::string p; if (ChooseOpenWorldFile(p)) g_pendingOpenPath = fs::path(p); } break;
                    case IDM_SAVE:          if (!g_documentPath.empty()) SaveWorldDocument(g_documentPath); break;
                    case IDM_SAVE_AS:       FileSaveAs(); break;
                    case IDM_PLAY_TEST:     { /* launch test */ std::string tempPath = "System/Cache/editor_test.wdl";
                        std::wstring wstr = OTEditor.WorldData; std::string wd(wstr.begin(), wstr.end());
                        std::ofstream f(tempPath); if (f.is_open()) { f << wd; f.close(); system(("start \"\" Angels95.exe --world " + tempPath).c_str()); } } break;
                    case IDM_EXIT:          CloseWindow(); break;
                    case IDM_MODEL_BRW:     ToggleModelBrowser(); break;
                    case IDM_SOUND_MGR:     ToggleSoundMgr(); break;
                    case IDM_TEXTURE_MGR:   ToggleTextureMgr(); break;
                    case IDM_PAWN_MGR:      TogglePawnMgr(); break;
                    case IDM_SCRIPT_MGR:    ToggleScriptMgr(); break;
                    case IDM_ZONE_PROPS:    ToggleEnvPanel(); break;
                    case IDM_NODE_PANEL:    ToggleNodePanel(); break;
                    case IDM_PICKUP_PANEL:  TogglePickupPanel(); break;
                    case IDM_LIGHT_PROPS:   ShowLightProps(true); break;
                    case IDM_HEIGHTMAP:     ToggleHeightmapEditor(); break;
                    case IDM_FULLSCREEN:    ToggleFullscreen(); break;
                    case IDM_RESET_CAM:     SetViewPerspective(); ResetCamera(); break;
                    case IDM_VIEW_TOP:      { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x,c.y+80,c.z+0.1f},c,{0,0,-1}); } break;
                    case IDM_VIEW_BOTTOM:   { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x,c.y-80,c.z+0.1f},c,{0,0,1}); } break;
                    case IDM_VIEW_RIGHT:    { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x+80,c.y,c.z},c,{0,1,0}); } break;
                    case IDM_VIEW_LEFT:     { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x-80,c.y,c.z},c,{0,1,0}); } break;
                    case IDM_VIEW_PERSPECTIVE: SetViewPerspective(); break;
                    case IDM_ABOUT:         MessageBoxA(NULL, "AngelEd v1.0\nOzWorld Editor\nBased on OmegaTech\nTribeWarez 2026", "About AngelEd", MB_OK | MB_ICONINFORMATION); break;
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
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

        // Right-click: drag resizes placement ghost; click picks entity + context menu
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            g_rbDown = true;
            g_rbDownPos = GetMousePosition();
            if (LastClickTime != 0) DoubleClick = true;
            else LastClickTime = 1;
        }
        if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && g_rbDown) {
            Vector2 delta = GetMouseDelta();
            if (fabsf(delta.x) > 3 || fabsf(delta.y) > 3) g_rbDown = false; // started dragging
        }
        if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON) && g_rbDown) {
            g_rbDown = false;
            // If not placing and not dragging, pick entity + show context menu
            if (!OmegaTechEditor.DrawModel) {
                EditorPickEntity();
                if (g_sel.type != SelType::NONE) {
                    g_showContextMenu = true;
                }
            }
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

        // Pawn system 
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

            // Axis gizmo
            float gizmoLen = fmaxf(ps * 3, 2.0f);
            float gizmoTip = gizmoLen * 0.1f;
            // X axis (Red)
            DrawLine3D({px, py, pz}, {px + gizmoLen, py, pz}, RED);
            DrawSphere({px + gizmoLen, py, pz}, gizmoTip, RED);
            // Y axis (Blue)
            DrawLine3D({px, py, pz}, {px, py + gizmoLen, pz}, BLUE);
            DrawSphere({px, py + gizmoLen, pz}, gizmoTip, BLUE);
            // Z axis (Green)
            DrawLine3D({px, py, pz}, {px, py, pz + gizmoLen}, GREEN);
            DrawSphere({px, py, pz + gizmoLen}, gizmoTip, GREEN);
            // Origin sphere
            DrawSphere({px, py, pz}, gizmoTip * 0.5f, (Color){180, 180, 180, 200});

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

                // CSG: push brush through processor for OZONE primitives (EMID >= 200)
                if (EMID >= 200 && g_placeMode == PlaceMode::MODEL) {
                    CsgBrush brush;
                    brush.op = (CsgOp)OmegaTechEditor.CSGOperation;
                    brush.minX = OmegaTechEditor.X;
                    brush.minY = OmegaTechEditor.Y;
                    brush.minZ = OmegaTechEditor.Z;
                    brush.maxX = OmegaTechEditor.X + OmegaTechEditor.W;
                    brush.maxY = OmegaTechEditor.Y + OmegaTechEditor.H;
                    brush.maxZ = OmegaTechEditor.Z + OmegaTechEditor.L;
                    g_csgProc.Apply(brush);
                    int merges = g_csgProc.MergePass();
                    // Rebuild OzoneLoader collision volumes from CSG result
                    OzoneLoader::Instance().RebuildCollisionVolumes();
                    std::vector<CsgProcessor::Volume> vols;
                    g_csgProc.GetVolumes(vols);
                    EditorLog("CSG: op=%d volumes=%d merges=%d",
                              (int)brush.op, (int)vols.size(), merges);
                }

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

        // Selection highlight (right-click picked entity)
        if (g_sel.type != SelType::NONE) {
            BoundingBox selBox = {{0,0,0},{0,0,0}};
            if (g_sel.type == SelType::NPC) {
                auto& pawns = PawnSystem::Instance().GetPawns();
                for (auto& p : pawns) {
                    if ((int)p.id == g_sel.index && p.active) {
                        selBox = {{p.position.x-1,p.position.y-1,p.position.z-1},
                                  {p.position.x+1,p.position.y+1,p.position.z+1}};
                        break;
                    }
                }
            } else if (g_sel.type == SelType::PICKUP) {
                auto& pickups = PawnSystem::Instance().GetPickups();
                for (auto& pk : pickups) {
                    if ((int)pk.id == g_sel.index && pk.active) {
                        selBox = {{pk.position.x-0.4f,pk.position.y-0.4f,pk.position.z-0.4f},
                                  {pk.position.x+0.4f,pk.position.y+0.4f,pk.position.z+0.4f}};
                        break;
                    }
                }
            } else if (g_sel.type == SelType::BRUSH) {
                auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
                if (g_sel.index >= 0 && g_sel.index < (int)vols.size())
                    selBox = vols[g_sel.index].aabb;
            } else if (g_sel.type == SelType::ZONE) {
                auto& zones = PawnSystem::Instance().GetZones();
                for (auto& z : zones) {
                    if ((int)z.id == g_sel.index) {
                        selBox = z.bounds;
                        break;
                    }
                }
            }
            if (selBox.min.x != selBox.max.x || selBox.min.y != selBox.max.y) {
                float pulse = 0.5f + 0.5f * sinf(GetTime() * 4);
                DrawBoundingBox(selBox, (Color){255,(unsigned char)(200*pulse),0,255});
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
                }
                bx += bw + 2;
            };

            tBtn(nullptr,"New",10); tBtn(nullptr,"Open",11); tBtn(nullptr,"Save",12);
            // Play button
            {
                const char* lbl = "Play";
                int bw = (int)strlen(lbl) * 7 + 10;
                Rectangle r = {(float)bx, 2, (float)bw, (float)tbH - 4};
                bool hover = CheckCollisionPointRec(GetMousePosition(), r);
                DrawRectangleRec(r, hover ? (Color){50,100,50,255} : (Color){35,80,35,255});
                DrawText(lbl, bx + 4, 7, 12, WHITE);
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    // Save world to temp file and launch client
                    std::string tempPath = "System/Cache/editor_test.wdl";
                    std::wstring wstr = OTEditor.WorldData;
                    std::string worldData(wstr.begin(), wstr.end());
                    std::ofstream f(tempPath);
                    if (f.is_open()) {
                        f << worldData;
                        f.close();
                        std::string cmd = std::string("start \"\" Angels95.exe --world ") + tempPath;
                        system(cmd.c_str());
                        EditorLog("Launched: %s", cmd.c_str());
                    }
                }
                bx += bw + 2;
            }
            bx += 6;
            tBtn("BBGeneric","Mod",0); tBtn(nullptr,"Snd",1); tBtn(nullptr,"Tex",2);
            tBtn(nullptr,"Pawn",3); tBtn(nullptr,"Scr",4); bx += 6;

            for (int li=0;li<3;li++) {
                const char* labs[]={"Lit","Unlit","Wire"};
                int lw=38; Color lc=(int)OTEditor.ViewMode==li?(Color){70,90,120,255}:(Color){45,45,50,255};
                DrawRectangle(bx,2,lw,tbH-4,lc);
                DrawText(labs[li],bx+4,7,12,(int)OTEditor.ViewMode==li?WHITE:LIGHTGRAY);
                if(CheckCollisionPointRec(GetMousePosition(),{(float)bx,2,(float)lw,(float)tbH-4})&&IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    LightingMode prev = OTEditor.ViewMode;
                    OTEditor.ViewMode = (LightingMode)li;
                    if (prev != OTEditor.ViewMode) {
                        static std::vector<Shader> savedModelShaders;
                        if (OTEditor.ViewMode == LightingMode::UNLIT) {
                            savedModelShaders.resize(WDLModels.models.size());
                            for (size_t i = 0; i < WDLModels.models.size(); i++) {
                                auto& m = WDLModels.models[i];
                                savedModelShaders[i] = (m.loaded && m.model.meshCount > 0)
                                    ? m.model.materials[0].shader : Shader{0};
                                if (m.loaded && m.model.meshCount > 0)
                                    m.model.materials[0].shader = Shader{0};
                            }
                            OzoneLoader::Instance().SetLitFogShaderEnabled(false);
                        } else if (prev == LightingMode::UNLIT) {
                            for (size_t i = 0; i < WDLModels.models.size() && i < savedModelShaders.size(); i++) {
                                auto& m = WDLModels.models[i];
                                if (m.loaded && m.model.meshCount > 0 && savedModelShaders[i].id > 0)
                                    m.model.materials[0].shader = savedModelShaders[i];
                            }
                            savedModelShaders.clear();
                            OzoneLoader::Instance().SetLitFogShaderEnabled(true);
                        }
                    }
                }
                bx+=lw+2;
            }
            bx+=6;
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

        // Context menu overlay (right-click entity)
        DrawContextMenu();
        DrawPropertiesPanel();
        if (g_contextMenuChoice >= 0) {
            int action = g_contextMenuChoice;
            g_contextMenuChoice = -1;

            if (action == 1) {
                // Properties
                g_propsFieldCount = 0;
                for (int i = 0; i < 6; i++) g_propsBuf[i][0] = '\0';
                if (g_sel.type == SelType::NPC) {
                    Pawn* p = PawnSystem::Instance().Get(g_sel.index);
                    if (p) {
                        snprintf(g_propsBuf[0], 32, "%.1f", p->position.x);
                        snprintf(g_propsBuf[1], 32, "%.1f", p->position.y);
                        snprintf(g_propsBuf[2], 32, "%.1f", p->position.z);
                        g_propsFieldCount = 3;
                    }
                } else if (g_sel.type == SelType::PICKUP) {
                    auto& pickups = PawnSystem::Instance().GetPickups();
                    for (auto& pk : pickups) {
                        if ((int)pk.id == g_sel.index) {
                            snprintf(g_propsBuf[0], 32, "%.1f", pk.position.x);
                            snprintf(g_propsBuf[1], 32, "%.1f", pk.position.y);
                            snprintf(g_propsBuf[2], 32, "%.1f", pk.position.z);
                            g_propsFieldCount = 3;
                            break;
                        }
                    }
                } else if (g_sel.type == SelType::BRUSH) {
                    auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
                    if (g_sel.index >= 0 && g_sel.index < (int)vols.size()) {
                        auto& v = vols[g_sel.index];
                        Vector3 center = {(v.aabb.min.x + v.aabb.max.x) * 0.5f,
                                          (v.aabb.min.y + v.aabb.max.y) * 0.5f,
                                          (v.aabb.min.z + v.aabb.max.z) * 0.5f};
                        snprintf(g_propsBuf[0], 32, "%.1f", center.x);
                        snprintf(g_propsBuf[1], 32, "%.1f", center.y);
                        snprintf(g_propsBuf[2], 32, "%.1f", center.z);
                        snprintf(g_propsBuf[3], 32, "%.1f", v.aabb.max.x - v.aabb.min.x);
                        snprintf(g_propsBuf[4], 32, "%.1f", v.aabb.max.y - v.aabb.min.y);
                        snprintf(g_propsBuf[5], 32, "%.1f", v.aabb.max.z - v.aabb.min.z);
                        g_propsFieldCount = 6;
                    }
                }
                g_showPropsPanel = true;
                EditorLog("Properties for %s idx=%d", g_sel.name.c_str(), g_sel.index);
            }

            if (action == 2) {
                // Delete
                EditorLog("Deleted %s idx=%d", g_sel.name.c_str(), g_sel.index);
                if (g_sel.type == SelType::NPC)
                    PawnSystem::Instance().Despawn(g_sel.index);
                else if (g_sel.type == SelType::PICKUP)
                    PawnSystem::Instance().RemovePickup(g_sel.index);
                else if (g_sel.type == SelType::BRUSH) {
                    auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
                    if (g_sel.index >= 0 && g_sel.index < (int)vols.size()) {
                        vols.erase(vols.begin() + g_sel.index);
                        OzoneLoader::Instance().RebuildCollisionVolumes();
                        EditorLog("Deleted brush volume idx=%d", g_sel.index);
                    } else {
                        EditorLog("Brush deletion failed: invalid index %d", g_sel.index);
                    }
                }
                g_sel = { SelType::NONE, -1, "", {0,0,0} };
            }

            if (action == 3) {
                // Duplicate
                Vector3 offset = {2.0f, 0, 2.0f};
                EditorLog("Duplicating %s idx=%d", g_sel.name.c_str(), g_sel.index);
                if (g_sel.type == SelType::NPC) {
                    Pawn* p = PawnSystem::Instance().Get(g_sel.index);
                    if (p) {
                        Vector3 newPos = {p->position.x + offset.x, p->position.y + offset.y, p->position.z + offset.z};
                        PawnSystem::Instance().Spawn(newPos, p->defName.c_str());
                    }
                } else if (g_sel.type == SelType::PICKUP) {
                    auto& pickups = PawnSystem::Instance().GetPickups();
                    for (auto& pk : pickups) {
                        if ((int)pk.id == g_sel.index) {
                            PickupNode clone = pk;
                            clone.position.x += offset.x;
                            clone.position.z += offset.z;
                            PawnSystem::Instance().AddPickup(clone);
                            break;
                        }
                    }
                } else if (g_sel.type == SelType::ZONE) {
                    auto& zones = PawnSystem::Instance().GetZones();
                    for (auto& z : zones) {
                        if ((int)z.id == g_sel.index) {
                            ZoneVolumeNode clone = z;
                            clone.bounds.min.x += offset.x;
                            clone.bounds.min.z += offset.z;
                            clone.bounds.max.x += offset.x;
                            clone.bounds.max.z += offset.z;
                            PawnSystem::Instance().AddZone(clone);
                            break;
                        }
                    }
                }
            }
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
                else {
                    std::string fext = entry.path.substr(entry.path.rfind('.'));
                    std::transform(fext.begin(), fext.end(), fext.begin(), ::tolower);
                    if (fext == ".png" || fext == ".jpg" || fext == ".jpeg" || fext == ".bmp" || fext == ".tga")
                        tex = LoadTextureWithFallback(entry.path.c_str());
                }
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
            else {
                std::string fext = entry.path.substr(entry.path.rfind('.'));
                std::transform(fext.begin(), fext.end(), fext.begin(), ::tolower);
                if (fext == ".png" || fext == ".jpg" || fext == ".jpeg" || fext == ".bmp" || fext == ".tga")
                    tex = LoadTextureWithFallback(entry.path.c_str());
            }
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
        if (IsKeyPressed(KEY_HOME)) { SetViewPerspective(); ResetCamera(); }
        if (IsKeyPressed(KEY_PAGE_UP)) CamUp();
        if (IsKeyPressed(KEY_PAGE_DOWN)) CamDown();

        // View preset shortcuts (NumPad)
        if (IsKeyPressed(KEY_KP_7)) {
            // Top view
            Vector3 center = OTEditor.MainCamera.target;
            SetViewPreset({center.x, center.y + 80, center.z + 0.1f}, center, {0, 0, -1});
        }
        if (IsKeyPressed(KEY_KP_1)) {
            // Bottom view
            Vector3 center = OTEditor.MainCamera.target;
            SetViewPreset({center.x, center.y - 80, center.z + 0.1f}, center, {0, 0, 1});
        }
        if (IsKeyPressed(KEY_KP_3)) {
            // Right view
            Vector3 center = OTEditor.MainCamera.target;
            SetViewPreset({center.x + 80, center.y, center.z}, center, {0, 1, 0});
        }
        if (IsKeyPressed(KEY_KP_9)) {
            // Left view
            Vector3 center = OTEditor.MainCamera.target;
            SetViewPreset({center.x - 80, center.y, center.z}, center, {0, 1, 0});
        }
        if (IsKeyPressed(KEY_KP_5)) {
            // Perspective / back to default
            SetViewPerspective();
        }

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
