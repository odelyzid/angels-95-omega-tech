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
#include "../../Source/Renderer/LitLightning.hpp"
#ifdef _WIN32
#include <GL/gl.h>
#endif
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
    IDM_WORLD_GRAPH,
    IDM_FULLSCREEN,
    IDM_RESET_CAM,
    IDM_VIEW_TOP,
    IDM_VIEW_BOTTOM,
    IDM_VIEW_RIGHT,
    IDM_VIEW_LEFT,
    IDM_VIEW_PERSPECTIVE,
    IDM_ABOUT,
    // Context menu actions
    IDM_PROPERTIES = 2001,
    IDM_DELETE_ENTITY,
    IDM_DUPLICATE_ENTITY,
    IDM_CANCEL,
    IDM_APPLY_TEXTURE,
};

// Forward declarations
static void EditorLog(const char* fmt, ...);

// WDLModels definition (extern declared in Editor.hpp)
GameModels WDLModels;

// CSG processor — accumulates brush operations for collision geometry
static CsgProcessor g_csgProc;

// ---------------------------------------------------------------------------
// Entity selection system (hover + click + right-click context menu)
// ---------------------------------------------------------------------------
enum class SelType { NONE, BRUSH, MODEL, NPC, PICKUP, LIGHT, ZONE, SPAWN };
struct EditorSelection {
    SelType type = SelType::NONE;
    int index = -1;
    std::string name;
    Vector3 pos{0,0,0};
    float scale = 1.0f;
    float rotation = 0.0f;
};
static EditorSelection g_sel;       // left-click selected (red)
static EditorSelection g_hoverSel;  // mouse hover (yellow)

// Right-click state: drag vs click detection
static bool g_rbDown = false;
static Vector2 g_rbDownPos{0,0};

static RayCollision RaycastTestBrushes(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
    for (size_t i = 0; i < vols.size(); i++) {
        RayCollision hit = GetRayCollisionBox(ray, vols[i].aabb);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::BRUSH, (int)i, "Brush", 
                    {(vols[i].aabb.min.x + vols[i].aabb.max.x)/2,
                     (vols[i].aabb.min.y + vols[i].aabb.max.y)/2,
                     (vols[i].aabb.min.z + vols[i].aabb.max.z)/2} };
        }
    }
    return best;
}

static RayCollision RaycastTestOzPrimitives(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    int count = OzoneLoader::Instance().Count();
    for (int i = 0; i < count; i++) {
        OzoneRenderable* r = OzoneLoader::Instance().Get(i);
        if (!r || !r->loaded || r->model.meshCount == 0) continue;
        BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
        // Apply mesh-local AABB transformed by position + scale (handles non-centered meshes)
        Vector3 wMin = {r->position.x + mb.min.x * r->scale,
                        r->position.y + mb.min.y * r->scale,
                        r->position.z + mb.min.z * r->scale};
        Vector3 wMax = {r->position.x + mb.max.x * r->scale,
                        r->position.y + mb.max.y * r->scale,
                        r->position.z + mb.max.z * r->scale};
        BoundingBox box = {wMin, wMax};
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            // Store ACTUAL mesh center as selection position, not r->position
            Vector3 center = {(wMin.x + wMax.x) * 0.5f, (wMin.y + wMax.y) * 0.5f, (wMin.z + wMax.z) * 0.5f};
            out = { SelType::BRUSH, i, TextFormat("OzPrimitive %d", i),
                    center, r->scale, r->rotation };
        }
    }
    return best;
}

static RayCollision RaycastTestModels(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    for (int i = 0; i < CachedModelCounter; i++) {
        int mid = CachedModels[i].ModelId;
        if (mid <= 0) continue;
        LoadedModel* lm = WDLModels.GetModelByWDLId(mid);
        if (!lm || !lm->loaded || lm->model.meshCount == 0) continue;
        BoundingBox box = GetMeshBoundingBox(lm->model.meshes[0]);
        float sx = CachedModels[i].S;
        Vector3 pos = {CachedModels[i].X, CachedModels[i].Y, CachedModels[i].Z};
        box.min = Vector3Add(Vector3Scale(box.min, sx), pos);
        box.max = Vector3Add(Vector3Scale(box.max, sx), pos);
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::MODEL, i, TextFormat("Model%d", mid), pos, sx, CachedModels[i].R };
        }
    }
    return best;
}

static RayCollision RaycastTestPawns(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& pawns = PawnSystem::Instance().GetPawns();
    for (auto& p : pawns) {
        if (!p.active) continue;
        BoundingBox box = { {p.position.x - 1, p.position.y - 1, p.position.z - 1},
                            {p.position.x + 1, p.position.y + 1, p.position.z + 1} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::NPC, (int)p.id, p.defName, p.position };
        }
    }
    return best;
}

static RayCollision RaycastTestPickups(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& pickups = PawnSystem::Instance().GetPickups();
    for (auto& pk : pickups) {
        if (!pk.active) continue;
        BoundingBox box = { {pk.position.x - 0.6f, pk.position.y - 0.3f, pk.position.z - 0.6f},
                            {pk.position.x + 0.6f, pk.position.y + 0.9f, pk.position.z + 0.6f} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::PICKUP, (int)pk.id, pk.typeName, pk.position };
        }
    }
    return best;
}

static RayCollision RaycastTestZones(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& zones = PawnSystem::Instance().GetZones();
    Vector3 camPos = OTEditor.MainCamera.position;
    for (auto& z : zones) {
        // Skip zones containing the camera — can't select the boundary you're inside
        if (camPos.x >= z.bounds.min.x && camPos.x <= z.bounds.max.x &&
            camPos.y >= z.bounds.min.y && camPos.y <= z.bounds.max.y &&
            camPos.z >= z.bounds.min.z && camPos.z <= z.bounds.max.z)
            continue;
        RayCollision hit = GetRayCollisionBox(ray, z.bounds);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::ZONE, (int)z.id, "ZoneVolume", {
                (z.bounds.min.x + z.bounds.max.x) * 0.5f,
                (z.bounds.min.y + z.bounds.max.y) * 0.5f,
                (z.bounds.min.z + z.bounds.max.z) * 0.5f
            }};
        }
    }
    return best;
}

static RayCollision RaycastTestLights(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& lights = PawnSystem::Instance().GetLights();
    for (auto& l : lights) {
        BoundingBox box = { {l.position.x - 0.5f, l.position.y - 0.3f, l.position.z - 0.5f},
                            {l.position.x + 0.5f, l.position.y + 0.8f, l.position.z + 0.5f} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::LIGHT, (int)l.id, "Light", l.position };
        }
    }
    return best;
}

static RayCollision RaycastTestStarts(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& starts = PawnSystem::Instance().GetPlayerStarts();
    for (auto& s : starts) {
        BoundingBox box = { {s.position.x - 0.6f, s.position.y - 0.3f, s.position.z - 0.6f},
                            {s.position.x + 0.6f, s.position.y + 1.0f, s.position.z + 0.6f} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::SPAWN, (int)s.id, "PlayerStart", s.position };
        }
    }
    return best;
}

static bool EditorRaycastAt(Vector2 mousePos, EditorSelection& out) {
    Ray ray = GetMouseRay(mousePos, OTEditor.MainCamera);
#ifdef DEBUG_EDITOR_TRACE
    EditorLog("MouseAt: x=%.0f y=%.0f)", mousePos.x, mousePos.y);
#endif   
    out = { SelType::NONE, -1, "", {0,0,0} };
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    EditorSelection bestSel = { SelType::NONE, -1, "", {0,0,0} };

    auto testWithSel = [&](RayCollision hit, const EditorSelection& sel, float distMul = 1.0f) {
        // Zones get a distance penalty so solid entities inside them are preferred
        float d = hit.distance * distMul;
        if (hit.hit && d < best.distance) {
            best = hit;
#ifdef DEBUG_EDITOR_TRACE
            EditorLog("bestNormalHit: x=%f y=%f z=%f", (float)best.normal.x, (float)best.normal.y, (float)best.normal.z);
            EditorLog("bestPointHit: x=%f y=%f z=%f", (float)best.point.x, (float)best.point.y, (float)best.point.z);
#endif   
            
            bestSel = sel;
#ifdef DEBUG_EDITOR_TRACE
            EditorLog("bestSelHit: name=%s idx=%d", bestSel.name.c_str(), (int)bestSel.index);
            EditorLog("bestSelHit: x=%f y=%f z=%f", (float)bestSel.pos.x, (float)bestSel.pos.y, (float)bestSel.pos.z);
#endif
        }
    };

    EditorSelection tmp;
    testWithSel(RaycastTestBrushes(ray, tmp), tmp);
    testWithSel(RaycastTestOzPrimitives(ray, tmp), tmp);
    testWithSel(RaycastTestModels(ray, tmp), tmp);
    testWithSel(RaycastTestPawns(ray, tmp), tmp);
    testWithSel(RaycastTestPickups(ray, tmp), tmp);
    testWithSel(RaycastTestLights(ray, tmp), tmp);
    testWithSel(RaycastTestZones(ray, tmp), tmp, 1.2f);
    testWithSel(RaycastTestStarts(ray, tmp), tmp);

    out = bestSel;
    return best.hit;
}

static void SnapGizmoToSelection(const EditorSelection& sel) {
    OmegaTechEditor.X = sel.pos.x;
    OmegaTechEditor.Y = sel.pos.y;
    OmegaTechEditor.Z = sel.pos.z;
    OmegaTechEditor.S = sel.scale > 0.01f ? sel.scale : 1.0f;
    OmegaTechEditor.R = sel.rotation;
    if (sel.type == SelType::BRUSH) {
        auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
        if (sel.index >= 0 && sel.index < (int)vols.size()) {
            OmegaTechEditor.W = vols[sel.index].aabb.max.x - vols[sel.index].aabb.min.x;
            OmegaTechEditor.H = vols[sel.index].aabb.max.y - vols[sel.index].aabb.min.y;
            OmegaTechEditor.L = vols[sel.index].aabb.max.z - vols[sel.index].aabb.min.z;
        }
    } else if (sel.type == SelType::ZONE) {
        auto& zones = PawnSystem::Instance().GetZones();
        for (auto& z : zones) {
            if ((int)z.id == sel.index) {
                OmegaTechEditor.W = z.bounds.max.x - z.bounds.min.x;
                OmegaTechEditor.H = z.bounds.max.y - z.bounds.min.y;
                OmegaTechEditor.L = z.bounds.max.z - z.bounds.min.z;
                break;
            }
        }
    }
    EditorLog("Gizmo snapped to %s idx=%d", sel.name.c_str(), sel.index);
}

static void EditorPickEntity() {
    Vector2 mousePos = GetMousePosition();
    EditorSelection prevSel = g_sel;

    if (EditorRaycastAt(mousePos, g_sel)) {
        // Toggle: clicking the same entity deselects
        if (g_sel.type == prevSel.type && g_sel.index == prevSel.index) {
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = false;
            return;
        }
        // Clicking a different zone while one is selected = deselect
        if (prevSel.type != SelType::NONE && g_sel.type == SelType::ZONE) {
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = false;
            return;
        }
        EditorLog("Selected: %s (type=%d idx=%d x=%f y=%f z=%f)",
                  g_sel.name.c_str(), (int)g_sel.type, g_sel.index, (float)g_sel.pos.x, (float)g_sel.pos.y, (float)g_sel.pos.z);
        OmegaTechEditor.DrawModel = true;
        SnapGizmoToSelection(g_sel);
    } else {
        OmegaTechEditor.DrawModel = false;
    }
}

static void EditorHoverEntity() {
    Vector2 mousePos = GetMousePosition();
    EditorRaycastAt(mousePos, g_hoverSel);
}


// ---------------------------------------------------------------------------
// Entity action functions (called from native context menu + WorldGraph)
// ---------------------------------------------------------------------------
static void DeleteSelectedEntity() {
    if (g_sel.type == SelType::NONE) return;
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
        }
    } else if (g_sel.type == SelType::LIGHT) {
        PawnSystem::Instance().RemoveLight(g_sel.index);
    } else if (g_sel.type == SelType::ZONE) {
        PawnSystem::Instance().RemoveZone(g_sel.index);
    } else if (g_sel.type == SelType::SPAWN) {
        PawnSystem::Instance().RemovePlayerStart(g_sel.index);
    } else if (g_sel.type == SelType::MODEL && g_sel.index >= 0 && g_sel.index < CachedModelCounter) {
        int mid = CachedModels[g_sel.index].ModelId;
        wstring line = L"Model" + to_wstring(mid) + L":" +
            to_wstring(CachedModels[g_sel.index].X) + L":" +
            to_wstring(CachedModels[g_sel.index].Y) + L":" +
            to_wstring(CachedModels[g_sel.index].Z) + L":" +
            to_wstring(CachedModels[g_sel.index].S) + L":" +
            to_wstring(CachedModels[g_sel.index].R) + L":";
        size_t pos = OTEditor.WorldData.find(line);
        if (pos != wstring::npos) {
            OTEditor.WorldData.erase(pos, line.size());
            CacheWDL();
        }
    }
    g_sel = { SelType::NONE, -1, "", {0,0,0} };
}

static void DuplicateSelectedEntity() {
    if (g_sel.type == SelType::NONE) return;
    Vector3 offset = {2.0f, 0, 2.0f};
    EditorLog("Duplicating %s idx=%d", g_sel.name.c_str(), g_sel.index);
    if (g_sel.type == SelType::NPC) {
        Pawn* p = PawnSystem::Instance().Get(g_sel.index);
        if (p) PawnSystem::Instance().Spawn({p->position.x+offset.x, p->position.y+offset.y, p->position.z+offset.z}, p->defName.c_str());
    } else if (g_sel.type == SelType::PICKUP) {
        auto& pickups = PawnSystem::Instance().GetPickups();
        for (auto& pk : pickups) {
            if ((int)pk.id == g_sel.index) {
                PickupNode clone = pk;
                clone.position.x += offset.x; clone.position.z += offset.z;
                PawnSystem::Instance().AddPickup(clone);
                break;
            }
        }
    } else if (g_sel.type == SelType::ZONE) {
        auto& zones = PawnSystem::Instance().GetZones();
        for (auto& z : zones) {
            if ((int)z.id == g_sel.index) {
                ZoneVolumeNode clone = z;
                clone.bounds.min.x += offset.x; clone.bounds.min.z += offset.z;
                clone.bounds.max.x += offset.x; clone.bounds.max.z += offset.z;
                PawnSystem::Instance().AddZone(clone);
                break;
            }
        }
    } else if (g_sel.type == SelType::BRUSH) {
        auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
        if (g_sel.index >= 0 && g_sel.index < (int)vols.size()) {
            OzoneCollisionVolume clone = vols[g_sel.index];
            clone.aabb.min.x += offset.x; clone.aabb.min.z += offset.z;
            clone.aabb.max.x += offset.x; clone.aabb.max.z += offset.z;
            vols.push_back(clone);
            OzoneLoader::Instance().RebuildCollisionVolumes();
        }
    } else if (g_sel.type == SelType::LIGHT) {
        LightNode* l = PawnSystem::Instance().GetLight(g_sel.index);
        if (l) {
            LightNode clone = *l;
            clone.position.x += offset.x; clone.position.z += offset.z;
            PawnSystem::Instance().AddLight(clone);
        }
    } else if (g_sel.type == SelType::SPAWN) {
        auto& starts = PawnSystem::Instance().GetPlayerStarts();
        for (auto& s : starts) {
            if ((int)s.id == g_sel.index) {
                PlayerStartNode clone = s;
                clone.position.x += offset.x; clone.position.z += offset.z;
                PawnSystem::Instance().AddPlayerStart(clone);
                break;
            }
        }
    } else if (g_sel.type == SelType::MODEL && g_sel.index >= 0 && g_sel.index < CachedModelCounter) {
        int mid = CachedModels[g_sel.index].ModelId;
        float nx = CachedModels[g_sel.index].X + offset.x;
        float nz = CachedModels[g_sel.index].Z + offset.z;
        wstring newLine = L"Model" + to_wstring(mid) + L":" +
            to_wstring(nx) + L":" +
            to_wstring(CachedModels[g_sel.index].Y) + L":" +
            to_wstring(nz) + L":" +
            to_wstring(CachedModels[g_sel.index].S) + L":" +
            to_wstring(CachedModels[g_sel.index].R) + L":";
        OTEditor.WorldData += newLine;
        CacheWDL();
    }
}

static void OpenPropertiesForSelection() {
    if (g_sel.type == SelType::NONE) return;
    // Forward selection to the native Properties panel
    g_editorPanels.propsTargetType = (int)g_sel.type;
    g_editorPanels.propsTargetIndex = g_sel.index;
    g_editorPanels.propsTargetName = g_sel.name;
    g_editorPanels.propsTargetPos[0] = g_sel.pos.x;
    g_editorPanels.propsTargetPos[1] = g_sel.pos.y;
    g_editorPanels.propsTargetPos[2] = g_sel.pos.z;
    g_editorPanels.propsTargetScale = g_sel.scale;
    g_editorPanels.propsTargetRotation = g_sel.rotation;
    ShowPropertiesPanel(true);
    EditorLog("Properties for %s idx=%d", g_sel.name.c_str(), g_sel.index);
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
static void ToggleWorldGraph() { ShowWorldGraph(!g_editorPanels.showWorldGraph); }
static void ToggleCollision()   { CollisionToggle = !CollisionToggle; }
static void ResetCamera()       { OTEditor.MainCamera.position = {0, 10, 0}; OTEditor.MainCamera.target = {0, 0, 0}; OTEditor.MainCamera.up = {0, 1, 0}; }
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

static const char* LegacyPickupType(int idx) {
    static const char* map[] = {"HealthVial","ManaVial","EnergyCrystal","Key","Coin","Powerup"};
    return (idx >= 0 && idx < 6) ? map[idx] : nullptr;
}

static void SyncWDLNodes() {
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
                int legacyIdx = std::stoi(node.typeName);
                const char* mapped = LegacyPickupType(legacyIdx);
                if (mapped) node.typeName = mapped;
            } catch (...) {
                // already a string name, use as-is
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

// ---------------------------------------------------------------------------
// Accessors for WorldGraph panel (called from Win32Dialogs.cpp)
// ---------------------------------------------------------------------------
int WorldGraph_GetModelCount() { return CachedModelCounter; }
void WorldGraph_GetModelData(int index, float& outX, float& outY, float& outZ, float& outR, float& outS) {
    if (index >= 0 && index < CachedModelCounter) {
        outX = CachedModels[index].X; outY = CachedModels[index].Y; outZ = CachedModels[index].Z;
        outR = CachedModels[index].R; outS = CachedModels[index].S;
    }
}
const char* WorldGraph_GetModelName(int index) {
    if (index < 0 || index >= CachedModelCounter) return nullptr;
    int mid = CachedModels[index].ModelId;
    LoadedModel* lm = WDLModels.GetModelByWDLId(mid);
    return lm ? lm->name.c_str() : TextFormat("Model%d", mid);
}

// ---------------------------------------------------------------------------
// Native Win32 Menu Bar — window subclass intercepts WM_COMMAND from menus
// ---------------------------------------------------------------------------
static WNDPROC g_originalWndProc = nullptr;

static LRESULT CALLBACK EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        switch (id) {
            case IDM_NEW:           FileNew(); return 0;
            case IDM_OPEN:          { std::string p; if (ChooseOpenWorldFile(p)) g_pendingOpenPath = fs::path(p); } return 0;
            case IDM_SAVE:          if (!g_documentPath.empty()) SaveWorldDocument(g_documentPath); return 0;
            case IDM_SAVE_AS:       FileSaveAs(); return 0;
            case IDM_PLAY_TEST:     { std::string tempPath = "System/Cache/editor_test.wdl";
                std::wstring wstr = OTEditor.WorldData; std::string wd(wstr.begin(), wstr.end());
                std::ofstream f(tempPath); if (f.is_open()) { f << wd; f.close(); system(("start \"\" Angels95.exe --world " + tempPath).c_str()); } } return 0;
            case IDM_EXIT:          CloseWindow(); return 0;
            case IDM_MODEL_BRW:     ToggleModelBrowser(); return 0;
            case IDM_SOUND_MGR:     ToggleSoundMgr(); return 0;
            case IDM_TEXTURE_MGR:   ToggleTextureMgr(); return 0;
            case IDM_PAWN_MGR:      TogglePawnMgr(); return 0;
            case IDM_SCRIPT_MGR:    ToggleScriptMgr(); return 0;
            case IDM_ZONE_PROPS:    ToggleEnvPanel(); return 0;
            case IDM_NODE_PANEL:    ToggleNodePanel(); return 0;
            case IDM_PICKUP_PANEL:  TogglePickupPanel(); return 0;
            case IDM_LIGHT_PROPS:   ShowLightProps(true); return 0;
            case IDM_HEIGHTMAP:     ToggleHeightmapEditor(); return 0;
            case IDM_FULLSCREEN:    ToggleFullscreen(); return 0;
            case IDM_RESET_CAM:     SetViewPerspective(); ResetCamera(); return 0;
            case IDM_VIEW_TOP:      { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x,c.y+80,c.z+0.1f},c,{0,0,-1}); } return 0;
            case IDM_VIEW_BOTTOM:   { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x,c.y-80,c.z+0.1f},c,{0,0,1}); } return 0;
            case IDM_VIEW_RIGHT:    { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x+80,c.y,c.z},c,{0,1,0}); } return 0;
            case IDM_VIEW_LEFT:     { Vector3 c = OTEditor.MainCamera.target; SetViewPreset({c.x-80,c.y,c.z},c,{0,1,0}); } return 0;
            case IDM_VIEW_PERSPECTIVE: SetViewPerspective(); return 0;
            case IDM_WORLD_GRAPH:   ToggleWorldGraph(); return 0;
            case IDM_ABOUT:         MessageBoxA(NULL, "AngelEd v1.0\nOzWorld Editor\nBased on OmegaTech\nTribeWarez 2026", "About AngelEd", MB_OK | MB_ICONINFORMATION); return 0;
            // Context menu actions
            case IDM_PROPERTIES:    OpenPropertiesForSelection(); return 0;
            case IDM_DELETE_ENTITY: DeleteSelectedEntity(); return 0;
            case IDM_DUPLICATE_ENTITY: DuplicateSelectedEntity(); return 0;
            case IDM_APPLY_TEXTURE:
                g_editorPanels.actionApplyTextureToSel = true;
                return 0;
            case IDM_CANCEL:        return 0;
        }
    }
    return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
}

static void CreateEditorMenuBar() {
#ifdef _WIN32
    HWND hWnd = (HWND)GetWindowHandle();
    if (!hWnd) return;

    // Subclass the raylib/GLFW window so we can intercept menu WM_COMMAND
    g_originalWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)EditorWndProc);

    HMENU hMenu = CreateMenu();
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
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, IDM_WORLD_GRAPH, "&World Graph Explorer");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "&View");

    HMENU hCam = CreatePopupMenu();
    AppendMenuA(hCam, MF_STRING, IDM_RESET_CAM, "&Reset Camera\tHome");
    AppendMenuA(hCam, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_TOP, "&Top\tNumpad 7");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_BOTTOM, "&Bottom\tNumpad 1");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_RIGHT, "&Right\tNumpad 3");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_LEFT, "&Left\tNumpad 9");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_PERSPECTIVE, "&Perspective\tNumpad 5");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hCam, "&Camera");

    HMENU hSettings = CreatePopupMenu();
    AppendMenuA(hSettings, MF_STRING, IDM_FULLSCREEN, "Toggle &Fullscreen\tF11");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSettings, "&Settings");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuA(hHelp, MF_STRING, IDM_ABOUT, "&About AngelEd");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");

    SetMenu(hWnd, hMenu);
#endif
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
    SetConfigFlags(FLAG_VSYNC_HINT);
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

    // Initialize LightningScript entity registry (loads .ozls pickup defs)
    LightningEntityRegistry::Instance().Init();

    // Initialize engine/item texture mapper (must be before EngineBillboard::Init)
    AssetMapper::Instance().Init();

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
    SetTraceLogLevel(LOG_ERROR);

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

        // Viewport bounds check — all raycasts only fire when mouse is inside 3D viewport
        Vector2 _mp = GetMousePosition();
        bool _inViewport = (_mp.x >= (float)GetStatsSidebarWidth() && _mp.y >= 28.0f);

        // Hover raycast (throttled every 4 frames for performance)
        {
            static int g_hoverFrameCounter = 0;
            g_hoverFrameCounter++;
            if (g_hoverFrameCounter >= 4) {
                g_hoverFrameCounter = 0;
                if (_inViewport && !OmegaTechEditor.DrawModel) {
                    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
                    EditorHoverEntity();
                } else {
                    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
                }
            }
        }

        // Left-click: select entity (red highlight)
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !OmegaTechEditor.DrawModel) {
            Vector2 mp = GetMousePosition();
            if (mp.x >= (float)GetStatsSidebarWidth() && mp.y >= 28.0f)
                EditorPickEntity();
        }

        // Right-click: drag resizes placement ghost; click picks entity + native context menu
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            g_rbDown = true;
            g_rbDownPos = GetMousePosition();
            if (LastClickTime != 0) DoubleClick = true;
            else LastClickTime = 1;
        }
        // Only apply drag threshold when in placement mode (DrawModel);
        // otherwise any right-click is a context menu click regardless of tiny movement.
        if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && g_rbDown && OmegaTechEditor.DrawModel) {
            Vector2 delta = GetMouseDelta();
            if (fabsf(delta.x) > 3.0f || fabsf(delta.y) > 3.0f) g_rbDown = false;
        }
        if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON) && g_rbDown) {
            g_rbDown = false;
            // Re-check viewport bounds with fresh cursor position
            Vector2 _mp_rel = GetMousePosition();
            bool _inVpRel = (_mp_rel.x >= (float)GetStatsSidebarWidth() && _mp_rel.y >= 28.0f);
            if (_inVpRel && !OmegaTechEditor.DrawModel) {
                EditorPickEntity();
                if (g_sel.type != SelType::NONE) {
                    // Native Win32 context menu with TPM_RETURNCMD (avoids WM_COMMAND routing issues)
                    #ifdef _WIN32
                    HWND hWnd = (HWND)GetWindowHandle();
                    if (hWnd) {
                        HMENU hMenu = CreatePopupMenu();
                        AppendMenuA(hMenu, MF_STRING, IDM_PROPERTIES, "Properties");
                        AppendMenuA(hMenu, MF_STRING, IDM_DELETE_ENTITY, "Delete");
                        AppendMenuA(hMenu, MF_STRING, IDM_DUPLICATE_ENTITY, "Duplicate");
                        if (!g_editorPanels.activeTexturePath.empty() &&
                            (g_sel.type == SelType::BRUSH || g_sel.type == SelType::MODEL)) {
                            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                            AppendMenuA(hMenu, MF_STRING, IDM_APPLY_TEXTURE, "Apply Texture to Surface");
                        }
                        POINT pt;
                        GetCursorPos(&pt);
                        SetForegroundWindow(hWnd);
                        int cmd = TrackPopupMenu(hMenu,
                            TPM_RIGHTBUTTON | TPM_RETURNCMD,
                            pt.x, pt.y, 0, hWnd, NULL);
                        DestroyMenu(hMenu);
                        // Handle the returned command directly
                        if (cmd == IDM_PROPERTIES) OpenPropertiesForSelection();
                        else if (cmd == IDM_DELETE_ENTITY) DeleteSelectedEntity();
                        else if (cmd == IDM_DUPLICATE_ENTITY) DuplicateSelectedEntity();
                        else if (cmd == IDM_APPLY_TEXTURE) g_editorPanels.actionApplyTextureToSel = true;
                    }
                    #endif
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

        // Submit lights and viewPos for Lit mode
        if (OTEditor.ViewMode == LightingMode::LIT && OTEditor.LitFogShader.id > 0) {
            auto& pawnLights = PawnSystem::Instance().GetLights();
            float dt = GetFrameTime();
            LitLightning_Update(pawnLights, OTEditor.LitFogShader, OTEditor.MainCamera, dt);
            if (OTEditor.ViewPosLoc >= 0) {
                Vector3 camPos = OTEditor.MainCamera.position;
                SetShaderValue(OTEditor.LitFogShader, OTEditor.ViewPosLoc, &camPos, SHADER_UNIFORM_VEC3);
            }
        }

        ClearBackground(BLACK);

        // Apply lighting mode before 3D rendering
        rlDrawRenderBatchActive();
        if (OTEditor.ViewMode == LightingMode::WIREFRAME) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDisable(GL_CULL_FACE);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_CULL_FACE);
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

        // Pawn system (use lit shader for billboards when in lit/unlit mode)
        {
            Shader bbShader = {0};
            if (OTEditor.ViewMode == LightingMode::LIT) bbShader = OTEditor.LitFogShader;
            else if (OTEditor.ViewMode == LightingMode::UNLIT) bbShader = OTEditor.UnlitShader;
            PawnSystem::Instance().DrawAll(OTEditor.MainCamera, bbShader);
            PawnSystem::Instance().DrawEntities(OTEditor.MainCamera, bbShader);
        }

        // Zone volume wireframes
        {
            auto& zones = PawnSystem::Instance().GetZones();
            for (auto& z : zones) {
                Color wireColor;
                switch (z.zoneType) {
                    case ZoneType::ZONE_LADDER: wireColor = (Color){180, 120, 0, 80};   break;
                    case ZoneType::ZONE_SKY:    wireColor = (Color){100, 150, 255, 80};  break;
                    case ZoneType::ZONE_REVERB: wireColor = (Color){150, 50, 200, 80};   break;
                    case ZoneType::ZONE_GAMEPLAY_SOUND: wireColor = (Color){50, 200, 50, 80}; break;
                    default:                    wireColor = (Color){50, 120, 200, 80};   break;
                }
                DrawBoundingBox(z.bounds, wireColor);
            }
        }

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
                // Draw pickup preview from model (fallback to colored cube)
                static Model g_pickupPreviewModel = {0};
                static std::string g_pickupPreviewName;
                if (g_pickupPreviewName != OmegaTechEditor.ActivePickupName) {
                    if (g_pickupPreviewModel.meshes) UnloadModel(g_pickupPreviewModel);
                    g_pickupPreviewModel = {0};
                    g_pickupPreviewName = OmegaTechEditor.ActivePickupName;
                    const EntityDef* edef = LightningEntityRegistry::Instance().Find(OmegaTechEditor.ActivePickupName);
                    if (edef && !edef->mesh.empty()) {
                        g_pickupPreviewModel = LoadModelWithFallback(edef->mesh.c_str());
                        if (g_pickupPreviewModel.meshes && !edef->texture.empty()) {
                            Texture2D tex = LoadTextureWithFallback(edef->texture.c_str());
                            if (tex.id > 0)
                                g_pickupPreviewModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
                        }
                    }
                }
                if (g_pickupPreviewModel.meshes) {
                    DrawModelEx(g_pickupPreviewModel, {px, py, pz}, {0, pr, 0}, pr, {ps, ps, ps}, WHITE);
                } else {
                    Color c = GREEN;
                    const EntityDef* edef = LightningEntityRegistry::Instance().Find(OmegaTechEditor.ActivePickupName);
                    if (edef) {
                        auto it = edef->stats.vec3s.find("preview_color");
                        if (it != edef->stats.vec3s.end()) {
                            c.r = (unsigned char)(it->second[0] * 255.0f);
                            c.g = (unsigned char)(it->second[1] * 255.0f);
                            c.b = (unsigned char)(it->second[2] * 255.0f);
                        }
                    }
                    DrawCube({px, py + 0.5f, pz}, 0.6f, 0.8f, 0.6f, c);
                    DrawCubeWires({px, py + 0.5f, pz}, 0.6f, 0.8f, 0.6f, (Color){c.r,c.g,c.b,80});
                }
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

            // Sync gizmo position back to selected entity (for manipulation)
            if (g_sel.type != SelType::NONE) {
                Vector3 newPos = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                int idx = g_sel.index;
                if (g_sel.type == SelType::NPC) {
                    Pawn* p = PawnSystem::Instance().Get(idx);
                    if (p) p->position = newPos;
                } else if (g_sel.type == SelType::PICKUP) {
                    auto& pickups = PawnSystem::Instance().GetPickups();
                    for (auto& pk : pickups) {
                        if ((int)pk.id == idx) { pk.position = newPos; break; }
                    }
                } else if (g_sel.type == SelType::BRUSH) {
                    auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
                    if (idx >= 0 && idx < (int)vols.size()) {
                        Vector3 sz = {vols[idx].aabb.max.x - vols[idx].aabb.min.x,
                                      vols[idx].aabb.max.y - vols[idx].aabb.min.y,
                                      vols[idx].aabb.max.z - vols[idx].aabb.min.z};
                        vols[idx].aabb.min = {newPos.x - sz.x*0.5f, newPos.y - sz.y*0.5f, newPos.z - sz.z*0.5f};
                        vols[idx].aabb.max = {newPos.x + sz.x*0.5f, newPos.y + sz.y*0.5f, newPos.z + sz.z*0.5f};
                        OzoneLoader::Instance().RebuildCollisionVolumes();
                    }
                } else if (g_sel.type == SelType::LIGHT) {
                    LightNode* l = PawnSystem::Instance().GetLight(idx);
                    if (l) l->position = newPos;
                } else if (g_sel.type == SelType::SPAWN) {
                    auto& starts = PawnSystem::Instance().GetPlayerStarts();
                    for (auto& s : starts) {
                        if ((int)s.id == idx) { s.position = newPos; break; }
                    }
                }
                // Update selection stored position
                g_sel.pos = newPos;
            }

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
                    WDLCommand += L"Pickup:" + wstring(OmegaTechEditor.ActivePickupName.begin(), OmegaTechEditor.ActivePickupName.end()) + L":" +
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
                    // Add brush renderable so it's visible in the viewport
                    int primType = EMID - 200;
                    Vector3 center = {OmegaTechEditor.X + OmegaTechEditor.W * 0.5f,
                                      OmegaTechEditor.Y + OmegaTechEditor.H * 0.5f,
                                      OmegaTechEditor.Z + OmegaTechEditor.L * 0.5f};
                    Vector3 size = {OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L};
                    int ridx = OzoneLoader::Instance().AddBrushRenderable(
                        primType, center, size, OmegaTechEditor.R, OmegaTechEditor.S,
                        OmegaTechEditor.CSGOperation);
                    if (ridx >= 0) {
                        EditorLog("Brush renderable added idx=%d prim=%d", ridx, primType);
                        // Auto-apply preselected texture to new brush
                        if (!g_editorPanels.activeTexturePath.empty()) {
                            auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
                            if (!vols.empty()) {
                                vols.back().texPath = g_editorPanels.activeTexturePath;
                                vols.back().texSlot = 1;
                                EditorLog("Auto-applied texture to new brush: %s",
                                          g_editorPanels.activeTexturePath.c_str());
                            }
                        }
                    }
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

        // Helper lambda: get bounding box for a given selection
        auto GetSelBox = [](const EditorSelection& sel) -> BoundingBox {
            BoundingBox box = {{0,0,0},{0,0,0}};
            if (sel.type == SelType::NPC) {
                auto& pawns = PawnSystem::Instance().GetPawns();
                for (auto& p : pawns) {
                    if ((int)p.id == sel.index && p.active)
                        return {{p.position.x-1,p.position.y-1,p.position.z-1},
                                {p.position.x+1,p.position.y+1,p.position.z+1}};
                }
            } else if (sel.type == SelType::PICKUP) {
                auto& pickups = PawnSystem::Instance().GetPickups();
                for (auto& pk : pickups) {
                    if ((int)pk.id == sel.index && pk.active)
                        return {{pk.position.x-0.6f,pk.position.y-0.3f,pk.position.z-0.6f},
                                {pk.position.x+0.6f,pk.position.y+1.2f,pk.position.z+0.6f}};
                }
            } else if (sel.type == SelType::BRUSH) {
                auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
                if (sel.index >= 0 && sel.index < (int)vols.size())
                    return vols[sel.index].aabb;
            } else if (sel.type == SelType::ZONE) {
                auto& zones = PawnSystem::Instance().GetZones();
                for (auto& z : zones) {
                    if ((int)z.id == sel.index)
                        return z.bounds;
                }
            } else if (sel.type == SelType::LIGHT) {
                LightNode* l = PawnSystem::Instance().GetLight(sel.index);
                if (l)
                    return {{l->position.x-0.5f,l->position.y-0.5f,l->position.z-0.5f},
                            {l->position.x+0.5f,l->position.y+1.0f,l->position.z+0.5f}};
            } else if (sel.type == SelType::SPAWN) {
                auto& starts = PawnSystem::Instance().GetPlayerStarts();
                for (auto& s : starts) {
                    if ((int)s.id == sel.index)
                        return {{s.position.x-0.6f,s.position.y-0.5f,s.position.z-0.6f},
                                {s.position.x+0.6f,s.position.y+1.2f,s.position.z+0.6f}};
                }
            } else if (sel.type == SelType::MODEL && sel.index >= 0 && sel.index < CachedModelCounter) {
                int mid = CachedModels[sel.index].ModelId;
                LoadedModel* lm = WDLModels.GetModelByWDLId(mid);
                if (lm && lm->loaded) {
                    BoundingBox mb = GetMeshBoundingBox(lm->model.meshes[0]);
                    float sx = CachedModels[sel.index].S;
                    Vector3 pos = {CachedModels[sel.index].X, CachedModels[sel.index].Y, CachedModels[sel.index].Z};
                    mb.min = Vector3Add(Vector3Scale(mb.min, sx), pos);
                    mb.max = Vector3Add(Vector3Scale(mb.max, sx), pos);
                    return mb;
                }
            }
            return box;
        };

        // Hover highlight (yellow)
        if (g_hoverSel.type != SelType::NONE &&
            !(g_hoverSel.type == g_sel.type && g_hoverSel.index == g_sel.index)) {
            BoundingBox hb = GetSelBox(g_hoverSel);
            if (hb.min.x != hb.max.x || hb.min.y != hb.max.y) {
                float hp = 0.5f + 0.5f * sinf(GetTime() * 4);
                DrawBoundingBox(hb, (Color){255,255,(unsigned char)(100*hp),255});
            }
        }

        // Selection highlight (red)
        if (g_sel.type != SelType::NONE) {
            BoundingBox sb = GetSelBox(g_sel);
            if (sb.min.x != sb.max.x || sb.min.y != sb.max.y) {
                float sp = 0.5f + 0.5f * sinf(GetTime() * 4);
                DrawBoundingBox(sb, (Color){255,(unsigned char)(80*sp),(unsigned char)(80*sp),255});
            }
        }

        // Terrain-following camera
        {
            float gy = SampleHeightmapGroundY(OTEditor.MainCamera.position.x, OTEditor.MainCamera.position.z);
            if (gy > -50000.0f)
                OTEditor.MainCamera.position.y = gy + 2.0f;
        }

        EndMode3D();

        // Reset lighting mode state (always restore defaults)
        rlDrawRenderBatchActive();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_CULL_FACE);

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
                        if (OTEditor.ViewMode == LightingMode::UNLIT && OTEditor.UnlitShader.id > 0) {
                            for (auto& m : WDLModels.models)
                                if (m.loaded && m.model.meshCount > 0)
                                    m.model.materials[0].shader = OTEditor.UnlitShader;
                            if (WDLModels.HeightMapReady)
                                WDLModels.HeightMap.materials[0].shader = OTEditor.UnlitShader;
                            OzoneLoader::Instance().SetLitFogShader(OTEditor.UnlitShader);
                        } else if (prev == LightingMode::UNLIT && OTEditor.LitFogShader.id > 0) {
                            for (auto& m : WDLModels.models)
                                if (m.loaded && m.model.meshCount > 0)
                                    m.model.materials[0].shader = OTEditor.LitFogShader;
                            if (WDLModels.HeightMapReady)
                                WDLModels.HeightMap.materials[0].shader = OTEditor.LitFogShader;
                            OzoneLoader::Instance().SetLitFogShader(OTEditor.LitFogShader);
                        }
                    }
                }
                bx+=lw+2;
            }
            bx+=6;
            tBtn("AddVolume","Zone",5); tBtn("ModeCamera","Node",7);
            tBtn("PolyTexInfo","Pickup",6);
        }

        // Draw the 3D viewport render target (offset by native stats sidebar)
        int sbW = GetStatsSidebarWidth();
        const int tbH = 28;
        LayoutStatsSidebar(GetScreenWidth(), GetScreenHeight(), tbH, sbW);
        {
            const char* modeStr =
                g_placeMode == PlaceMode::MODEL ? "MODEL" :
                g_placeMode == PlaceMode::PICKUP ? "PICKUP" :
                g_placeMode == PlaceMode::NODE ? "NODE" : "ENV";
            int volCount = (int)OzoneLoader::Instance().GetCollisionVolumes().size();
            int chunkCount = OzoneLoader::Instance().GetChunkManager().CellCount();
            UpdateStatsSidebar(
                OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z,
                OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L,
                OmegaTechEditor.R, OmegaTechEditor.S,
                volCount, chunkCount, modeStr,
                OTEditor.MainCamera.position.x,
                OTEditor.MainCamera.position.y,
                OTEditor.MainCamera.position.z);
        }
        DrawTexturePro(Target.texture, (Rectangle){0, 0, (float)Target.texture.width, -(float)Target.texture.height},
                       (Rectangle){(float)sbW, (float)tbH, (float)(GetScreenWidth() - sbW), (float)(GetScreenHeight() - tbH)}, (Vector2){0,0}, 0, WHITE);
        DrawFPS(GetScreenWidth() - 60, 36);

        // WorldGraph selection handler — set g_sel from explorer double-click
        if (g_editorPanels.actionSelectFromGraph >= 0) {
            int idx = g_editorPanels.actionSelectFromGraph;
            SelType selType = (SelType)g_editorPanels.actionSelectFromGraphType;
            // The name and pos were set by the WorldGraph panel
            g_sel.type = selType;
            g_sel.index = idx;
            g_sel.name = g_editorPanels.actionSelectFromGraphName;
            g_sel.pos = {g_editorPanels.actionSelectFromGraphPos[0],
                         g_editorPanels.actionSelectFromGraphPos[1],
                         g_editorPanels.actionSelectFromGraphPos[2]};
            g_editorPanels.actionSelectFromGraph = -1;
            EditorLog("Selected from WorldGraph: %s (type=%d idx=%d)",
                      g_sel.name.c_str(), (int)selType, idx);
            OmegaTechEditor.DrawModel = true;
            SnapGizmoToSelection(g_sel);
        }

        // Properties apply handler — write values back from native panel
        if (g_editorPanels.actionApplyProperties) {
            float px = g_editorPanels.propPosX;
            float py = g_editorPanels.propPosY;
            float pz = g_editorPanels.propPosZ;
            float prot = g_editorPanels.propRotation;
            int tgtIdx = g_editorPanels.propsTargetIndex;
            SelType tgtType = (SelType)g_editorPanels.propsTargetType;

            if (tgtType == SelType::NPC) {
                Pawn* p = PawnSystem::Instance().Get(tgtIdx);
                if (p) p->position = {px, py, pz};
            } else if (tgtType == SelType::PICKUP) {
                auto& pickups = PawnSystem::Instance().GetPickups();
                for (auto& pk : pickups) {
                    if ((int)pk.id == tgtIdx) { pk.position = {px, py, pz}; break; }
                }
            } else if (tgtType == SelType::BRUSH) {
                auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
                if (tgtIdx >= 0 && tgtIdx < (int)vols.size()) {
                    float sx = g_editorPanels.propSizeX;
                    float sy = g_editorPanels.propSizeY;
                    float sz = g_editorPanels.propSizeZ;
                    if (sx < 0.01f) sx = 1.0f;
                    if (sy < 0.01f) sy = 1.0f;
                    if (sz < 0.01f) sz = 1.0f;
                    vols[tgtIdx].aabb.min = {px - sx*0.5f, py - sy*0.5f, pz - sz*0.5f};
                    vols[tgtIdx].aabb.max = {px + sx*0.5f, py + sy*0.5f, pz + sz*0.5f};
                    vols[tgtIdx].texScaleU = g_editorPanels.propTexScaleU;
                    vols[tgtIdx].texScaleV = g_editorPanels.propTexScaleV;
                    vols[tgtIdx].texOffsetU = g_editorPanels.propTexOffsetU;
                    vols[tgtIdx].texOffsetV = g_editorPanels.propTexOffsetV;
                    OzoneLoader::Instance().RebuildCollisionVolumes();
                }
            } else if (tgtType == SelType::LIGHT) {
                LightNode* l = PawnSystem::Instance().GetLight(tgtIdx);
                if (l) l->position = {px, py, pz};
            } else if (tgtType == SelType::ZONE) {
                auto& zones = PawnSystem::Instance().GetZones();
                for (auto& zone : zones) {
                    if ((int)zone.id == tgtIdx) {
                        float szx = g_editorPanels.propSizeX;
                        float szy = g_editorPanels.propSizeY;
                        float szz = g_editorPanels.propSizeZ;
                        zone.bounds.min = {px - szx*0.5f, py - szy*0.5f, pz - szz*0.5f};
                        zone.bounds.max = {px + szx*0.5f, py + szy*0.5f, pz + szz*0.5f};
                        break;
                    }
                }
            } else if (tgtType == SelType::MODEL && tgtIdx >= 0 && tgtIdx < CachedModelCounter) {
                int mid = CachedModels[tgtIdx].ModelId;
                wstring oldLine = L"Model" + to_wstring(mid) + L":" +
                    to_wstring(CachedModels[tgtIdx].X) + L":" +
                    to_wstring(CachedModels[tgtIdx].Y) + L":" +
                    to_wstring(CachedModels[tgtIdx].Z) + L":" +
                    to_wstring(CachedModels[tgtIdx].S) + L":" +
                    to_wstring(CachedModels[tgtIdx].R) + L":";
                wstring newLine = L"Model" + to_wstring(mid) + L":" +
                    to_wstring(px) + L":" + to_wstring(py) + L":" +
                    to_wstring(pz) + L":" +
                    to_wstring(g_editorPanels.propScale) + L":" +
                    to_wstring(prot) + L":";
                size_t pos = OTEditor.WorldData.find(oldLine);
                if (pos != wstring::npos) {
                    OTEditor.WorldData.replace(pos, oldLine.size(), newLine);
                    CacheWDL();
                }
            } else if (tgtType == SelType::SPAWN) {
                auto& starts = PawnSystem::Instance().GetPlayerStarts();
                for (auto& s : starts) {
                    if ((int)s.id == tgtIdx) {
                        s.position = {px, py, pz};
                        s.yaw = prot;
                        break;
                    }
                }
            }
            EditorLog("Applied properties to %s idx=%d", g_sel.name.c_str(), tgtIdx);
            g_editorPanels.actionApplyProperties = false;
        }

        // Apply active texture to selected entity (from context menu)
        if (g_editorPanels.actionApplyTextureToSel) {
            if (!g_editorPanels.activeTexturePath.empty() && g_sel.type != SelType::NONE) {
                if (g_sel.type == SelType::BRUSH) {
                    auto& vols = OzoneLoader::Instance().GetCollisionVolumesMutable();
                    if (g_sel.index >= 0 && g_sel.index < (int)vols.size()) {
                        vols[g_sel.index].texPath = g_editorPanels.activeTexturePath;
                        vols[g_sel.index].texSlot = 1;
                        EditorLog("Applied texture to brush idx=%d: %s", g_sel.index,
                                  g_editorPanels.activeTexturePath.c_str());
                    }
                } else if (g_sel.type == SelType::MODEL && g_sel.index >= 0 && g_sel.index < CachedModelCounter) {
                    int mid = CachedModels[g_sel.index].ModelId;
                    if (ApplyTextureToModel(mid, g_editorPanels.activeTexturePath.c_str()))
                        EditorLog("Applied texture to model %d: %s", mid,
                                  g_editorPanels.activeTexturePath.c_str());
                }
            }
            g_editorPanels.actionApplyTextureToSel = false;
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
            // Look up pickup name from legacy index or registry
            int idx = g_editorPanels.actionPickupType;
            const char* legacy = LegacyPickupType(idx);
            if (legacy) {
                OmegaTechEditor.ActivePickupName = legacy;
            } else {
                std::vector<const EntityDef*> pickups;
                LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickups);
                if (idx >= 0 && idx < (int)pickups.size())
                    OmegaTechEditor.ActivePickupName = pickups[idx]->name;
            }
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

        // Selection shortcuts
        if (IsKeyPressed(KEY_ESCAPE) && g_sel.type != SelType::NONE) {
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = false;
        }
        if (IsKeyPressed(KEY_DELETE) && g_sel.type != SelType::NONE) {
            DeleteSelectedEntity();
        }
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_D) && g_sel.type != SelType::NONE) {
            DuplicateSelectedEntity();
        }

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
