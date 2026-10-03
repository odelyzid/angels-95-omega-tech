// =============================================================================
// Core/EditorShell.hpp
//
// Preamble: includes, shared editor globals, local types, forward declarations.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

#include "../../../Source/WindowsCompat.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../Editor.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include "../UI/UiPanels.hpp"
#include "../EditorIcons.hpp"
#include "../../../Source/IniConfig.hpp"
#include "../../../Source/World/OzOzoneLoader.hpp"
#include "../../../Source/Pawn/OzPawnSystem.hpp"
#include "../../../Source/Package/Anim/OzAnimFormat.hpp"
#include "../../../Source/Package/PackageAssetLoader.hpp"
#include "../../../Source/Script/LightningEntityRegistry.hpp"
#include "../../../Source/Script/OzlsWriter.hpp"
#include "../../../Source/Audio/SoundManager.hpp"
#include "../../../Source/Physics/OzBsp.hpp"
#include "../../../Source/Renderer/LitLightning.hpp"
#ifdef _WIN32
#include <GL/gl.h>
#endif
#include <algorithm>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
    IDM_UNDO,
    IDM_REDO,
    IDM_EXIT,
    IDM_MODEL_BRW = 1100,
    IDM_SOUND_MGR,
    IDM_TEXTURE_MGR,
    IDM_PAWN_MGR,
    IDM_SCRIPT_MGR,
    IDM_NODE_PANEL,
    IDM_PICKUP_PANEL,
    IDM_HEIGHTMAP,
    IDM_WORLD_GRAPH,
    IDM_LEVEL_LIST,
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
IDM_APPEND_AUTOCONVEX,
IDM_SURFACE_PROPS,
IDM_SURFACE_RESET,
};

// Box budget for one "Append AutoConvex Collision" run. Past this the command
// refuses outright rather than emitting a partial hull - a missing box in a
// collision wall is the exact failure the feature exists to prevent.
static const int kAutoConvexMaxBoxes = 2048;

// Forward declarations
static void EditorLog(const char* fmt, ...);
static void HistoryPush();
static void HistoryUndo();
static void HistoryRedo();
static void HistoryClear();

// WDLModels definition (extern declared in Editor.hpp)
GameModels WDLModels;

// ---------------------------------------------------------------------------
// Entity selection system (hover + click + right-click context menu)
// ---------------------------------------------------------------------------
// SelType itself lives in SelType.hpp because it crosses the Main.cpp <->
// Win32Dialogs.cpp boundary as a raw int on both sides.
#include "../SelType.hpp"
#include "EditorEventBus.hpp"

// Events posted by the Win32 panels during the frame's message pump, drained at
// the single dispatch point below. A file-static buffer rather than a local so
// the drain cannot allocate during dispatch.
static std::vector<ed::Event> g_editorFrameEvents;
// Surface edits are drained in their own pass, at the point the old
// actionApplySurface / actionResetSurface handlers lived. Kept separate from
// g_editorFrameEvents so this batch's handlers can be moved into
// Subsystems/SurfaceOps during R4 without reordering the selection dispatch.
static std::vector<ed::Event> g_editorSurfaceEvents;
// Animation commands, drained and dispatched by ApplyAnimIntents. Separate pass
// for the same reason as the surface one: that function is moving to
// Subsystems/AnimEditing in R4 and should not have to be interleaved with the
// selection dispatch.
static std::vector<ed::Event> g_editorAnimEvents;
// Bumped by any event kind the dispatcher does not handle yet. Should be 0 once
// R2 is complete; a non-zero value means a batch posted an event nobody consumes,
// which is a silently dead click rather than a compile error.
static int g_editorUnhandledEvents = 0;
// Index of a mesh awaiting Ev::ConvertToAnimated. -1 = none. The conversion itself
// runs at its own site in the frame (it needs EnsureMeshNodeLoaded and the Anim
// panel handover), so the dispatcher only records the target.
static int evConvertMeshIndex = -1;

struct EditorSelection {
    SelType type = SelType::NONE;
    int index = -1;
    std::string name;
    Vector3 pos{0,0,0};
    float scale = 1.0f;
    float rotation = 0.0f;
    // True only when `rotation` above was actually populated by the raycast
    // that produced this selection. Most entity types have no yaw concept at
    // all (zones, pickups, lights, portals), so they leave `rotation` at its
    // 0.0f default. The properties panel still seeds a "Rot" row from that
    // default, and the apply handler used to write it straight back - which
    // silently reset the authored `playerstart` yaw to 0 on any Apply. Same
    // trap as scale. Anything that consumes `rotation` on write must gate on
    // this flag.
    bool hasRotation = false;
};
static EditorSelection g_sel;       // left-click selected (red)
static EditorSelection g_hoverSel;  // mouse hover (yellow)

// Right-click state: drag vs click detection
static bool g_rbDown = false;
static Vector2 g_rbDownPos{0,0};
// Left-click state: pick fires on release so a held drag (e.g. moving a
// placement ghost) is not mistaken for a selection click.
static bool g_lbDown = false;
static Vector2 g_lbDownPos{0,0};

// Gizmo drag: moving a SELECTED entity requires an intentional gesture (Move
// tool active, or the press landing on the selection) â€” a plain viewport click
// must never translate it. g_suppressViewportDrag swallows stale mouse input
// for the frame(s) around a native context menu.
static bool g_gizmoDrag = false;
static bool g_gizmoHistPushed = false;
static bool g_suppressViewportDrag = false;
static bool g_terrainHistPushed = false;

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
                    center, r->scale, r->rotation * RAD2DEG, true }; // UI works in degrees
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
            out = { SelType::MODEL, i, TextFormat("Model%d", mid), pos, sx, CachedModels[i].R, true };
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
    auto& zones = ZoneManager::Instance().GetZones();
    Vector3 camPos = OTEditor.MainCamera.position;
    for (auto& z : zones) {
        // Skip zones containing the camera â€” can't select the boundary you're inside
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
    // A light is drawn as a billboard at position + 0.4 Y, so the pick volume
    // has to cover that sprite rather than sitting under it - otherwise the
    // visible marker and the clickable region disagree and aiming feels broken.
    for (auto& l : lights) {
        if (!l.active) continue;
        BoundingBox box = { {l.position.x - 0.6f, l.position.y - 0.1f, l.position.z - 0.6f},
                            {l.position.x + 0.6f, l.position.y + 1.0f, l.position.z + 0.6f} };
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::LIGHT, (int)l.id,
                    l.name.empty() ? "Light" : l.name.c_str(), l.position };
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
            out = { SelType::SPAWN, (int)s.id, "PlayerStart", s.position, 1.0f, s.yaw, true };
        }
    }
    return best;
}

static RayCollision RaycastTestPortals(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& portals = ZoneManager::Instance().GetPortals();
    for (size_t p = 0; p < portals.size(); p++) {
        auto& portal = portals[p];
        RayCollision hit = GetRayCollisionBox(ray, portal.bounds);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::PORTAL, (int)p, portal.targetWorld, {
                (portal.bounds.min.x + portal.bounds.max.x) * 0.5f,
                (portal.bounds.min.y + portal.bounds.max.y) * 0.5f,
                (portal.bounds.min.z + portal.bounds.max.z) * 0.5f
            }};
        }
    }
    return best;
}

static RayCollision RaycastTestMeshObjects(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& objs = PawnSystem::Instance().GetMeshObjects();
    for (auto& n : objs) {
        BoundingBox box;
        Vector3 center = n.position;
        if (n.mesh && n.mesh->Valid()) {
            const BoundingBox& mb = n.mesh->Bounds();
            box.min = {n.position.x + mb.min.x * n.scale,
                       n.position.y + mb.min.y * n.scale,
                       n.position.z + mb.min.z * n.scale};
            box.max = {n.position.x + mb.max.x * n.scale,
                       n.position.y + mb.max.y * n.scale,
                       n.position.z + mb.max.z * n.scale};
            center = {(box.min.x + box.max.x) * 0.5f,
                      (box.min.y + box.max.y) * 0.5f,
                      (box.min.z + box.max.z) * 0.5f};
        } else {
            box.min = {n.position.x - 1.0f, n.position.y - 1.0f, n.position.z - 1.0f};
            box.max = {n.position.x + 1.0f, n.position.y + 1.0f, n.position.z + 1.0f};
        }
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::MESH, (int)n.id,
                    n.skeletal ? "Mesh.Skeletal" : "Mesh.Static",
                    center, n.scale, n.yaw, true };
        }
    }
    return best;
}

static RayCollision RaycastTestParticleEmitters(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& list = PawnSystem::Instance().GetParticleEmitters();
    for (auto& e : list) {
        float r = e.radius + 0.5f;
        BoundingBox box = {{e.position.x - r, e.position.y - r, e.position.z - r},
                           {e.position.x + r, e.position.y + r, e.position.z + r}};
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::PARTICLE, (int)e.id, "ParticleEmitter", e.position, 1.0f, e.yaw, true };
        }
    }
    return best;
}

static RayCollision RaycastTestPathNodes(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& list = PawnSystem::Instance().GetPathNodes();
    for (auto& n : list) {
        float r = n.radius + 0.4f;
        BoundingBox box = {{n.position.x - r, n.position.y - r, n.position.z - r},
                           {n.position.x + r, n.position.y + r, n.position.z + r}};
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            out = { SelType::PATHNODE, (int)n.id, n.name, n.position, 1.0f, 0.0f };
        }
    }
    return best;
}

static RayCollision RaycastTestWindZones(Ray ray, EditorSelection& out) {
    RayCollision best = { false, 1e9f, {0,0,0}, {0,0,0} };
    auto& list = PawnSystem::Instance().GetWindZones();
    for (auto& z : list) {
        RayCollision hit = GetRayCollisionBox(ray, z.bounds);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
            Vector3 center = {(z.bounds.min.x + z.bounds.max.x) * 0.5f,
                              (z.bounds.min.y + z.bounds.max.y) * 0.5f,
                              (z.bounds.min.z + z.bounds.max.z) * 0.5f};
            out = { SelType::WINDZONE, (int)z.id, "WindZone", center, 1.0f, 0.0f };
        }
    }
    return best;
}
