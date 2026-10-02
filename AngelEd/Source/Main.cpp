#include "../../Source/WindowsCompat.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Editor.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include "Win32Dialogs.hpp"
#include "EditorIcons.hpp"
#include "../../Source/IniConfig.hpp"
#include "../../Source/World/OzOzoneLoader.hpp"
#include "../../Source/Pawn/OzPawnSystem.hpp"
#include "../../Source/Package/Anim/OzAnimFormat.hpp"
#include "../../Source/Package/PackageAssetLoader.hpp"
#include "../../Source/Script/LightningEntityRegistry.hpp"
#include "../../Source/Script/OzlsWriter.hpp"
#include "../../Source/Audio/SoundManager.hpp"
#include "../../Source/Physics/OzBsp.hpp"
#include "../../Source/Renderer/LitLightning.hpp"
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
    IDM_ZONE_PROPS,
    IDM_NODE_PANEL,
    IDM_PICKUP_PANEL,
    IDM_LIGHT_PROPS,
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
enum class SelType { NONE, BRUSH, MODEL, NPC, PICKUP, LIGHT, ZONE, SPAWN, PORTAL, MESH, PARTICLE, PATHNODE, WINDZONE };
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
    testWithSel(RaycastTestPortals(ray, tmp), tmp);
    testWithSel(RaycastTestMeshObjects(ray, tmp), tmp);
    testWithSel(RaycastTestParticleEmitters(ray, tmp), tmp);
    testWithSel(RaycastTestPathNodes(ray, tmp), tmp);
    testWithSel(RaycastTestWindZones(ray, tmp), tmp, 1.2f);

    out = bestSel;
    return best.hit;
}

// ---------------------------------------------------------------------------
// Per-face surface picking (UT99-style: right-click a FACE, not a brush)
//
// The brush raycast above only returns the renderable, so Surface Properties
// had no idea which of the six faces was clicked. This intersects the actual
// triangles of the renderable's mesh and derives the face from the dominant
// axis of the hit triangle's normal - the same rule SurfaceMaterial's per-face
// mesh split uses, so the face the user clicks is provably the face whose
// properties the dialog will edit.
//
// Self-contained Moeller-Trumbore rather than raylib's triangle helper, which
// is not part of the public API in every supported raylib version.
// ---------------------------------------------------------------------------
struct SurfaceFacePick {
    bool hit = false;
    int  renderable = -1;
    oz::surface::SurfaceFace face = oz::surface::FACE_NONE;
    Vector3 point{0, 0, 0};
    Vector3 normal{0, 1, 0};
    float  distance = 0.0f;
};

static bool RayTri(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2,
                   float& tOut, Vector3& nOut) {
    const Vector3 e1 = Vector3Subtract(v1, v0);
    const Vector3 e2 = Vector3Subtract(v2, v0);
    const Vector3 p = Vector3CrossProduct(ray.direction, e2);
    const float det = Vector3DotProduct(e1, p);
    if (fabsf(det) < 1e-8f) return false;          // parallel
    const float invDet = 1.0f / det;
    const Vector3 tv = Vector3Subtract(ray.position, v0);
    const float u = Vector3DotProduct(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const Vector3 q = Vector3CrossProduct(tv, e1);
    const float v = Vector3DotProduct(ray.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = Vector3DotProduct(e2, q) * invDet;
    if (t <= 1e-4f) return false;                  // behind the camera
    tOut = t;
    nOut = Vector3Normalize(Vector3CrossProduct(e1, e2));
    return true;
}

static SurfaceFacePick PickSurfaceFace(Vector2 mousePos) {
    SurfaceFacePick best;
    Ray ray = GetMouseRay(mousePos, OTEditor.MainCamera);
    int count = OzoneLoader::Instance().Count();
    for (int i = 0; i < count; i++) {
        OzoneRenderable* r = OzoneLoader::Instance().Get(i);
        if (!r || !r->loaded || r->model.meshCount == 0) continue;
        Mesh& m = r->model.meshes[0];
        if (!m.vertices || !m.indices) continue;
        for (int t = 0; t < m.triangleCount; t++) {
            unsigned short i0 = m.indices[t * 3 + 0];
            unsigned short i1 = m.indices[t * 3 + 1];
            unsigned short i2 = m.indices[t * 3 + 2];
            if ((int)i0 >= m.vertexCount || (int)i1 >= m.vertexCount ||
                (int)i2 >= m.vertexCount) continue;
            Vector3 a, b, c;
            // Local -> world, matching Draw()/DrawSurface(): position + scale,
            // yaw about Y. Picking in local space instead would report the
            // wrong face on any rotated brush.
            float rad = r->rotation;
            float cs = cosf(rad), sn = sinf(rad);
            const float* vs[3] = { m.vertices + (size_t)i0 * 3,
                                   m.vertices + (size_t)i1 * 3,
                                   m.vertices + (size_t)i2 * 3 };
            Vector3 out[3];
            for (int k = 0; k < 3; k++) {
                float lx = vs[k][0] * r->scale;
                float ly = vs[k][1] * r->scale;
                float lz = vs[k][2] * r->scale;
                out[k] = { r->position.x + (lx * cs + lz * sn),
                           r->position.y + ly,
                           r->position.z + (-lx * sn + lz * cs) };
            }
            a = out[0]; b = out[1]; c = out[2];
            float dist = 0.0f; Vector3 n{0, 1, 0};
            if (!RayTri(ray, a, b, c, dist, n)) continue;
            if (best.hit && dist >= best.distance) continue;
            best.hit = true;
            best.renderable = i;
            best.distance = dist;
            best.normal = n;
            best.point = Vector3Add(ray.position, Vector3Scale(ray.direction, dist));
            // The triangle normal is in world space and the buckets are engine
            // Y-up, so no axis swap here.
            best.face = oz::surface::FaceFromNormal(n.x, n.y, n.z);
        }
    }
    return best;
}

// Enter selection adds to the current set instead of replacing it, which is how
// the "(N Selected)" count in the context menu is built up. Plain left-click
// still replaces, so ordinary picking is unchanged.
static std::vector<SurfaceFacePick> g_selectedSurfaces;

static void ToggleSurfacePick(const SurfaceFacePick& sp) {
    for (size_t i = 0; i < g_selectedSurfaces.size(); i++) {
        if (g_selectedSurfaces[i].renderable == sp.renderable &&
            g_selectedSurfaces[i].face == sp.face) {
            g_selectedSurfaces.erase(g_selectedSurfaces.begin() + i);
            return;
        }
    }
    g_selectedSurfaces.push_back(sp);
}

static void ClearSurfacePicks() { g_selectedSurfaces.clear(); }

static int ClampPropInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// PlayerStart lookups are by node id, not vector index. Both the raycast and
// the WorldGraph hand out `s.id`, and ids are assigned from a counter shared
// with every other entity type, so they are neither 0-based nor contiguous per
// type. Indexing the vector by id (as an earlier revision of the properties
// apply handler did) therefore addressed the wrong node - or none.
static PlayerStartNode* FindPlayerStartById(int id) {
    for (auto& s : PawnSystem::Instance().GetPlayerStarts())
        if ((int)s.id == id) return &s;
    return nullptr;
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
        auto& zones = ZoneManager::Instance().GetZones();
        for (auto& z : zones) {
            if ((int)z.id == sel.index) {
                OmegaTechEditor.W = z.bounds.max.x - z.bounds.min.x;
                OmegaTechEditor.H = z.bounds.max.y - z.bounds.min.y;
                OmegaTechEditor.L = z.bounds.max.z - z.bounds.min.z;
                break;
            }
        }
    } else if (sel.type == SelType::PORTAL) {
        auto& portals = ZoneManager::Instance().GetPortals();
        if (sel.index >= 0 && sel.index < (int)portals.size()) {
            auto& p = portals[sel.index];
            OmegaTechEditor.W = p.bounds.max.x - p.bounds.min.x;
            OmegaTechEditor.H = p.bounds.max.y - p.bounds.min.y;
            OmegaTechEditor.L = p.bounds.max.z - p.bounds.min.z;
        }
    }
    EditorLog("Gizmo snapped to %s idx=%d", sel.name.c_str(), sel.index);
}

// toggleOffSame: left-click toggles a repeat pick off (deselect); right-click
// must NOT toggle, otherwise right-clicking the already-selected entity would
// deselect it instead of opening its context menu.
// Returns true only when the raycast actually HIT an entity. Callers that open a
// context menu must gate on this: on a miss the selection is intentionally left
// untouched, so g_sel still names the previous entity and would otherwise target
// a menu at an entity the user did not right-click.
static bool EditorPickEntity(bool toggleOffSame = true) {
    Vector2 mousePos = GetMousePosition();
    EditorSelection prevSel = g_sel;

    if (EditorRaycastAt(mousePos, g_sel)) {
        // Toggle: clicking the same entity deselects
        if (toggleOffSame && g_sel.type == prevSel.type && g_sel.index == prevSel.index) {
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = false;
            return true;
        }
        // Clicking a different zone while one is selected = deselect
        if (toggleOffSame && prevSel.type != SelType::NONE && g_sel.type == SelType::ZONE) {
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = false;
            return true;
        }
        EditorLog("Selected: %s (type=%d idx=%d x=%f y=%f z=%f)",
                  g_sel.name.c_str(), (int)g_sel.type, g_sel.index, (float)g_sel.pos.x, (float)g_sel.pos.y, (float)g_sel.pos.z);
        OmegaTechEditor.DrawModel = true;
        SnapGizmoToSelection(g_sel);
        if (g_sel.type == SelType::PORTAL) {
            SetPortalSelection(g_sel.index);
            g_editorPanels.portalTargetWorld = g_sel.name;
        }
    } else {
        OmegaTechEditor.DrawModel = false;
        return false;
    }
    return true;
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
    HistoryPush();
    EditorLog("Deleted %s idx=%d", g_sel.name.c_str(), g_sel.index);
    if (g_sel.type == SelType::NPC)
        PawnSystem::Instance().Despawn(g_sel.index);
    else if (g_sel.type == SelType::PICKUP)
        PawnSystem::Instance().RemovePickup(g_sel.index);
    else if (g_sel.type == SelType::BRUSH) {
        // First try to remove the renderable (reliable index if from OzPrimitives)
        if (g_sel.index >= 0 && g_sel.index < OzoneLoader::Instance().Count()) {
            OzoneLoader::Instance().RemoveRenderable(g_sel.index);
        } else {
            // Index is a collision volume index â€” find matching renderable
            int rIdx = OzoneLoader::Instance().FindRenderableByCollisionVol(g_sel.index);
            if (rIdx >= 0) OzoneLoader::Instance().RemoveRenderable(rIdx);
        }
        OzoneLoader::Instance().RebuildCollisionVolumes();
    } else if (g_sel.type == SelType::LIGHT) {
        PawnSystem::Instance().RemoveLight(g_sel.index);
    } else if (g_sel.type == SelType::ZONE) {
        ZoneManager::Instance().RemoveZone(g_sel.index);
    } else if (g_sel.type == SelType::SPAWN) {
        PawnSystem::Instance().RemovePlayerStart(g_sel.index);
    } else if (g_sel.type == SelType::PORTAL) {
        ZoneManager::Instance().RemovePortal(g_sel.index);
        RefreshPortalList();
        RefreshLevelList();
    } else if (g_sel.type == SelType::MESH) {
        PawnSystem::Instance().RemoveMeshObject(g_sel.index);
    } else if (g_sel.type == SelType::PARTICLE) {
        PawnSystem::Instance().RemoveParticleEmitter(g_sel.index);
    } else if (g_sel.type == SelType::PATHNODE) {
        PawnSystem::Instance().RemovePathNode(g_sel.index);
    } else if (g_sel.type == SelType::WINDZONE) {
        PawnSystem::Instance().RemoveWindZone(g_sel.index);
    }
    g_sel = { SelType::NONE, -1, "", {0,0,0} };
    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
    OmegaTechEditor.DrawModel = false; // drop the gizmo so picking isn't gated
}

static void DuplicateSelectedEntity() {
    if (g_sel.type == SelType::NONE) return;
    HistoryPush();
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
        auto& zones = ZoneManager::Instance().GetZones();
        for (auto& z : zones) {
            if ((int)z.id == g_sel.index) {
                ZoneVolumeNode clone = z;
                clone.bounds.min.x += offset.x; clone.bounds.min.z += offset.z;
                clone.bounds.max.x += offset.x; clone.bounds.max.z += offset.z;
                ZoneManager::Instance().AddZone(clone);
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
        if (PlayerStartNode* s = FindPlayerStartById(g_sel.index)) {
            PlayerStartNode clone = *s;
            clone.position.x += offset.x; clone.position.z += offset.z;
            PawnSystem::Instance().AddPlayerStart(clone);
        }
    } else if (g_sel.type == SelType::PORTAL) {
        auto& portals = ZoneManager::Instance().GetPortals();
        if (g_sel.index >= 0 && g_sel.index < (int)portals.size()) {
            ZonePortal clone = portals[g_sel.index];
            clone.bounds.min.x += offset.x; clone.bounds.min.z += offset.z;
            clone.bounds.max.x += offset.x; clone.bounds.max.z += offset.z;
            ZoneManager::Instance().AddPortal(clone);
            RefreshLevelList();
        }
    } else if (g_sel.type == SelType::MESH) {
        MeshObjectNode* src = PawnSystem::Instance().GetMeshObject(g_sel.index);
        if (src) {
            MeshObjectNode clone = *src;
            clone.id = 0;
            clone.mesh.reset(); // force re-resolve through MeshCache
            clone.position.x += offset.x;
            clone.position.z += offset.z;
            PawnSystem::Instance().AddMeshObject(clone);
        }
    } else if (g_sel.type == SelType::PARTICLE) {
        ParticleEmitterNode* src = PawnSystem::Instance().GetParticleEmitter(g_sel.index);
        if (src) {
            ParticleEmitterNode clone = *src;
            clone.id = 0;
            clone.position.x += offset.x;
            clone.position.z += offset.z;
            PawnSystem::Instance().AddParticleEmitter(clone);
        }
    } else if (g_sel.type == SelType::PATHNODE) {
        PathNode* src = PawnSystem::Instance().GetPathNode(g_sel.index);
        if (src) {
            static int s_pathDupCounter = 1000;
            PathNode clone = *src;
            clone.id = 0;
            clone.name = "path_" + std::to_string(s_pathDupCounter++); // names must stay unique
            clone.position.x += offset.x;
            clone.position.z += offset.z;
            clone.next.clear(); // duplicates start unlinked
            PawnSystem::Instance().AddPathNode(clone);
        }
    } else if (g_sel.type == SelType::WINDZONE) {
        WindZoneNode* src = PawnSystem::Instance().GetWindZone(g_sel.index);
        if (src) {
            WindZoneNode clone = *src;
            clone.id = 0;
            clone.bounds.min.x += offset.x; clone.bounds.min.z += offset.z;
            clone.bounds.max.x += offset.x; clone.bounds.max.z += offset.z;
            PawnSystem::Instance().AddWindZone(clone);
        }
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
    g_editorPanels.propsTargetHasRotation = g_sel.hasRotation;
    ShowPropertiesPanel(true);
    EditorLog("Properties for %s idx=%d", g_sel.name.c_str(), g_sel.index);
}

// ---------------------------------------------------------------------------
// AppendAutoConvexForSelection
//
// The collision world is AABB-only, so a placed Mesh.Static prop has no
// collision at all and the player walks straight through it. This voxelises the
// selected Mesh or brush into convex boxes and appends them as
// SURF_COLLISION_PROXY brushes (invisible unless the "Collision Bounds" toggle
// is on, but exported to the .ozone so they survive a reload).
//
// The vertex soup is gathered first and handed to AutoConvex as one array, so
// a multi-mesh model is voxelised as a single union rather than producing one
// overlapping proxy set per submesh.
// ---------------------------------------------------------------------------
static void AppendAutoConvexForSelection() {
    // World-space vertex soup for the current selection.
    std::vector<float> soup;
    const char* what = "";

    if (g_sel.type == SelType::MESH) {
        MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_sel.index);
        if (!n || !n->mesh || !n->mesh->Valid()) {
            EditorLog("AutoConvex: mesh has no loaded geometry");
            return;
        }
        what = n->meshPath.c_str();
        Model& mdl = n->mesh->GetModel();
        for (int i = 0; i < mdl.meshCount; i++) {
            Mesh& m = mdl.meshes[i];
            if (!m.vertices || m.vertexCount <= 0) continue;
            soup.reserve(soup.size() + (size_t)m.vertexCount * 3);
            for (int v = 0; v < m.vertexCount; v++) {
                // Bind pose + node transform. Skeletal meshes are voxelised in
                // their rest pose: the per-frame vertex upload happens at draw
                // time, so there is no posed geometry available here.
                float lx = m.vertices[v * 3 + 0] * n->scale;
                float ly = m.vertices[v * 3 + 1] * n->scale;
                float lz = m.vertices[v * 3 + 2] * n->scale;
                float rad = n->yaw * DEG2RAD;
                float cs = cosf(rad), sn = sinf(rad);
                soup.push_back(n->position.x + lx * cs + lz * sn);
                soup.push_back(n->position.y + ly);
                soup.push_back(n->position.z - lx * sn + lz * cs);
            }
        }
    } else if (g_sel.type == SelType::BRUSH) {
        OzoneRenderable* r = OzoneLoader::Instance().Get(g_sel.index);
        if (!r || !r->loaded || r->model.meshCount <= 0 || !r->model.meshes[0].vertices) {
            EditorLog("AutoConvex: brush has no generated mesh");
            return;
        }
        what = "brush";
        Mesh& m = r->model.meshes[0];
        BoundingBox mb = GetMeshBoundingBox(m);
        soup.reserve((size_t)m.vertexCount * 3);
        for (int v = 0; v < m.vertexCount; v++) {
            // Local vertex -> world, matching how Draw()/DrawWorldGeometry
            // place the model (position + scale, yaw about Y).
            float lx = m.vertices[v * 3 + 0];
            float ly = m.vertices[v * 3 + 1];
            float lz = m.vertices[v * 3 + 2];
            float rad = r->rotation;
            float cs = cosf(rad), sn = sinf(rad);
            soup.push_back(r->position.x + (lx * cs + lz * sn) * r->scale);
            soup.push_back(r->position.y + ly * r->scale);
            soup.push_back(r->position.z + (-lx * sn + lz * cs) * r->scale);
        }
    } else {
        return;
    }

    if (soup.size() < 9) {
        EditorLog("AutoConvex: no geometry to voxelise");
        return;
    }

    // Cell size is picked from the selection's own extent so a crate and a
    // castle wall both get a sensible budget instead of one of them exploding
    // into thousands of boxes.
    float cell = 0.5f;
    BoundingBox sel = {};
    if (g_sel.type == SelType::MESH) {
        MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_sel.index);
        if (n && n->mesh && n->mesh->Valid()) {
            const BoundingBox& mb = n->mesh->Bounds();
            sel = {{n->position.x + mb.min.x * n->scale, n->position.y + mb.min.y * n->scale,
                    n->position.z + mb.min.z * n->scale},
                   {n->position.x + mb.max.x * n->scale, n->position.y + mb.max.y * n->scale,
                    n->position.z + mb.max.z * n->scale}};
        }
    } else {
        OzoneRenderable* r = OzoneLoader::Instance().Get(g_sel.index);
        if (r && r->loaded && r->model.meshCount > 0) {
            BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
            sel = {{r->position.x + mb.min.x * r->scale, r->position.y + mb.min.y * r->scale,
                    r->position.z + mb.min.z * r->scale},
                   {r->position.x + mb.max.x * r->scale, r->position.y + mb.max.y * r->scale,
                    r->position.z + mb.max.z * r->scale}};
        }
    }
    float extent = fmaxf(fmaxf(sel.max.x - sel.min.x, sel.max.y - sel.min.y),
                         sel.max.z - sel.min.z);
    if (extent > 0.0f) cell = fmaxf(0.25f, extent / 16.0f);

    HistoryPush();
    int added = OzoneLoader::Instance().AppendAutoConvexCollision(
        soup.data(), (int)soup.size(), cell, kAutoConvexMaxBoxes);

    if (added > 0) {
        // Turn the collision view on so the author sees the proxies land
        // somewhere sensible instead of having to go hunting for the toggle.
        g_editorPanels.showCollisionBounds = true;
        OzoneLoader::Instance().SetDrawCollisionProxies(true);
        EditorLog("AutoConvex: %d proxies appended to '%s' (cell %.2f)", added, what, cell);
    } else {
        EditorLog("AutoConvex: refused for '%s' - over the %d box budget at cell %.2f. "
                  "Use a coarser cell size.", what, kAutoConvexMaxBoxes, cell);
    }
}

// ---------------------------------------------------------------------------
// CommitBrushRenderable
//
// The single place a brush is added to the document. Both the Enter-key ghost
// and the sidebar Solid/Add/Sub/Inter buttons used to open-code this sequence,
// which is how the two could drift; the collision rebuild and the no-effect
// warning below have to happen on both paths or a brush silently arrives with
// no collision.
//
// SUB / DE_RESC / INTERSECT are *modifiers*: CsgProcessor (Source/Physics/
// OzBsp.cpp) subtracts from, or intersects with, the solids already in the
// world. With nothing there to act on, the brush contributes no volume at all
// while its render mesh still draws - which is how a floor authored as `sub`
// looks like a floor in game and drops the player straight through it.
// ---------------------------------------------------------------------------
static int CommitBrushRenderable(int primType, const Vector3& center,
                                 const Vector3& size, float rot, float scale,
                                 int csgOp) {
    auto& loader = OzoneLoader::Instance();
    const int before = (int)loader.GetCollisionVolumes().size();

    int ridx = loader.AddBrushRenderable(primType, center, size, rot, scale, csgOp);

    // Rebuild collision volumes (includes the new brush).
    loader.RebuildCollisionVolumes();

    if (ridx >= 0) {
        const int after = (int)loader.GetCollisionVolumes().size();
        const bool modifier = (csgOp == (int)CsgOp::SUB ||
                               csgOp == (int)CsgOp::DE_RESC ||
                               csgOp == (int)CsgOp::INTERSECT);
        if (modifier && after <= before) {
            static const char* kOpName[] = {"solid", "add", "sub", "intersect", "de-resc"};
            const char* opName = (csgOp >= 0 && csgOp <= 4) ? kOpName[csgOp] : "?";
            // Loud on purpose: the viewport shows a solid-looking brush and the
            // collision count is the only place the failure is visible.
            g_editorPanels.collisionOpWarning = true;
            EditorLog("*** CSG '%s' added NO collision volume (world has %d) ***", opName, after);
            EditorLog("***    '%s' only modifies solids that already exist. "
                      "Add a Solid/Add brush first, or switch the op to Solid. ***", opName);
            #ifdef _WIN32
            MessageBoxA((HWND)GetWindowHandle(),
                        "This CSG operation produced NO collision.\n\n"
                        "Sub / Intersect only modify solids that already exist in the "
                        "world. The brush still renders, so it looks solid in game - "
                        "but the player will fall straight through it.\n\n"
                        "Add a Solid/Add brush first, or switch the operation to Solid.",
                        "AngelEd - no collision generated", MB_OK | MB_ICONWARNING);
            #endif
        } else {
            g_editorPanels.collisionOpWarning = false;
        }
    }
    return ridx;
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

enum class PlaceMode { MODEL, PICKUP, NODE, ENV, TERRAIN };
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
static void ToggleAnimPanel()   { ShowAnimPanel(!g_editorPanels.showAnimPanel); }
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
    OzoneLoader::Instance().Unload();
    auto& pawns = PawnSystem::Instance();
    auto& zones = ZoneManager::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    zones.ClearZones();
    pawns.ClearLights();     // lights otherwise accumulate across loads
    zones.ClearPortals();
    pawns.ClearEmitters();
    pawns.ClearParticleEmitters();
    pawns.ClearPathNodes();
    pawns.ClearWindZones();
    pawns.ClearMeshObjects();
    pawns.ClearSkyZones();
    OmegaTechEditor.DrawModel = false;
}

static const char* LegacyPickupType(int idx) {
    static const char* map[] = {"HealthVial","ManaVial","EnergyCrystal","Key","Coin","Powerup"};
    return (idx >= 0 && idx < 6) ? map[idx] : nullptr;
}

static bool LoadWorldDocument(const fs::path& path) {
    EditorLog("Loading world: %s", path.string().c_str());
    ClearScene();
    HistoryClear();
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
        bool ok = OzoneLoader::Instance().LoadFile(path.string().c_str());
        // The loader only parses; applying the parsed entities to the editor's
        // scene is an explicit step.
        InjectOzoneEntities(OzoneLoader::Instance().GetEntities(),
                            PawnSystem::Instance());
        // The OZONE loader does not populate the editor's level metadata, so a
        // saved levelinfo skybox/particles would be invisible in the viewport.
        // Parse them here into GetLevelMetadata()/SetLevelMetadata().
        auto prims = OzoneParser::parse_file(path.string().c_str());
        for (auto& pr : prims) {
            if (pr.type == OzonePrimitiveType::ENTITY_LEVELINFO) {
                LevelMetadata meta = GetLevelMetadata();
                auto arg = [&](int i) -> float {
                    return (i >= 0 && i < (int)pr.args.size()) ? pr.args[i] : 0.0f;
                };
                meta.gameType = (GameType)(int)arg(0);
                meta.maxPlayers = (int)arg(1);
                meta.respawnTime = arg(2);
                meta.timeLimitEnabled = arg(3) != 0.0f;
                meta.timeLimitMinutes = arg(4);
                meta.scoreLimit = (int)arg(5);
                meta.friendlyFire = arg(6) != 0.0f;
                meta.skyboxTexturePath = pr.entityType;
                SetLevelMetadata(meta);
                EditorLog("OZONE levelinfo: skybox='%s'", meta.skyboxTexturePath.c_str());
            } else if (pr.type == OzonePrimitiveType::ENTITY_PARTICLES) {
                LevelMetadata meta = GetLevelMetadata();
                auto arg = [&](int i) -> float {
                    return (i >= 0 && i < (int)pr.args.size()) ? pr.args[i] : 0.0f;
                };
                meta.particleType = (ParticleType)(int)arg(0);
                meta.particleDensity = arg(1);
                meta.particleSpeed = arg(2);
                meta.particleColorR = (int)arg(3);
                meta.particleColorG = (int)arg(4);
                meta.particleColorB = (int)arg(5);
                meta.particleWindX = arg(6);
                meta.particleWindZ = arg(7);
                SetLevelMetadata(meta);
            }
        }
        return ok;
    }
    // OZONE-only editor: any non-.ozone world format is unsupported.
    EditorLog("Unsupported world format '%s' (OZONE-only editor)", extension.c_str());
    return false;
}

static const char* WDLZoneTypeName(ZoneType t) {
    switch (t) {
        case ZoneType::ZONE_LADDER: return "ladder";
        case ZoneType::ZONE_SKY: return "sky";
        case ZoneType::ZONE_REVERB: return "reverb";
        case ZoneType::ZONE_GAMEPLAY_SOUND: return "sound";
        default: return "water";
    }
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
    for (const auto& zone : ZoneManager::Instance().GetZones())
        output << L"ZoneInfo:" << WDLZoneTypeName(zone.zoneType) << L":"
               << zone.bounds.min.x << L":" << zone.bounds.min.y << L":" << zone.bounds.min.z << L":"
               << zone.bounds.max.x << L":" << zone.bounds.max.y << L":" << zone.bounds.max.z << L":"
               << zone.intensity << L":\n";
    for (const auto& portal : ZoneManager::Instance().GetPortals())
        output << L"Portal:" << std::wstring(portal.targetWorld.begin(), portal.targetWorld.end()) << L":"
               << portal.bounds.min.x << L":" << portal.bounds.min.y << L":" << portal.bounds.min.z << L":"
               << portal.bounds.max.x << L":" << portal.bounds.max.y << L":" << portal.bounds.max.z << L":"
               << portal.targetSpawn.x << L":" << portal.targetSpawn.y << L":" << portal.targetSpawn.z << L":"
               << (portal.bidirectional ? 1 : 0) << L":\n";

    // Level metadata â€” only written when non-default to keep files clean
    LevelMetadata meta = GetLevelMetadata();
    bool metaNonDefault = meta.gameType != GameType::SINGLEPLAYER ||
                          meta.maxPlayers != 8 || meta.respawnTime != 5.0f ||
                          meta.timeLimitEnabled || meta.scoreLimit != 50 ||
                          meta.friendlyFire || !meta.skyboxTexturePath.empty();
    if (metaNonDefault) {
        output << L"LevelInfo:" << (int)meta.gameType << L":" << meta.maxPlayers << L":"
               << meta.respawnTime << L":" << (meta.timeLimitEnabled ? 1 : 0) << L":"
               << meta.timeLimitMinutes << L":" << meta.scoreLimit << L":"
               << (meta.friendlyFire ? 1 : 0) << L":"
               << std::wstring(meta.skyboxTexturePath.begin(), meta.skyboxTexturePath.end()) << L":\n";
    }
    if (meta.particleType != ParticleType::NONE) {
        output << L"Particles:" << (int)meta.particleType << L":" << meta.particleDensity << L":"
               << meta.particleSpeed << L":" << meta.particleColorR << L":" << meta.particleColorG << L":"
               << meta.particleColorB << L":" << meta.particleWindX << L":" << meta.particleWindZ << L":\n";
    }
}

static const char* OzonePrimName(int typeId) {
    switch ((OzonePrimitiveType)typeId) {
        case OzonePrimitiveType::BOX:      return "box";
        case OzonePrimitiveType::CYLINDER: return "cyl";
        case OzonePrimitiveType::SPHERE:   return "sph";
        case OzonePrimitiveType::PYRAMID:  return "pyr";
        case OzonePrimitiveType::PLANE:    return "pln";
        default: return nullptr; // entity types / heightmap
    }
}

static const char* CsgPrefixName(int op) {
    if (op == (int)CsgOp::SUB || op == (int)CsgOp::DE_RESC) return "sub";
    if (op == (int)CsgOp::INTERSECT) return "intersect";
    return "add";
}

// Renderable texture paths are stored resolved (absolute) after loading; convert
// back to a portable, world- or repo-relative path.
static std::string MakeWorldRelativePath(const std::string& p) {
    std::string s = p;
    for (auto& c : s) if (c == '\\') c = '/';
    // Repo-relative is the most portable form (loader accepts GameData/ as-is)
    size_t gd = s.find("GameData/");
    if (gd != std::string::npos) return s.substr(gd);
    // World-relative subdirs
    size_t oz = s.find("oztex/");
    if (oz != std::string::npos) return s.substr(oz);
    size_t wt = s.find("Worlds/");
    if (wt != std::string::npos) {
        size_t w1 = s.find('/', wt + 7); // end of the world folder
        if (w1 != std::string::npos && w1 + 1 < s.size()) return s.substr(w1 + 1);
    }
    return s;
}

static void ExportToOzone(std::ostream& output) {
    // Header
    output << "# OZONE world exported from AngelEd\n";
    output << "# Format: ozone v1.0\n\n";

    // Geometry: export the AUTHORED renderables (original primitives + CSG ops +
    // material kwargs). Exporting post-CSG collision volumes here would re-apply
    // add/sub on already-carved hulls (double subtraction â†’ empty world) and
    // lose textures/csg ops.
    // (OZONE is Z-up: file y/z swapped vs engine coords)
    auto& loader = OzoneLoader::Instance();
    // Skyboxes are render-only rooms, exported before the solid brushes so the
    // shell that surrounds them reads naturally in the file.
    for (int i = 0; i < loader.Count(); i++) {
        OzoneRenderable* r = loader.Get(i);
        if (!r || !r->loaded) continue;
        if (r->typeId != (int)OzonePrimitiveType::SKYBOX) continue;
        // The mesh is unit-sized around its own origin, so recover the authored
        // extent from the bounds rather than the (always 1.0) renderable scale.
        BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
        const float size = (mb.max.x - mb.min.x) * (r->scale > 0.0001f ? r->scale : 1.0f);
        // OZONE is Z-up, engine is Y-up (the same swap the zup lambda below
        // applies to every other entity; spelled out here because this loop runs
        // before that lambda is declared).
        const Vector3 p = { r->position.x, r->position.z, r->position.y };
        output << "skybox " << MakeWorldRelativePath(r->texPath)
               << " " << p.x << " " << p.y << " " << p.z
               << " " << (size > 0.01f ? size : 512.0f);
        if (r->surface.def.panU != 0.0f) output << " panU=" << r->surface.def.panU;
        if (r->surface.def.panV != 0.0f) output << " panV=" << r->surface.def.panV;
        output << "\n";
    }
    for (int i = 0; i < loader.Count(); i++) {
        OzoneRenderable* r = loader.Get(i);
        if (!r || !r->loaded) continue;
        const char* prim = OzonePrimName(r->typeId);
        if (!prim) continue; // entity types / heightmap handled below

        BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
        float sc = (r->scale > 0.0001f) ? r->scale : 1.0f; // mesh bounds are unscaled
        float w = (mb.max.x - mb.min.x) * sc;
        float h = (mb.max.y - mb.min.y) * sc;
        float d = (mb.max.z - mb.min.z) * sc;
        if (w < 0.01f) w = 1.0f;
        if (h < 0.01f) h = 1.0f;
        if (d < 0.01f) d = 1.0f;

        // Cylinder/pyramid meshes sit with their bottom at Y=0 (the loader
        // re-centers them on import) â€” undo the offset so position round-trips.
        Vector3 center = r->position;
        if (r->typeId == (int)OzonePrimitiveType::CYLINDER ||
            r->typeId == (int)OzonePrimitiveType::PYRAMID)
            center.y += h * 0.5f;

        // Engine Y-up â†’ OZONE Z-up
        Vector3 c = {center.x, center.z, center.y};
        float rotDeg = r->rotation * RAD2DEG;

        output << CsgPrefixName(r->csgOp) << " " << prim
               << " " << c.x << " " << c.y << " " << c.z;

        if (r->typeId == (int)OzonePrimitiveType::BOX) {
            // box x y z w h d rot [texSlot]
            output << " " << w << " " << h << " " << d << " " << rotDeg;
            if (r->texSlot > 0) output << " " << r->texSlot;
        } else if (r->typeId == (int)OzonePrimitiveType::CYLINDER) {
            // cyl x y z rTop rBot h slices rot [texSlot]
            float rad = ((w > d) ? w : d) * 0.5f;
            output << " " << rad << " " << rad << " " << h << " 16 " << rotDeg;
            if (r->texSlot > 0) output << " " << r->texSlot;
        } else if (r->typeId == (int)OzonePrimitiveType::SPHERE) {
            // sph x y z r segments
            float rad = ((w > h) ? ((w > d) ? w : d) : ((h > d) ? h : d)) * 0.5f;
            output << " " << rad << " 16";
        } else if (r->typeId == (int)OzonePrimitiveType::PYRAMID) {
            // pyr x y z w d h [texSlot]
            output << " " << w << " " << d << " " << h;
            if (r->texSlot > 0) output << " " << r->texSlot;
        } else { // PLANE â€” orientation is not stored by the loader yet
            output << " 0 1 0 0";
        }

        // Material kwargs (consumed by the OZONE brush parser).
        //
        // texPath / texScale* / texOffset* are now VIEWS onto
        // OzoneRenderable::surface.def (see OzoneParser's DeriveLegacySurface-
        // Fields), so writing them from the legacy fields cannot disagree with
        // the surface block below.
        if (!r->texPath.empty()) {
            std::string tp = MakeWorldRelativePath(r->texPath);
            bool quote = tp.find(' ') != std::string::npos;
            output << " texPath=" << (quote ? "\"" + tp + "\"" : tp);
        }
        if (r->texScaleU != 1.0f || r->texScaleV != 1.0f)
            output << " texScaleU=" << r->texScaleU << " texScaleV=" << r->texScaleV;
        if (r->texOffsetU != 0.0f || r->texOffsetV != 0.0f)
            output << " texOffsetU=" << r->texOffsetU << " texOffsetV=" << r->texOffsetV;

        // Per-face surface properties, as `face<name>_<field>=` kwargs. Only
        // fields the author actually set are emitted, so a face override stays
        // a small readable diff and an untouched face inherits the brush
        // default on reload. The face NAMES are engine Y-up (see
        // World/SurfaceFlags.hpp) - OZONE is Z-up, but surface faces are a
        // renderer concept, not world coordinates, so they are NOT swapped.
        {
            const oz::surface::BrushSurface& bs = r->surface;
            const oz::surface::SurfaceProps& d = bs.def;
            // Brush-wide surface fields the legacy kwargs above do not cover.
            if (d.flags != (uint32_t)r->surfaceFlags)
                output << " flags=" << d.flags;
            if (d.texSlot > 0) output << " surfTexSlot=" << d.texSlot;
            if (!d.texPath.empty() && r->texPath.empty())
                output << " surfTex=" << MakeWorldRelativePath(d.texPath);
            if (d.panU != 0.0f) output << " panU=" << d.panU;
            if (d.panV != 0.0f) output << " panV=" << d.panV;
            if (d.alpha != 1.0f) output << " surfAlpha=" << d.alpha;
            if (d.alphaCutoff != 0.0f) output << " surfCutoff=" << d.alphaCutoff;
            if (d.glowR != 0.0f || d.glowG != 0.0f || d.glowB != 0.0f)
                output << " surfGlow=(" << d.glowR << "," << d.glowG << "," << d.glowB << ")";
            if (d.glowScale != 1.0f) output << " surfGlowScale=" << d.glowScale;

            for (int f = 0; f < oz::surface::FACE_COUNT; f++) {
                if (!bs.IsFaceOverridden((oz::surface::SurfaceFace)f)) continue;
                const std::string pfx =
                    std::string("face") + oz::surface::FaceName((oz::surface::SurfaceFace)f) + "_";
                // Resolve against the brush default so only the DIFFERENCE is
                // written; the parser re-seeds a face from the default, so this
                // round-trips exactly.
                const oz::surface::SurfaceProps& p = bs.Resolve((oz::surface::SurfaceFace)f);
                if (p.flags != d.flags)     output << " " << pfx << "flags=" << p.flags;
                if (p.texSlot != d.texSlot)  output << " " << pfx << "texSlot=" << p.texSlot;
                if (p.texPath != d.texPath)
                    output << " " << pfx << "tex=" << MakeWorldRelativePath(p.texPath);
                if (p.uvScaleU != d.uvScaleU)  output << " " << pfx << "uvScaleU=" << p.uvScaleU;
                if (p.uvScaleV != d.uvScaleV)  output << " " << pfx << "uvScaleV=" << p.uvScaleV;
                if (p.uvOffsetU != d.uvOffsetU) output << " " << pfx << "uvOffsetU=" << p.uvOffsetU;
                if (p.uvOffsetV != d.uvOffsetV) output << " " << pfx << "uvOffsetV=" << p.uvOffsetV;
                if (p.panU != d.panU)   output << " " << pfx << "panU=" << p.panU;
                if (p.panV != d.panV)   output << " " << pfx << "panV=" << p.panV;
                if (p.alpha != d.alpha) output << " " << pfx << "alpha=" << p.alpha;
                if (p.alphaCutoff != d.alphaCutoff)
                    output << " " << pfx << "cutoff=" << p.alphaCutoff;
                if (p.glowR != d.glowR || p.glowG != d.glowG || p.glowB != d.glowB)
                    output << " " << pfx << "glow=(" << p.glowR << "," << p.glowG << "," << p.glowB << ")";
                if (p.glowScale != d.glowScale)
                    output << " " << pfx << "glowScale=" << p.glowScale;
            }
        }
        output << "\n";
    }

    // Heightmap â€” prefer the OZONE loader's terrain (authored in this document),
    // fall back to the legacy WDL heightmap.
    if (loader.HasHeightmap() && !loader.GetHeightmapImagePath().empty()) {
        Vector3 hp = loader.GetHeightmapPosition();
        Vector3 hs = loader.GetHeightmapSize();
        std::string img = loader.GetHeightmapImagePath();
        std::string tex = loader.GetHeightmapTexturePath();
        output << "heightmap " << img << " " << (tex.empty() ? img : tex)
               << " " << hp.x << " " << hp.z << " " << hp.y   // Z-up
               << " " << loader.GetHeightmapScale()
               << " " << hs.x << " " << hs.y << " " << hs.z << "\n";
    } else if (WDLModels.HeightMapReady) {
        Vector3 hp = {WDLModels.HeightMapPosition.x,
                      WDLModels.HeightMapPosition.z,
                      WDLModels.HeightMapPosition.y};
        output << "heightmap Models/HeightMap.png Models/HeightMapTexture.png "
               << hp.x << " " << hp.y << " " << hp.z
               << " " << WDLModels.HeightMapScale
               << " " << WDLModels.HeightMapSize.x << " " << WDLModels.HeightMapSize.y << " " << WDLModels.HeightMapSize.z << "\n";
    }

    output << "\n# Entities\n";

    // OZONE files are Z-up ("x=east, y=north, z=up"); loaders convert to
    // engine Y-up by swapping y/z. Swap here so round-trips are lossless.
    auto& pawns = PawnSystem::Instance();
    auto zup = [](const Vector3& v) { return Vector3{v.x, v.z, v.y}; };

    // Player starts
    for (auto& start : pawns.GetPlayerStarts()) {
        Vector3 p = zup(start.position);
        output << "playerstart " << p.x << " " << p.y << " " << p.z << " " << start.yaw << "\n";
    }

    // Pickups
    for (auto& pickup : pawns.GetPickups()) {
        Vector3 p = zup(pickup.position);
        output << "pickup " << pickup.typeName << " " << p.x << " " << p.y << " " << p.z;
        if (pickup.respawnTime > 0.01f) output << " " << pickup.respawnTime;
        output << "\n";
    }

    // NPCs
    for (auto& pawn : pawns.GetPawns()) {
        if (!pawn.active || pawn.defName.empty()) continue;
        Vector3 p = zup(pawn.position);
        output << "npc " << pawn.defName << " " << p.x << " " << p.y << " " << p.z << "\n";
    }

    // Lights
    //
    // Optional attributes (effect / flare / corona / name) are written as named
    // kwargs, never as trailing positional floats. The positional tail is
    // position-dependent: a light with no effect but a flare emitted
    // `... 16 1 0`, which the loader read back as effect=1 (WATERY) + flare=0.
    // The editor is also the only place these were editable, so the round trip
    // has to be lossless or every save silently retuned the level's lighting.
    for (auto& light : pawns.GetLights()) {
        if (!light.active) continue;
        Vector3 p = zup(light.position);
        Vector3 t = zup(light.target);
        // Color components are unsigned char â€” stream them as integers, never as
        // raw bytes (a raw byte >= 0x80 corrupts the UTF-8 text file and breaks
        // the client's numeric light parser).
        int r = (int)light.color.r, g = (int)light.color.g, b = (int)light.color.b;
        if (light.type == LitLightType::DIRECTIONAL) {
            // directional x y z r g b intensity  (x y z is the SOURCE; the
            // loader aims it at the world origin)
            output << "light directional " << p.x << " " << p.y << " " << p.z
                   << " " << r << " " << g << " " << b << " " << light.intensity;
        } else if (light.type == LitLightType::SPOT) {
            // spot x y z tx ty tz r g b intensity radius innerCone outerCone
            output << "light spot " << p.x << " " << p.y << " " << p.z
                   << " " << t.x << " " << t.y << " " << t.z
                   << " " << r << " " << g << " " << b
                   << " " << light.intensity << " " << light.radius
                   << " " << light.innerCone << " " << light.outerCone;
        } else {
            // point x y z r g b intensity radius
            output << "light point " << p.x << " " << p.y << " " << p.z
                   << " " << r << " " << g << " " << b
                   << " " << light.intensity << " " << light.radius;
        }
        if (light.effect != LitLightEffect::NONE) output << " effect=" << (int)light.effect;
        if (light.flare)  output << " flare=1";
        if (light.corona) output << " corona=1";
        if (!light.name.empty()) {
            std::string ln = light.name;
            bool quote = ln.find(' ') != std::string::npos;
            output << " name=" << (quote ? "\"" + ln + "\"" : ln);
        }
        output << "\n";
    }

    // Zone volumes
    for (auto& zone : ZoneManager::Instance().GetZones()) {
        const char* zt = WDLZoneTypeName(zone.zoneType);
        Vector3 mn = zup(zone.bounds.min);
        Vector3 mx = zup(zone.bounds.max);
        output << "zone " << zt
               << " " << std::min(mn.x, mx.x) << " " << std::min(mn.y, mx.y) << " " << std::min(mn.z, mx.z)
               << " " << std::max(mn.x, mx.x) << " " << std::max(mn.y, mx.y) << " " << std::max(mn.z, mx.z)
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
        // Script-hook name (name= kwarg; consumed by the loader, not an arg index)
        if (!zone.name.empty())
            output << " name=" << zone.name;
        // Per-zone physics overrides (named kwargs; absent = engine defaults)
        auto& ph = zone.physics;
        output << " gravity="   << ph.gravity
               << " jump="      << ph.jumpSpeed
               << " terminal="  << ph.terminalVelocity
               << " water_gravity=" << ph.waterGravity
               << " water_drag="    << ph.waterDrag
               << " swim_up="   << ph.swimUpSpeed
               << " ladder_speed="  << ph.ladderSpeed
               << " fly_mult="  << ph.flySpeedMult;
        output << "\n";
    }

    // Portals (level connections)
    for (auto& portal : ZoneManager::Instance().GetPortals()) {
        Vector3 mn = zup(portal.bounds.min);
        Vector3 mx = zup(portal.bounds.max);
        Vector3 sp = zup(portal.targetSpawn);
        output << "portal " << portal.targetWorld
               << " " << std::min(mn.x, mx.x) << " " << std::min(mn.y, mx.y) << " " << std::min(mn.z, mx.z)
               << " " << std::max(mn.x, mx.x) << " " << std::max(mn.y, mx.y) << " " << std::max(mn.z, mx.z)
               << " " << sp.x << " " << sp.y << " " << sp.z
               << (portal.bidirectional ? " bidir" : "") << "\n";
    }

    // Emitters
    for (auto& emitter : pawns.GetEmitters()) {
        const char* et = (emitter.type == EmitterType::SOUND) ? "sound" : "music";
        Vector3 p = zup(emitter.position);
        output << "emitter " << et << " " << p.x << " " << p.y << " " << p.z << "\n";
    }

    // GameEngine.Mesh.Static / GameEngine.Mesh.Skeletal placed objects
    for (auto& m : pawns.GetMeshObjects()) {
        if (m.meshPath.empty()) continue;
        Vector3 p = zup(m.position);
        output << (m.skeletal ? "Mesh.Skeletal " : "Mesh.Static ")
               << m.meshPath << " " << p.x << " " << p.y << " " << p.z << " " << m.yaw;
        if (m.scale != 1.0f) output << " scale=" << m.scale;
        if (!m.texturePath.empty()) output << " tex=" << m.texturePath;
        if (m.skeletal && !m.animClip.empty()) output << " anim=" << m.animClip;
        if (m.skeletal && !m.animFile.empty()) output << " animfile=" << m.animFile;
        if (m.animSpeed != 1.0f) output << " speed=" << m.animSpeed;
        if (m.windAffected) output << " wind=1";
        output << "\n";
    }

    // GameEngine.ParticleEmitter nodes
    for (auto& e : pawns.GetParticleEmitters()) {
        if (!e.active) continue;
        Vector3 p = zup(e.position);
        Vector3 d = zup(e.direction);
        output << "ParticleEmitter " << (e.type.empty() ? "fire" : e.type)
               << " " << p.x << " " << p.y << " " << p.z
               << " " << e.rate << " " << e.lifetime << " " << e.speed << " " << e.spread
               << " " << e.sizeStart << " " << e.sizeEnd
               << " " << (int)e.colorStart.r << " " << (int)e.colorStart.g << " " << (int)e.colorStart.b
               << " " << (int)e.colorEnd.r << " " << (int)e.colorEnd.g << " " << (int)e.colorEnd.b
               << " " << e.gravity << " " << e.radius
               << " " << d.x << " " << d.y << " " << d.z
               << " " << e.yaw;
        if (!e.texturePath.empty()) output << " tex=" << e.texturePath;
        output << "\n";
    }

    // GameEngine.PathNode waypoints
    for (auto& pn : pawns.GetPathNodes()) {
        Vector3 p = zup(pn.position);
        output << "PathNode " << (pn.name.empty() ? "path" : pn.name)
               << " " << p.x << " " << p.y << " " << p.z;
        if (pn.radius != 1.0f) output << " radius=" << pn.radius;
        if (!pn.next.empty()) {
            output << " next=";
            for (size_t i = 0; i < pn.next.size(); i++) {
                if (i) output << ",";
                output << pn.next[i];
            }
        }
        if (pn.loop) output << " loop";
        output << "\n";
    }

    // WindZone regions
    for (auto& wz : pawns.GetWindZones()) {
        Vector3 mn = zup(wz.bounds.min);
        Vector3 mx = zup(wz.bounds.max);
        Vector3 d = zup(wz.direction);
        output << "WindZone "
               << std::min(mn.x, mx.x) << " " << std::min(mn.y, mx.y) << " " << std::min(mn.z, mx.z)
               << " " << std::max(mn.x, mx.x) << " " << std::max(mn.y, mx.y) << " " << std::max(mn.z, mx.z)
               << " " << d.x << " " << d.y << " " << d.z
               << " " << wz.strength << " " << wz.frequency << "\n";
    }

    // Level metadata
    {
        LevelMetadata meta = GetLevelMetadata();
        bool metaNonDefault = meta.gameType != GameType::SINGLEPLAYER ||
                              meta.maxPlayers != 8 || meta.respawnTime != 5.0f ||
                              meta.timeLimitEnabled || meta.scoreLimit != 50 ||
                              meta.friendlyFire || !meta.skyboxTexturePath.empty();
        if (metaNonDefault) {
            output << "levelinfo " << (int)meta.gameType << " " << meta.maxPlayers << " "
                   << meta.respawnTime << " " << (meta.timeLimitEnabled ? 1 : 0) << " "
                   << meta.timeLimitMinutes << " " << meta.scoreLimit << " "
                   << (meta.friendlyFire ? 1 : 0) << " " << meta.skyboxTexturePath;
            // Append gametype=<key> when the mode is not the default, so the
            // file carries the human-readable name alongside the numeric id.
            // The parser recognises it in the tail; older tools ignore it.
            const char* gtKey = oz::gametype::GameTypeKey(meta.gameType);
            if (gtKey && std::string(gtKey) != "singleplayer")
                output << " gametype=" << gtKey;
            output << "\n";
        }
        if (meta.particleType != ParticleType::NONE) {
            output << "particles " << (int)meta.particleType << " " << meta.particleDensity << " "
                   << meta.particleSpeed << " " << meta.particleColorR << " " << meta.particleColorG << " "
                   << meta.particleColorB << " " << meta.particleWindX << " " << meta.particleWindZ << "\n";
        }
    }

    output << "\n# End of OZONE export\n";
}

static bool SaveWorldDocument(const fs::path& path) {
    // Never overwrite shipped OZWN packages with plain text
    std::string pnorm = path.string();
    for (auto& c : pnorm) if (c == '\\') c = '/';
    if (pnorm.find("System/Data/Zones") != std::string::npos) {
        MessageBoxA(nullptr,
            "This document is a packaged world (System/Data/Zones).\n"
            "Use Save As into GameData/Worlds/<name>/World.ozone to edit it.",
            "Save World", MB_OK | MB_ICONWARNING);
        return false;
    }

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

    return false;
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

// Selection accessors for Win32 dialogs
int Editor_GetSelectedType() { return (int)g_sel.type; }
int Editor_GetSelectedIndex() { return g_sel.index; }
std::string Editor_GetCurrentWorldName() {
    if (g_documentPath.empty()) return "";
    fs::path parent = g_documentPath.parent_path();
    return parent.filename().string();
}
std::string Editor_GetCurrentWorldDir() {
    if (g_documentPath.empty()) return "";
    return g_documentPath.parent_path().string();
}
int Editor_GetCsgOperation() { return OmegaTechEditor.CSGOperation; }
void Editor_SetCsgOperation(int op) { OmegaTechEditor.CSGOperation = op; }
int Editor_GetPlaceMode() { return (int)g_placeMode; }
void Editor_SetPlaceMode(int mode) { g_placeMode = (PlaceMode)mode; }

// ---------------------------------------------------------------------------
// Native Win32 Menu Bar â€” window subclass intercepts WM_COMMAND from menus
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
            case IDM_PLAY_TEST: {
                // Compile the current document to disk first so playtest shows
                // exactly what the editor sees (and never launches a stale file).
                if (g_documentPath.empty()) {
                    FileSaveAs();
                    if (g_documentPath.empty()) return 0; // user cancelled
                }
                if (!SaveWorldDocument(g_documentPath)) {
                    EditorLog("ERROR: could not save '%s' for playtest", g_documentPath.string().c_str());
                    return 0;
                }
                std::string worldDir = g_documentPath.parent_path().filename().string();
                std::string ext = g_documentPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                std::string worldArg;
                if (ext == ".ozone") {
                    worldArg = g_documentPath.string();
                }
                if (!worldArg.empty()) {
                    if (IsPathFile("System/Angels95.exe")) {
                        std::string cmd = "start \"\" System\\Angels95.exe --world \"" + worldArg + "\" --world-dir " + worldDir;
                        EditorLog("Launching: %s", cmd.c_str());
                        system(cmd.c_str());
                    } else {
                        EditorLog("ERROR: System/Angels95.exe not found");
                    }
                }
            } return 0;
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
            case IDM_LEVEL_LIST:    ShowLevelList(!g_editorPanels.showLevelList); RefreshLevelList(); return 0;
            case IDM_ABOUT:         MessageBoxA(NULL, "AngelEd v1.0\nOzWorld Editor\nBased on OmegaTech\nTribeWarez 2026", "About AngelEd", MB_OK | MB_ICONINFORMATION); return 0;
            case IDM_UNDO:          HistoryUndo(); return 0;
            case IDM_REDO:          HistoryRedo(); return 0;
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

    HMENU hEdit = CreatePopupMenu();
    AppendMenuA(hEdit, MF_STRING, IDM_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(hEdit, MF_STRING, IDM_REDO, "&Redo\tCtrl+Y");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hEdit, "&Edit");

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
    AppendMenuA(hView, MF_STRING, IDM_LEVEL_LIST, "Level &List / Campaign");
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

// ---------------------------------------------------------------------------
// Animation tool helpers
// ---------------------------------------------------------------------------
static void EnsureMeshNodeLoaded(MeshObjectNode* n) {
    if (!n || n->mesh) return;
    if (!n->animFile.empty())
        n->mesh = oz::MeshCache::Instance().GetAnimated(n->meshPath, n->texturePath, n->animFile, n->baseDir, true);
    else if (n->skeletal)
        n->mesh = oz::MeshCache::Instance().GetSkeletal(n->meshPath, n->texturePath, n->baseDir, true);
    else
        n->mesh = oz::MeshCache::Instance().GetStatic(n->meshPath, n->texturePath, n->baseDir, true);
}

static oz::AnimatedMesh* AnimTarget() {
    MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
    if (!n || !n->mesh) return nullptr;
    return dynamic_cast<oz::AnimatedMesh*>(n->mesh.get());
}

static float AnimDuration() {
    oz::AnimatedMesh* am = AnimTarget();
    if (!am) return 0.0f;
    const ozanim::Clip* c = am->GetAnimation().FindClip(g_editorPanels.animClipName);
    return c ? c->Duration() : 0.0f;
}

// World position of a mesh vertex (local = base + offset, then node TRS).
static Vector3 MeshVertexWorld(const MeshObjectNode& n, const oz::AnimatedMesh& am,
                               const std::vector<float>& offs, int vi) {
    const std::vector<float>& base = am.BasePositions();
    size_t i = (size_t)vi * 3;
    Vector3 local = {
        base[i + 0] + (i + 0 < offs.size() ? offs[i + 0] : 0.0f),
        base[i + 1] + (i + 1 < offs.size() ? offs[i + 1] : 0.0f),
        base[i + 2] + (i + 2 < offs.size() ? offs[i + 2] : 0.0f)
    };
    Vector3 s = Vector3Scale(local, n.scale);
    Vector3 r = Vector3RotateByAxisAngle(s, {0.0f, 1.0f, 0.0f}, n.yaw * DEG2RAD);
    return Vector3Add(n.position, r);
}

// Nearest vertex to the mouse within maxPx screen pixels (-1 if none).
static int PickVertex(const MeshObjectNode& n, const oz::AnimatedMesh& am,
                      const std::vector<float>& offs, Camera3D& cam, Vector2 mouse, float maxPx) {
    int best = -1; float bestD = maxPx;
    int vc = am.TotalVertexCount();
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (int vi = 0; vi < vc; vi++) {
        Vector2 sp = GetWorldToScreen(MeshVertexWorld(n, am, offs, vi), cam);
        if (sp.x < -50 || sp.y < -50 || sp.x > sw + 50 || sp.y > sh + 50) continue;
        float d = sqrtf((sp.x - mouse.x) * (sp.x - mouse.x) + (sp.y - mouse.y) * (sp.y - mouse.y));
        if (d < bestD) { bestD = d; best = vi; }
    }
    return best;
}

// Full undo/redo for the animation tool: each snapshot captures the live vertex
// edit pose (if editing) AND the whole clip set, so vertex moves, key add/delete
// and clip create/delete are all covered.
struct AnimSnapshot {
    std::vector<float> pose;
    bool hasPose = false;
    ozanim::Animation clips;
};
static std::vector<AnimSnapshot> g_animUndo;
static std::vector<AnimSnapshot> g_animRedo;

static AnimSnapshot AnimCapture() {
    AnimSnapshot s;
    MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
    if (n && n->editPose) { s.pose = *n->editPose; s.hasPose = true; }
    if (oz::AnimatedMesh* am = AnimTarget()) s.clips = am->GetAnimation();
    return s;
}

static void AnimSnapshotPush() {
    g_animRedo.clear();
    g_animUndo.push_back(AnimCapture());
    if (g_animUndo.size() > 32) g_animUndo.erase(g_animUndo.begin());
}

static void AnimRestore(const AnimSnapshot& s) {
    if (oz::AnimatedMesh* am = AnimTarget()) am->SetAnimation(s.clips);
    MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
    if (n && n->editPose && s.hasPose) *n->editPose = s.pose;
}

static void AnimUndo() {
    if (g_animUndo.empty()) return;
    g_animRedo.push_back(AnimCapture());
    AnimSnapshot s = g_animUndo.back();
    g_animUndo.pop_back();
    AnimRestore(s);
}

static void AnimRedo() {
    if (g_animRedo.empty()) return;
    g_animUndo.push_back(AnimCapture());
    AnimSnapshot s = g_animRedo.back();
    g_animRedo.pop_back();
    AnimRestore(s);
}

// ---------------------------------------------------------------------------
// Editor History â€” full-document undo/redo via OZONE text snapshots.
// A snapshot captures geometry, entities, level metadata and the heightmap
// (everything ExportToOzone writes). Camera and selection are left untouched.
// ---------------------------------------------------------------------------
static std::vector<std::string> g_histUndo;
static std::vector<std::string> g_histRedo;
static const size_t kHistMax = 64;

static std::string HistoryCapture() {
    std::ostringstream oss;
    ExportToOzone(oss);
    return oss.str();
}

static void HistoryClear() {
    g_histUndo.clear();
    g_histRedo.clear();
}

// Call BEFORE a mutation: snapshots current state and invalidates redo.
static void HistoryPush() {
    g_histRedo.clear();
    g_histUndo.push_back(HistoryCapture());
    if (g_histUndo.size() > kHistMax) g_histUndo.erase(g_histUndo.begin());
}

static void HistoryRestore(const std::string& text) {
    ClearScene();
    OzoneLoader::Instance().LoadString(
        text.c_str(), OTEditor.Path[0] ? OTEditor.Path : nullptr);
    InjectOzoneEntities(OzoneLoader::Instance().GetEntities(),
                        PawnSystem::Instance());
    OzoneLoader::Instance().RebuildCollisionVolumes();
    g_sel = { SelType::NONE, -1, "", {0,0,0} };
    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
    OmegaTechEditor.DrawModel = false;
}

static void HistoryUndo() {
    if (g_histUndo.empty()) return;
    g_histRedo.push_back(HistoryCapture());
    std::string snap = g_histUndo.back();
    g_histUndo.pop_back();
    HistoryRestore(snap);
    EditorLog("Undo (%zu undo / %zu redo)", g_histUndo.size(), g_histRedo.size());
}

static void HistoryRedo() {
    if (g_histRedo.empty()) return;
    g_histUndo.push_back(HistoryCapture());
    std::string snap = g_histRedo.back();
    g_histRedo.pop_back();
    HistoryRestore(snap);
    EditorLog("Redo (%zu undo / %zu redo)", g_histUndo.size(), g_histRedo.size());
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

    g_documentPath = argc > 1 && argv[1] ? fs::path(argv[1]) : fs::path("../GameData/World.ozone");
    SetWorldDirectory(g_documentPath.parent_path());

    // Load INI config
    g_config.Load("System/AngelEd.ini");

    // Initialize package-based asset loading
    PackageAssetLoader::Instance().Init();

    // Initialize LightningScript entity registry (loads .ozls pickup defs)
    LightningEntityRegistry::Instance().Init();

    // Initialize engine/item texture mapper (must be before EngineBillboard::Init)
    AssetMapper::Instance().Init();

    // The editor viewport must always show authoring gizmos (player starts,
    // lights, zones, sound/music emitters). These were gated behind the client's
    // Debug flag, which AngelEd never sets, so those entities had no visual
    // representation at all and could not be picked in the viewport.
    PawnSystem::Instance().SetShowAuthoringGizmos(true);

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
                    def.name = name;
                    def.speed = speed;
                    def.aggroRange = aggroRange;
                    def.attackRange = attackRange;
                    def.damage = damage;
                    def.maxHealth = maxHealth;
                    def.sprite_path = sp;
                    def.scream_path = sc;
                    ps.RegisterDef(def);
                    if (!sp.empty())
                        PawnManagerAddPawn(name.c_str(), sp.c_str());
                    else
                        PawnManagerAddPawn(name.c_str(), (std::string("GameData/Global/Pawn/") + name + ".png").c_str());
                    loaded++;
                    EditorLog("Loaded pawn def: %s", name.c_str());
                }
            }
            EditorLog("Loaded %d pawn definitions from %s", loaded, defsDir);
        } else {
            // Legacy fallback if no config directory exists
            EditorLog("WARN: %s not found, using hardcoded defaults", defsDir);
            {
                PawnDef d; d.name="Walker"; d.speed=1.5f; d.aggroRange=6.0f; d.attackRange=1.5f; d.damage=10.0f; d.maxHealth=100; ps.RegisterDef(d);
                PawnManagerAddPawn("Walker", "GameData/Global/Pawn/Walker.png");
            } {
                PawnDef d; d.name="Skaarj"; d.speed=2.5f; d.aggroRange=10.0f; d.attackRange=2.0f; d.damage=20.0f; d.maxHealth=150; ps.RegisterDef(d);
                PawnManagerAddPawn("Skaarj", "GameData/Global/Pawn/Skaarj.png");
            } {
                PawnDef d; d.name="Brute"; d.speed=1.0f; d.aggroRange=4.0f; d.attackRange=1.5f; d.damage=30.0f; d.maxHealth=250; ps.RegisterDef(d);
                PawnManagerAddPawn("Brute", "GameData/Global/Pawn/Brute.png");
            } {
                PawnDef d; d.name="Floater"; d.speed=1.2f; d.aggroRange=8.0f; d.attackRange=3.0f; d.damage=15.0f; d.maxHealth=80; ps.RegisterDef(d);
                PawnManagerAddPawn("Floater", "GameData/Global/Pawn/Floater.png");
            }
        }
    }

    // The editor windows (and their Actor Hierarchy tree) were created before
    // the PawnDefs were registered, so rebuild the Pawn Manager tree now that
    // every registry is populated â€” otherwise EnemyPawn is empty and nothing
    // can be spawned from it.
    RefreshPawnManager();

    if (argc > 1 && argv[1]) {
        LoadWorldDocument(g_documentPath);
    } else {
        fs::path ozonePath = g_documentPath.parent_path() / "World.ozone";
        if (fs::exists(ozonePath)) {
            OzoneLoader::Instance().LoadFile(ozonePath.string().c_str());
            InjectOzoneEntities(OzoneLoader::Instance().GetEntities(),
                                PawnSystem::Instance());
        }
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
            HistoryClear();
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
            if (g_previewSoundLoaded) {
                SetSoundVolume(g_previewSound, g_editorPanels.actionSoundVolume / 100.0f);
                PlaySound(g_previewSound);
            }
            g_editorPanels.actionPreviewSoundPath.clear();
        }
        // Live volume + optional loop for the sound preview (Sound has no loop
        // flag, so looping is a replay poll).
        if (g_previewSoundLoaded) {
            SetSoundVolume(g_previewSound, g_editorPanels.actionSoundVolume / 100.0f);
            if (g_editorPanels.actionSoundLoop && !IsSoundPlaying(g_previewSound))
                PlaySound(g_previewSound);
        }
        if (g_editorPanels.actionTextureTarget > 0 && !g_editorPanels.actionTexturePath.empty()) {
            HistoryPush();
            ApplyTextureToModel(g_editorPanels.actionTextureTarget, g_editorPanels.actionTexturePath.c_str());
            g_editorPanels.actionTextureTarget = -1;
            g_editorPanels.actionTexturePath.clear();
        }

        // Viewport bounds check â€” all raycasts only fire when mouse is inside 3D viewport
        Vector2 _mp = GetMousePosition();
        bool _inViewport = (_mp.x >= (float)GetStatsSidebarWidth() && _mp.y >= 28.0f);

        // Hover raycast (throttled every 4 frames for performance)
        {
            static int g_hoverFrameCounter = 0;
            g_hoverFrameCounter++;
            if (g_hoverFrameCounter >= 4) {
                g_hoverFrameCounter = 0;
                // Hover is a read-only probe: it must not depend on DrawModel
                // (the gizmo/placement-ghost flag).
                if (_inViewport) {
                    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
                    EditorHoverEntity();
                } else {
                    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
                }
            }
        }

        // Vertex-edit picking (Phase C): click nearest vertex, Shift adds.
        if (g_editorPanels.animEditVerts && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mp = GetMousePosition();
            if (mp.x >= (float)GetStatsSidebarWidth() && mp.y >= 28.0f) {
                MeshObjectNode* vn = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
                oz::AnimatedMesh* vam = AnimTarget();
                if (vn && vn->editPose && vam) {
                    int vi = PickVertex(*vn, *vam, *vn->editPose, OTEditor.MainCamera, mp, 12.0f);
                    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                    if (vi >= 0) {
                        if (shift) {
                            if (std::find(g_editorPanels.animSelVerts.begin(), g_editorPanels.animSelVerts.end(), vi)
                                == g_editorPanels.animSelVerts.end())
                                g_editorPanels.animSelVerts.push_back(vi);
                        } else {
                            g_editorPanels.animSelVerts.clear();
                            g_editorPanels.animSelVerts.push_back(vi);
                        }
                    } else if (!shift) {
                        g_editorPanels.animSelVerts.clear();
                    }
                }
            }
        }
        // Left-click: select entity (red highlight) â€” suppressed while editing verts.
        // The pick fires on RELEASE, not press, so holding LMB to drag a placement
        // ghost still works: a drag cancels the pick via the movement threshold.
        // Picking is deliberately independent of DrawModel â€” DrawModel only means
        // "a gizmo/placement ghost is drawn". Gating picks on it made the viewport
        // unselectable whenever a placement ghost was active with nothing selected
        // (e.g. after using the CSG toolbox), with no way to recover via Escape.
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            g_lbDown = true;
            g_lbDownPos = GetMousePosition();
            g_gizmoHistPushed = false;
            // Begin an intentional move only when the Move tool is active, or the
            // press lands on the current selection. A plain viewport click never
            // translates a selected entity.
            g_gizmoDrag = false;
            if (!g_suppressViewportDrag && g_sel.type != SelType::NONE &&
                !g_editorPanels.animEditVerts) {
                Vector2 mp = GetMousePosition();
                if (mp.x >= (float)GetStatsSidebarWidth() && mp.y >= 28.0f) {
                    if (g_editorPanels.currentToolMode == 1) {
                        g_gizmoDrag = true;
                    } else {
                        EditorSelection probe;
                        if (EditorRaycastAt(mp, probe) &&
                            probe.type == g_sel.type && probe.index == g_sel.index)
                            g_gizmoDrag = true;
                    }
                }
            }
        }
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && g_lbDown) {
            Vector2 delta = GetMouseDelta();
            if (fabsf(delta.x) > 3.0f || fabsf(delta.y) > 3.0f) g_lbDown = false;
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (g_lbDown) {
                g_lbDown = false;
                Vector2 mp = GetMousePosition();
                if (!g_editorPanels.animEditVerts && g_placeMode != PlaceMode::TERRAIN &&
                    mp.x >= (float)GetStatsSidebarWidth() && mp.y >= 28.0f) {
                    EditorPickEntity();
                }
            }
            g_gizmoDrag = false;
            g_gizmoHistPushed = false;
            g_suppressViewportDrag = false;
            g_terrainHistPushed = false;
        }
        // Release the post-context-menu input guard once the button is no longer held.
        if (g_suppressViewportDrag && !IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            g_suppressViewportDrag = false;

        // TERRAIN brush: raise/lower heightmap cells (TERRAIN mode, left mouse)
        if (g_placeMode == PlaceMode::TERRAIN && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            auto& oz = OzoneLoader::Instance();
            if (oz.HasHeightmap()) {
                Vector2 mp = GetMousePosition();
                if (mp.x >= (float)GetStatsSidebarWidth() && mp.y >= 28.0f) {
                    if (!g_terrainHistPushed) { HistoryPush(); g_terrainHistPushed = true; }
                    Ray ray = GetMouseRay(mp, OTEditor.MainCamera);
                    Vector3 hmPos = oz.GetHeightmapPosition();
                    float scale = oz.GetHeightmapScale();
                    float sx = oz.GetHeightmapCellSize() * (oz.GetHeightmapGridW() - 1) * scale;
                    float sz = oz.GetHeightmapCellSize() * (oz.GetHeightmapGridH() - 1) * scale;
                    float halfW = sx * 0.5f;
                    float halfD = sz * 0.5f;

                    // Intersect ray with the heightmap's base plane (Y = hmPos.y)
                    float t = (hmPos.y - ray.position.y) / ray.direction.y;
                    if (t > 0.0f) {
                        float wx = ray.position.x + ray.direction.x * t;
                        float wz = ray.position.z + ray.direction.z * t;
                        float mx = (wx - hmPos.x) / scale;
                        float mz = (wz - hmPos.z) / scale;
                        int col = (int)((mx + halfW) / (sx / (float)(oz.GetHeightmapGridW() - 1)));
                        int row = (int)((mz + halfD) / (sz / (float)(oz.GetHeightmapGridH() - 1)));

                        // Throttle: skip if same cell as last frame
                        static int lastCol = -1, lastRow = -1;
                        if (col == lastCol && row == lastRow) {
                            // Still allow painting if mouse button held (applied once on press)
                        } else {
                            lastCol = col; lastRow = row;
                        }

                        int gw = oz.GetHeightmapGridW();
                        int gh = oz.GetHeightmapGridH();
                        int radius = g_editorPanels.terrainBrushSize;
                        float strength = g_editorPanels.terrainBrushStrength;
                        bool raising = (g_editorPanels.terrainBrushMode == 0);

                        // Batch all cell edits then rebuild once
                        for (int rz = -radius; rz <= radius; rz++) {
                            for (int rx = -radius; rx <= radius; rx++) {
                                int c = col + rx;
                                int r = row + rz;
                                if (c < 0 || c >= gw || r < 0 || r >= gh)
                                    continue;
                                float dist = sqrtf((float)(rx*rx + rz*rz));
                                if (dist > (float)radius) continue;
                                float falloff = 1.0f - dist / (float)(radius + 1);
                                float curH = oz.GetHeightAtGrid(c, r);
                                float delta = strength * falloff;
                                oz.SetHeightAtGrid(c, r,
                                    raising ? curH + delta : curH - delta, false);
                            }
                        }
                        oz.RebuildHeightmapMesh();
                    }
                }
            }
        }

        // Right-click: drag resizes placement ghost; click picks entity + native
        // context menu. It must NEVER commit a placement (that added duplicate
        // entities when right-clicking to delete/duplicate) â€” commit is Enter only.
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            g_rbDown = true;
            g_rbDownPos = GetMousePosition();
        }
        // A right-drag cancels the context menu. This must not depend on
        // DrawModel: the menu has to open whenever the user right-clicks an
        // entity in the viewport.
        if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && g_rbDown) {
            Vector2 delta = GetMouseDelta();
            if (fabsf(delta.x) > 3.0f || fabsf(delta.y) > 3.0f) g_rbDown = false;
        }
        if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON) && g_rbDown) {
            g_rbDown = false;
            // Re-check viewport bounds with fresh cursor position
            Vector2 _mp_rel = GetMousePosition();
            bool _inVpRel = (_mp_rel.x >= (float)GetStatsSidebarWidth() && _mp_rel.y >= 28.0f);
            // Per-FACE surface pick, done BEFORE the entity pick so the menu can
            // offer "Surface Properties (N Selected)". Triangle-level rather than
            // AABB-level, so clicking the top of a wall selects the +Y face and
            // not the brush as a whole.
            const SurfaceFacePick spfPick = _inVpRel ? PickSurfaceFace(_mp_rel) : SurfaceFacePick{};
            // Pick without the left-click toggle so right-clicking the already
            // selected entity opens its menu instead of deselecting it. Gate on
            // the hit result â€” on a miss g_sel still holds the previous entity,
            // so a stale selection would otherwise pop a Delete menu onto
            // whatever the user right-clicked next (or on empty space).
            if (_inVpRel) {
                if (EditorPickEntity(false)) {
                    // Native Win32 context menu with TPM_RETURNCMD (avoids WM_COMMAND routing issues)
                    #ifdef _WIN32
                    HWND hWnd = (HWND)GetWindowHandle();
                    if (hWnd) {
                        HMENU hMenu = CreatePopupMenu();
                        // Surface Properties first, like the UT99 viewport menu:
                        // it is the per-FACE editor and the most-used entry when
                        // texturing a brush.
                        if (spfPick.hit && spfPick.renderable >= 0) {
                            // Hold Shift to keep the previous face selection and
                            // add this face ("(N Selected)"). Plain right-click
                            // starts a fresh single-face selection.
                            if (!(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
                                ClearSurfacePicks();
                            ToggleSurfacePick(spfPick);
                            const int nSurf = (int)g_selectedSurfaces.size();
                            char lbl[96];
                            snprintf(lbl, sizeof(lbl), "Surface Properties (%d Selected)", nSurf);
                            AppendMenuA(hMenu, MF_STRING, IDM_SURFACE_PROPS, lbl);
                            AppendMenuA(hMenu, MF_STRING, IDM_SURFACE_RESET, "Reset Surface");
                        }
                        AppendMenuA(hMenu, MF_STRING, IDM_PROPERTIES, "Properties");
                        AppendMenuA(hMenu, MF_STRING, IDM_DELETE_ENTITY, "Delete");
                        AppendMenuA(hMenu, MF_STRING, IDM_DUPLICATE_ENTITY, "Duplicate");
                        // A Mesh or a CSG brush carries no usable collision of
                        // its own (the world is AABB-only and a placed prop has
                        // none at all), so offer to derive convex proxies for it.
                        if (g_sel.type == SelType::MESH || g_sel.type == SelType::BRUSH) {
                            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                            AppendMenuA(hMenu, MF_STRING, IDM_APPEND_AUTOCONVEX,
                                        "Append AutoConvex Collision");
                        }
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
                        // Swallow the input that dismissed/used the menu: the modal
                        // loop skips raylib polling, so a stray held button + stale
                        // mouse delta would otherwise drag the selected entity.
                        g_suppressViewportDrag = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
                        g_gizmoDrag = false;
                        g_gizmoHistPushed = false;
                        g_lbDown = false;
                        (void)GetMouseDelta();
                        (void)GetMouseWheelMove();
                        // Keep the gizmo glued to the (possibly re-picked) selection
                        // so no drift can be applied.
                        if (g_sel.type != SelType::NONE) SnapGizmoToSelection(g_sel);
                        // Handle the returned command directly
                        if (cmd == IDM_PROPERTIES) OpenPropertiesForSelection();
                        else if (cmd == IDM_DELETE_ENTITY) DeleteSelectedEntity();
                        else if (cmd == IDM_DUPLICATE_ENTITY) DuplicateSelectedEntity();
                        else if (cmd == IDM_APPLY_TEXTURE) g_editorPanels.actionApplyTextureToSel = true;
                        else if (cmd == IDM_APPEND_AUTOCONVEX) AppendAutoConvexForSelection();
                        else if (cmd == IDM_SURFACE_PROPS) {
                            // Every picked face must belong to ONE renderable:
                            // the dialog edits a single BrushSurface, and mixing
                            // two brushes' faces into one mask would write face 3
                            // of brush A onto face 3 of brush B.
                            int rIdx = -1; uint32_t mask = 0; bool mixed = false;
                            for (const auto& sp : g_selectedSurfaces) {
                                if (rIdx < 0) rIdx = sp.renderable;
                                else if (sp.renderable != rIdx) { mixed = true; break; }
                                if (sp.face >= 0) mask |= (1u << (int)sp.face);
                            }
                            if (mixed || rIdx < 0 || mask == 0) {
                                MessageBoxA(hWnd,
                                    "Surfaces from more than one brush were selected.\n\n"
                                    "Right-click without Shift to start a new selection.",
                                    "Surface Properties", MB_OK | MB_ICONWARNING);
                            } else {
                                ShowSurfaceProps(true, rIdx, mask);
                            }
                        }
                        else if (cmd == IDM_SURFACE_RESET) {
                            if (spfPick.hit && spfPick.renderable >= 0)
                                g_editorPanels.actionResetSurface = true;
                        }
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

            // Scroll wheel = dolly forward/backward.
            // In MODEL placement with Scale/Rotate tool modes the wheel is
            // reserved for scaling/rotating the ghost instead.
            bool wheelReservedForTool =
                (g_placeMode == PlaceMode::MODEL && OmegaTechEditor.DrawModel &&
                 g_editorPanels.currentToolMode != 0);
            if (!wheelReservedForTool) {
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
            // GameType / skybox / particles -> level metadata (persisted on save)
            if (zp.applyGameType || zp.applySkybox || zp.applyParticles) {
                LevelMetadata meta = GetLevelMetadata();
                if (zp.applyGameType) {
                    meta.gameType = zp.gameType;
                    meta.maxPlayers = zp.maxPlayers;
                    meta.respawnTime = zp.respawnTime;
                    meta.timeLimitEnabled = zp.timeLimitEnabled;
                    meta.timeLimitMinutes = zp.timeLimitMinutes;
                    meta.scoreLimit = zp.scoreLimit;
                    meta.friendlyFire = zp.friendlyFire;
                    EditorLog("Level GameType applied: mode=%d maxPlayers=%d", (int)meta.gameType, meta.maxPlayers);
                }
                if (zp.applySkybox) {
                    meta.skyboxTexturePath = zp.skyboxTexturePath;
                    EditorLog("Level skybox applied: %s", meta.skyboxTexturePath.c_str());
                }
                if (zp.applyParticles) {
                    meta.particleType = zp.particleType;
                    meta.particleDensity = zp.particleDensity;
                    meta.particleSpeed = zp.particleSpeed;
                    meta.particleColorR = zp.particleColorR;
                    meta.particleColorG = zp.particleColorG;
                    meta.particleColorB = zp.particleColorB;
                    meta.particleWindX = zp.particleWindX;
                    meta.particleWindZ = zp.particleWindZ;
                    EditorLog("Level particles applied: type=%d density=%.0f", (int)meta.particleType, meta.particleDensity);
                }
                SetLevelMetadata(meta);
            }
            ClearZoneApplyFlags();
        }

        // Submit lights and viewPos for Lit mode
        if (OTEditor.ViewMode == LightingMode::LIT && OTEditor.LitFogShader.id > 0) {
            auto& pawnLights = PawnSystem::Instance().GetLights();
            float dt = GetFrameTime();
            LitLightning_Update(pawnLights, OTEditor.LitFogShader, OTEditor.MainCamera, dt);
            // The surface program carries its own light uniforms, so a
            // surface-flagged brush would be lit by a stale light set without
            // this. Same lights, same order - one source of truth.
            if (oz::SurfaceMaterial::Instance().Ready()) {
                float amb[4] = {0.1f, 0.1f, 0.1f, 1.0f};
                OzoneLoader::Instance().GetWorldAmbient(amb);
                float fogCol[3] = {0.7f, 0.7f, 0.8f};
                float fogStart = 10.0f, fogEnd = 100.0f, fogDensity = 1.0f, fogIntensity = 1.0f;
                OzoneLoader::Instance().GetWorldFog(fogCol, fogStart, fogEnd,
                                                    fogDensity, fogIntensity);
                oz::SurfaceMaterial::Instance().SetFog(fogCol, fogStart, fogEnd,
                                                       fogDensity, fogIntensity);
                oz::SurfaceMaterial::Instance().UpdateFrame(pawnLights, OTEditor.MainCamera, dt, amb);
            }
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

        // -------------------------------------------------------------------
        // Skybox cube (viewport backdrop) â€” toggled by the toolbar "Sky" button.
        // Mirrors the client: top/bottom use the cap texture, sides reuse it
        // unless a dedicated side skybox is authored.
        // -------------------------------------------------------------------
        if (OTEditor.ShowSkybox) {
            static Model s_skyFaces[6];
            static bool s_skyFacesReady = false;
            static Texture2D s_skyTex = {0};
            static std::string s_skyPath;

            if (!s_skyFacesReady) {
                const float skySize = 2000.0f;
                for (int i = 0; i < 6; i++) {
                    Mesh plane = GenMeshPlane(skySize, skySize, 1, 1);
                    float* tc = (float*)plane.texcoords;
                    int vc = plane.vertexCount;
                    if (tc) {
                        switch (i) {
                            case 0: for (int v = 0; v < vc; v++) tc[v*2+1] = 1.0f - tc[v*2+1]; break;
                            case 2: for (int v = 0; v < vc; v++) tc[v*2]   = 1.0f - tc[v*2];   break;
                            case 5: for (int v = 0; v < vc; v++) tc[v*2]   = 1.0f - tc[v*2];   break;
                        }
                    }
                    s_skyFaces[i] = LoadModelFromMesh(plane);
                }
                s_skyFacesReady = true;
            }

            // Resolve the skybox texture: an explicitly-applied levelinfo skybox
            // path takes priority; otherwise fall back to the world's
            // Models/Skybox.png default.
            std::string want;
            {
                LevelMetadata meta = GetLevelMetadata();
                if (!meta.skyboxTexturePath.empty())
                    want = meta.skyboxTexturePath;
            }
            if (want.empty() && !g_documentPath.empty()) {
                fs::path worldSky = g_documentPath.parent_path() / "Models" / "Skybox.png";
                if (fs::exists(worldSky)) want = worldSky.string();
            }
            if (want != s_skyPath) {
                if (s_skyTex.id > 0) { UnloadTexture(s_skyTex); s_skyTex = {0}; }
                if (!want.empty()) {
                    s_skyTex = LoadTextureWithFallback(want.c_str());
                    if (s_skyTex.id == 0)
                        EditorLog("Skybox: could not load '%s'", want.c_str());
                }
                s_skyPath = want;
            }

            if (s_skyTex.id > 0) {
                Vector3 cp = OTEditor.MainCamera.position;
                rlDisableDepthMask();
                rlDisableBackfaceCulling();
                auto drawFace = [&](int idx, float x, float y, float z, float rotY, float rotX, Texture2D tex) {
                    rlPushMatrix();
                    rlTranslatef(x, y, z);
                    if (rotX != 0.0f) rlRotatef(rotX, 1, 0, 0);
                    if (rotY != 0.0f) rlRotatef(rotY, 0, 1, 0);
                    s_skyFaces[idx].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
                    DrawModel(s_skyFaces[idx], {0,0,0}, 1.0f, WHITE);
                    rlPopMatrix();
                };
                drawFace(0, cp.x, cp.y + 1000.0f, cp.z,   0.0f, 180.0f, s_skyTex);  // top
                drawFace(1, cp.x, cp.y - 1000.0f, cp.z,   0.0f,   0.0f, s_skyTex);  // bottom
                drawFace(2, cp.x + 1000.0f, cp.y, cp.z,  90.0f,   0.0f, s_skyTex);  // +X
                drawFace(3, cp.x - 1000.0f, cp.y, cp.z, -90.0f,   0.0f, s_skyTex);  // -X
                drawFace(4, cp.x, cp.y, cp.z + 1000.0f, 180.0f,   0.0f, s_skyTex);  // +Z
                drawFace(5, cp.x, cp.y, cp.z - 1000.0f,   0.0f,   0.0f, s_skyTex);  // -Z
                if (OTEditor.ViewMode == LightingMode::WIREFRAME)
                    rlDisableBackfaceCulling();
                else
                    rlEnableBackfaceCulling();
                rlEnableDepthMask();
            }
        }

        DrawGrid(1000, 10.0f);

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

        // Vertex-edit overlay (Phase C): highlight selected vertices.
        if (g_editorPanels.animEditVerts) {
            MeshObjectNode* vn = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
            oz::AnimatedMesh* vam = AnimTarget();
            if (vn && vn->editPose && vam) {
                for (int vi : g_editorPanels.animSelVerts)
                    DrawSphere(MeshVertexWorld(*vn, *vam, *vn->editPose, vi), 0.06f, RED);
            }
        }

        // Collision volume wireframes (editor only, toggled by the sidebar
// "Collision" button). This is the post-CSG result - what the player actually
// stands on - which is the only way to spot a brush whose render mesh is solid
// but whose collision volume does not exist, and to see generated
// SURF_COLLISION_PROXY boxes in place.
if (g_editorPanels.showCollisionBounds) {
    Color warnCol = g_editorPanels.collisionOpWarning
        ? (Color){255, 90, 90, 220} : (Color){120, 220, 160, 160};
    for (const auto& v : OzoneLoader::Instance().GetCollisionVolumes())
        DrawBoundingBox(v.aabb, warnCol);
}

// Zone volume wireframes
        {
            auto& zones = ZoneManager::Instance().GetZones();
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

        // GameEngine.PathNode markers + link lines (editor visualization)
        {
            auto& paths = PawnSystem::Instance().GetPathNodes();
            Color pathCol = (Color){120, 220, 255, 255};
            for (auto& pn : paths) {
                DrawCubeWires(pn.position, 0.6f, 0.6f, 0.6f, pathCol);
                DrawSphere(pn.position, 0.15f, pathCol);
                if (g_sel.type == SelType::PATHNODE && (int)pn.id == g_sel.index)
                    DrawSphereWires(pn.position, pn.radius, 8, 8, (Color){255, 180, 60, 120});
                for (auto& linkName : pn.next) {
                    PathNode* dst = PawnSystem::Instance().FindPathNodeByName(linkName);
                    if (dst) DrawLine3D(pn.position, dst->position, pathCol);
                }
                if (pn.loop)
                    DrawLine3D(pn.position,
                               {pn.position.x, pn.position.y + 1.5f, pn.position.z},
                               (Color){255, 220, 80, 255});
            }
        }

        // WindZone boxes + direction arrows (editor visualization)
        {
            auto& winds = PawnSystem::Instance().GetWindZones();
            Color windCol = (Color){120, 255, 180, 100};
            for (auto& wz : winds) {
                DrawBoundingBox(wz.bounds, windCol);
                Vector3 c = {(wz.bounds.min.x + wz.bounds.max.x) * 0.5f,
                             (wz.bounds.min.y + wz.bounds.max.y) * 0.5f,
                             (wz.bounds.min.z + wz.bounds.max.z) * 0.5f};
                Vector3 tip = {c.x + wz.direction.x * 3.0f,
                               c.y + wz.direction.y * 3.0f,
                               c.z + wz.direction.z * 3.0f};
                DrawLine3D(c, tip, (Color){120, 255, 180, 255});
                DrawSphere(tip, 0.15f, (Color){120, 255, 180, 255});
            }
        }

        // --- Placement visuals ---
        if (OmegaTechEditor.DrawModel)
        {
            float px = OmegaTechEditor.X, py = OmegaTechEditor.Y, pz = OmegaTechEditor.Z;
            float ps = OmegaTechEditor.S, pr = OmegaTechEditor.R;

            if (g_placeMode == PlaceMode::MODEL) {
                if (EMID >= 200) {
                    // OZONE primitive ghost
                    int primType = EMID - 200;
                    Vector3 size = {OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L};
                    Vector3 center = {px, py, pz};
                    DrawBoundingBox((BoundingBox){{px - size.x*0.5f, py - size.y*0.5f, pz - size.z*0.5f},
                                                  {px + size.x*0.5f, py + size.y*0.5f, pz + size.z*0.5f}}, ORANGE);
                    if (primType == 0) { // Box
                        DrawCubeWires(center, size.x, size.y, size.z, (Color){255,165,0,180});
                    } else if (primType == 1) { // Cylinder
                        DrawCylinderWires(center, size.x*0.5f, size.x*0.5f, size.y, 16, ORANGE);
                    } else if (primType == 2) { // Sphere
                        DrawSphereWires(center, size.x*0.5f, 12, 12, ORANGE);
                    } else if (primType == 3) { // Pyramid
                        DrawCubeWires(center, size.x, size.y, size.z, ORANGE);
                    } else if (primType == 4) { // Plane
                        DrawCubeWires(center, size.x, 0.1f, size.z, ORANGE);
                    }
                } else if (EMID > 0) {
                    LoadedModel* lm = WDLModels.GetModelByWDLId(EMID);
                    if (lm) DrawModelEx(lm->model, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE);
                } else if (EMID == 0) {
                    // User-selected model from the Model Browser â€” ghost preview.
                    int bidx = g_editorPanels.selectedModel;
                    if (bidx >= 0 && bidx < (int)g_editorPanels.modelEntries.size()) {
                        static Model s_browsePreview = {0};
                        static std::string s_browsePreviewPath;
                        const std::string& path = g_editorPanels.modelEntries[bidx].path;
                        if (s_browsePreviewPath != path) {
                            if (s_browsePreview.meshes) UnloadModel(s_browsePreview);
                            s_browsePreview = {0};
                            s_browsePreviewPath = path;
                            s_browsePreview = LoadModelWithFallback(path.c_str());
                        }
                        if (s_browsePreview.meshes)
                            DrawModelEx(s_browsePreview, {px,py,pz},{0,1,0},pr,{ps,ps,ps},WHITE);
                        else
                            DrawCubeWires({px,py,pz}, ps, ps, ps, ORANGE);
                    }
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
                        // Resolve relative mesh/texture paths beside the .ozls.
                        std::string baseDir;
                        size_t slash = edef->sourcePath.find_last_of("/\\");
                        if (slash != std::string::npos) baseDir = edef->sourcePath.substr(0, slash + 1);
                        std::string meshRes = oz::ResolveMeshAsset(baseDir, edef->mesh);
                        std::string texRes = oz::ResolveMeshAsset(baseDir, edef->texture);
                        g_pickupPreviewModel = LoadModelWithFallback(meshRes.c_str());
                        if (g_pickupPreviewModel.meshes && !texRes.empty()) {
                            Texture2D tex = LoadTextureWithFallback(texRes.c_str());
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
                    case EditorNodeType::SPAWN:  c = BLUE;    break;
                    case EditorNodeType::NPC:    c = MAGENTA; break;
                    case EditorNodeType::LIGHT:  c = YELLOW;  break;
                    case EditorNodeType::ZONE:   c = SKYBLUE; break;
                    case EditorNodeType::PORTAL: c = PURPLE;  break;
                }
                if (OmegaTechEditor.ActiveNodeType == EditorNodeType::PORTAL) {
                    // Portal preview: full volume wireframe + swirl marker
                    float hw = fmaxf(OmegaTechEditor.W, 1) * 0.5f;
                    float hh = fmaxf(OmegaTechEditor.H, 1) * 0.5f;
                    float hd = fmaxf(OmegaTechEditor.L, 1) * 0.5f;
                    DrawCubeWires({px, py, pz}, hw * 2, hh * 2, hd * 2, c);
                    DrawCube({px, py, pz}, hw * 2, hh * 2, hd * 2, (Color){c.r,c.g,c.b,40});
                    DrawSphere({px, py + hh, pz}, 0.3f, c);
                } else if (OmegaTechEditor.ActiveNodeType == EditorNodeType::ZONE) {
                    float hw = fmaxf(OmegaTechEditor.W, 1) * 0.5f;
                    float hh = fmaxf(OmegaTechEditor.H, 1) * 0.5f;
                    float hd = fmaxf(OmegaTechEditor.L, 1) * 0.5f;
                    DrawCubeWires({px, py, pz}, hw * 2, hh * 2, hd * 2, c);
                    DrawCube({px, py, pz}, hw * 2, hh * 2, hd * 2, (Color){c.r,c.g,c.b,30});
                } else {
                    DrawCube({px, py, pz}, 0.5f, 0.2f, 0.5f, c);
                    DrawCubeWires({px, py, pz}, 0.5f, 0.2f, 0.5f, (Color){c.r,c.g,c.b,80});
                }
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
            // Keyboard nudge/rotate/scale of a selection is an undoable edit.
            if (g_sel.type != SelType::NONE && !g_editorPanels.animEditVerts &&
                (IsKeyPressed(KEY_U) || IsKeyPressed(KEY_J) || IsKeyPressed(KEY_H) ||
                 IsKeyPressed(KEY_K) || IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_I) ||
                 IsKeyPressed(KEY_O) || IsKeyPressed(KEY_L) ||
                 IsKeyPressed(KEY_T) || IsKeyPressed(KEY_B))) {
                HistoryPush();
            }
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
            // NOTE: scale-down is B, not G â€” G cycles the CSG op in MODEL mode.
            if (IsKeyDown(KEY_B)) OmegaTechEditor.S -= 0.5f;

            // Vertex-edit transform (Phase C): the movement/rotate keys deform
            // the selected vertices instead of moving the mesh node.
            if (g_editorPanels.animEditVerts) {
                MeshObjectNode* vn = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
                oz::AnimatedMesh* vam = AnimTarget();
                if (!g_editorPanels.animPrevValid) {
                    g_editorPanels.animPrevX = OmegaTechEditor.X;
                    g_editorPanels.animPrevY = OmegaTechEditor.Y;
                    g_editorPanels.animPrevZ = OmegaTechEditor.Z;
                    g_editorPanels.animPrevR = OmegaTechEditor.R;
                    g_editorPanels.animPrevValid = true;
                }
                float vdx = OmegaTechEditor.X - g_editorPanels.animPrevX;
                float vdy = OmegaTechEditor.Y - g_editorPanels.animPrevY;
                float vdz = OmegaTechEditor.Z - g_editorPanels.animPrevZ;
                float vdr = OmegaTechEditor.R - g_editorPanels.animPrevR;
                if (vn && vn->editPose && vam && !g_editorPanels.animSelVerts.empty() &&
                    (vdx != 0.0f || vdy != 0.0f || vdz != 0.0f || vdr != 0.0f)) {
                    AnimSnapshotPush();
                    std::vector<float>& offs = *vn->editPose;
                    const std::vector<float>& base = vam->BasePositions();
                    Vector3 cen = {0, 0, 0};
                    for (int vi : g_editorPanels.animSelVerts) {
                        cen.x += base[vi * 3 + 0] + offs[vi * 3 + 0];
                        cen.y += base[vi * 3 + 1] + offs[vi * 3 + 1];
                        cen.z += base[vi * 3 + 2] + offs[vi * 3 + 2];
                    }
                    float inv = 1.0f / (float)g_editorPanels.animSelVerts.size();
                    cen.x *= inv; cen.y *= inv; cen.z *= inv;
                    float yawRad = vdr * DEG2RAD;
                    for (int vi : g_editorPanels.animSelVerts) {
                        Vector3 local = {base[vi * 3 + 0] + offs[vi * 3 + 0],
                                         base[vi * 3 + 1] + offs[vi * 3 + 1],
                                         base[vi * 3 + 2] + offs[vi * 3 + 2]};
                        local.x += vdx; local.y += vdy; local.z += vdz;
                        if (yawRad != 0.0f) {
                            Vector3 rel = {local.x - cen.x, local.y - cen.y, local.z - cen.z};
                            rel = Vector3RotateByAxisAngle(rel, {0, 1, 0}, yawRad);
                            local = {cen.x + rel.x, cen.y + rel.y, cen.z + rel.z};
                        }
                        offs[vi * 3 + 0] = local.x - base[vi * 3 + 0];
                        offs[vi * 3 + 1] = local.y - base[vi * 3 + 1];
                        offs[vi * 3 + 2] = local.z - base[vi * 3 + 2];
                    }
                }
                // Park the gizmo so the node itself doesn't move while editing.
                OmegaTechEditor.X = g_editorPanels.animPrevX;
                OmegaTechEditor.Y = g_editorPanels.animPrevY;
                OmegaTechEditor.Z = g_editorPanels.animPrevZ;
                OmegaTechEditor.R = g_editorPanels.animPrevR;
            } else {
                g_editorPanels.animPrevValid = false;
            }

            // Sync gizmo position back to selected entity (for manipulation)
            if (g_sel.type != SelType::NONE && !g_editorPanels.animEditVerts) {
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
                    // Try to update the renderable position so the visual moves
                    int rIdx = -1;
                    if (idx >= 0 && idx < OzoneLoader::Instance().Count()) {
                        rIdx = idx; // renderable index
                    } else {
                        rIdx = OzoneLoader::Instance().FindRenderableByCollisionVol(idx);
                    }
                    if (rIdx >= 0) {
                        OzoneRenderable* r = OzoneLoader::Instance().Get(rIdx);
                        if (r) {
                            BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
                            Vector3 center = {r->position.x + (mb.min.x + mb.max.x) * 0.5f * r->scale,
                                              r->position.y + (mb.min.y + mb.max.y) * 0.5f * r->scale,
                                              r->position.z + (mb.min.z + mb.max.z) * 0.5f * r->scale};
                            Vector3 delta = {r->position.x - center.x,
                                             r->position.y - center.y,
                                             r->position.z - center.z};
                            r->position = {newPos.x + delta.x, newPos.y + delta.y, newPos.z + delta.z};
                            // Scale/Rotate tool modes write back to the renderable
                            if (g_editorPanels.currentToolMode == 2) r->scale = OmegaTechEditor.S;
                            if (g_editorPanels.currentToolMode == 3) r->rotation = OmegaTechEditor.R * DEG2RAD;
                        }
                    }
                    OzoneLoader::Instance().RebuildCollisionVolumes();
                } else if (g_sel.type == SelType::LIGHT) {
                    LightNode* l = PawnSystem::Instance().GetLight(idx);
                    if (l) l->position = newPos;
                } else if (g_sel.type == SelType::SPAWN) {
                    if (PlayerStartNode* s = FindPlayerStartById(idx))
                        s->position = newPos;
                } else if (g_sel.type == SelType::PORTAL) {
                    auto& portals = ZoneManager::Instance().GetPortals();
                    if (idx >= 0 && idx < (int)portals.size()) {
                        auto& p = portals[idx];
                        Vector3 center = {(p.bounds.min.x + p.bounds.max.x) * 0.5f,
                                          (p.bounds.min.y + p.bounds.max.y) * 0.5f,
                                          (p.bounds.min.z + p.bounds.max.z) * 0.5f};
                        Vector3 delta = {newPos.x - center.x, newPos.y - center.y, newPos.z - center.z};
                        p.bounds.min.x += delta.x; p.bounds.min.y += delta.y; p.bounds.min.z += delta.z;
                        p.bounds.max.x += delta.x; p.bounds.max.y += delta.y; p.bounds.max.z += delta.z;
                    }
                }
                // Update selection stored position
                g_sel.pos = newPos;
            }

            // Commit placement
            if (IsKeyPressed(KEY_ENTER) || DoubleClick)
            {
                bool willCommit =
                    (g_placeMode == PlaceMode::PICKUP) ||
                    (g_placeMode == PlaceMode::NODE) ||
                    (g_placeMode == PlaceMode::MODEL &&
                     ((EMID == 0 && g_editorPanels.selectedModel >= 0) || EMID >= 200));
                if (willCommit) HistoryPush();
                if (g_placeMode == PlaceMode::MODEL) {
                    if (EMID > 0) {
                        // Legacy WDL Model/Collision placement is removed; OZONE
                        // primitives (EMID >= 200) are committed below.
                    } else {
                        if (EMID == 0) {
                            // Model Browser selection â†’ GameEngine.Mesh.* entity.
                            int bidx = g_editorPanels.selectedModel;
                            if (bidx >= 0 && bidx < (int)g_editorPanels.modelEntries.size()) {
                                const std::string& path = g_editorPanels.modelEntries[bidx].path;
                                std::string ext;
                                size_t dot = path.rfind('.');
                                if (dot != std::string::npos) {
                                    ext = path.substr(dot);
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                }
                                MeshObjectNode node;
                                node.meshPath = path;
                                node.skeletal = (ext == ".glb" || ext == ".gltf" || ext == ".iqm");
                                node.position = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                                node.yaw = OmegaTechEditor.R;
                                node.scale = OmegaTechEditor.S;
                                PawnSystem::Instance().AddMeshObject(node);
                                EditorLog("Placed Mesh.%s '%s'",
                                          node.skeletal ? "Skeletal" : "Static", path.c_str());
                            } else {
                                EditorLog("Place model: select a model in the Model Browser first");
                            }
                        }
                    }
                } else if (g_placeMode == PlaceMode::PICKUP) {
                    PickupNode node;
                    node.position = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                    node.typeName = OmegaTechEditor.ActivePickupName;
                    PawnSystem::Instance().AddPickup(node);
                    EditorLog("Placed pickup '%s'", node.typeName.c_str());
                } else if (g_placeMode == PlaceMode::NODE) {
                    if (OmegaTechEditor.ActiveNodeType == EditorNodeType::ZONE) {
                        float hw = fmaxf(OmegaTechEditor.W, 1) * 0.5f;
                        float hh = fmaxf(OmegaTechEditor.H, 1) * 0.5f;
                        float hd = fmaxf(OmegaTechEditor.L, 1) * 0.5f;
                        float minX = OmegaTechEditor.X - hw, maxX = OmegaTechEditor.X + hw;
                        float minY = OmegaTechEditor.Y - hh, maxY = OmegaTechEditor.Y + hh;
                        float minZ = OmegaTechEditor.Z - hd, maxZ = OmegaTechEditor.Z + hd;
                        ZoneVolumeNode node;
                        node.bounds = {{minX, minY, minZ}, {maxX, maxY, maxZ}};
                        node.zoneType = ZoneType::ZONE_WATER;
                        node.intensity = 1.0f;
                        ZoneManager::Instance().AddZone(node);
                    } else if (OmegaTechEditor.ActiveNodeType == EditorNodeType::PORTAL) {
                        float hw = fmaxf(OmegaTechEditor.W, 1) * 0.5f;
                        float hh = fmaxf(OmegaTechEditor.H, 1) * 0.5f;
                        float hd = fmaxf(OmegaTechEditor.L, 1) * 0.5f;
                        float minX = OmegaTechEditor.X - hw, maxX = OmegaTechEditor.X + hw;
                        float minY = OmegaTechEditor.Y - hh, maxY = OmegaTechEditor.Y + hh;
                        float minZ = OmegaTechEditor.Z - hd, maxZ = OmegaTechEditor.Z + hd;
                        std::string target(g_editorPanels.portalTargetWorld);
                        if (target.empty()) target = "EngineTest";
                        ZonePortal portal;
                        portal.bounds = {{minX, minY, minZ}, {maxX, maxY, maxZ}};
                        portal.targetWorld = target;
                        portal.targetSpawn = {0, 20, 0};
                        portal.bidirectional = true;
                        ZoneManager::Instance().AddPortal(portal);
                        RefreshLevelList();
                    } else if (OmegaTechEditor.ActiveNodeType == EditorNodeType::SPAWN) {
                        PlayerStartNode node;
                        node.position = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                        node.yaw = OmegaTechEditor.R;
                        PawnSystem::Instance().AddPlayerStart(node);
                    } else if (OmegaTechEditor.ActiveNodeType == EditorNodeType::NPC) {
                        PawnSystem::Instance().Spawn(
                            {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z}, "Walker");
                    } else if (OmegaTechEditor.ActiveNodeType == EditorNodeType::LIGHT) {
                        LightNode node;
                        node.position = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                        PawnSystem::Instance().AddLight(node);
                    }
                }

                OmegaTechEditor.DrawModel = false;

                // CSG: register brush renderable for OZONE primitives (EMID >= 200).
                // Collision comes from OzoneLoader::RebuildCollisionVolumes(), which
                // runs its own CsgProcessor over every renderable's stored csgOp.
                if (EMID >= 200 && g_placeMode == PlaceMode::MODEL) {
                    int primType = EMID - 200;
                    Vector3 center = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                    Vector3 size = {OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L};
                    int ridx = CommitBrushRenderable(
                        primType, center, size, OmegaTechEditor.R, OmegaTechEditor.S,
                        (int)OmegaTechEditor.CSGOperation);
                    if (ridx >= 0) {
                        EditorLog("Brush renderable added idx=%d prim=%d", ridx, primType);
                        // Auto-apply preselected texture to new brush
                        if (!g_editorPanels.activeTexturePath.empty()) {
                            OzoneLoader::Instance().ApplyRenderableTexture(
                                ridx, g_editorPanels.activeTexturePath.c_str());
                            EditorLog("Auto-applied texture to new brush %d: %s", ridx,
                                      g_editorPanels.activeTexturePath.c_str());
                        }
                    }
                }

            }

            // Tool modes (toolbar Cam/Move/Scale/Rotate) while placing:
            //   Cam (0)    â€” wheel shifts ghost depth (default, below)
            //   Move (1)   â€” LMB drag moves the ghost (same as Cam)
            //   Scale (2)  â€” wheel scales the ghost
            //   Rotate (3) â€” wheel rotates the ghost in 15 degree steps
            if (g_editorPanels.currentToolMode == 2) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0) OmegaTechEditor.S += wheel * 0.25f;
            } else if (g_editorPanels.currentToolMode == 3) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0) OmegaTechEditor.R += wheel * 15.0f;
            } else if (IsMouseButtonDown(0)) {
                // A selected entity moves only for an intentional gesture; a
                // placement ghost (no selection) still drags freely. Suppressed
                // entirely while the post-context-menu input guard is active.
                bool allowMove = !g_suppressViewportDrag &&
                    (g_sel.type == SelType::NONE || g_gizmoDrag);
                if (allowMove) {
                    if (g_sel.type != SelType::NONE && !g_gizmoHistPushed) {
                        HistoryPush();
                        g_gizmoHistPushed = true;
                    }
                    OmegaTechEditor.X += GetMouseDelta().x / 8;
                    OmegaTechEditor.Y += GetMouseDelta().y / 8;
                    OmegaTechEditor.Z -= (GetMouseWheelMove() * 2);
                }
            }
            if (IsMouseButtonDown(1) && !g_suppressViewportDrag) {
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
                auto& zones = ZoneManager::Instance().GetZones();
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
                if (PlayerStartNode* s = FindPlayerStartById(sel.index))
                    return {{s->position.x-0.6f,s->position.y-0.5f,s->position.z-0.6f},
                            {s->position.x+0.6f,s->position.y+1.2f,s->position.z+0.6f}};
            } else if (sel.type == SelType::PORTAL) {
                auto& portals = ZoneManager::Instance().GetPortals();
                if (sel.index >= 0 && sel.index < (int)portals.size())
                    return portals[sel.index].bounds;
            } else if (sel.type == SelType::MESH) {
                MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(sel.index);
                if (n) {
                    if (n->mesh && n->mesh->Valid()) {
                        const BoundingBox& mb = n->mesh->Bounds();
                        return {{n->position.x + mb.min.x * n->scale,
                                 n->position.y + mb.min.y * n->scale,
                                 n->position.z + mb.min.z * n->scale},
                                {n->position.x + mb.max.x * n->scale,
                                 n->position.y + mb.max.y * n->scale,
                                 n->position.z + mb.max.z * n->scale}};
                    }
                    return {{n->position.x - 1, n->position.y - 1, n->position.z - 1},
                            {n->position.x + 1, n->position.y + 1, n->position.z + 1}};
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
                    else if(cmd==8) ToggleHeightmapEditor(); else if(cmd==9) ToggleAnimPanel();
                    else if(cmd==10) { FileNew(); } else if(cmd==11) { FileOpen(); }
                    else if(cmd==12) { FileSaveAs(); }
                }
                bx += bw + 2;
            };

            tBtn(nullptr,"New",10); tBtn(nullptr,"Open",11); tBtn(nullptr,"Save",12);
            tBtn(nullptr,"Anim",9);
            // Play button
            {
                const char* lbl = "Play";
                int bw = (int)strlen(lbl) * 7 + 10;
                Rectangle r = {(float)bx, 2, (float)bw, (float)tbH - 4};
                bool hover = CheckCollisionPointRec(GetMousePosition(), r);
                DrawRectangleRec(r, hover ? (Color){50,100,50,255} : (Color){35,80,35,255});
                DrawText(lbl, bx + 4, 7, 12, WHITE);
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    // Compile the current document first (never load a stale file)
                    if (g_documentPath.empty()) {
                        FileSaveAs();
                    }
                    if (!g_documentPath.empty() && SaveWorldDocument(g_documentPath)) {
                        std::string worldDir = g_documentPath.parent_path().filename().string();
                        std::string ext = g_documentPath.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::string worldArg;
                        if (ext == ".ozone") {
                            worldArg = g_documentPath.string();
                        }
                        if (!worldArg.empty()) {
                            std::string cmd = "start \"\" System\\Angels95.exe --world \"" + worldArg + "\" --world-dir " + worldDir;
                            system(cmd.c_str());
                            EditorLog("Launched: %s", cmd.c_str());
                        }
                    } else if (!g_documentPath.empty()) {
                        EditorLog("ERROR: could not save '%s' for playtest", g_documentPath.string().c_str());
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
                        Shader targetShader;
                        if (OTEditor.ViewMode == LightingMode::LIT || OTEditor.ViewMode == LightingMode::WIREFRAME)
                            targetShader = OTEditor.LitFogShader;
                        else
                            targetShader = OTEditor.UnlitShader;
                        if (targetShader.id > 0) {
                            for (auto& m : WDLModels.models)
                                if (m.loaded && m.model.meshCount > 0)
                                    m.model.materials[0].shader = targetShader;
                            if (WDLModels.HeightMapReady)
                                WDLModels.HeightMap.materials[0].shader = targetShader;
                            OzoneLoader::Instance().SetLitFogShader(targetShader);
                        }
                    }
                }
                bx+=lw+2;
            }
            // Skybox visibility toggle
            {
                int lw = 42;
                bool on = OTEditor.ShowSkybox;
                Color lc = on ? (Color){70,90,120,255} : (Color){45,45,50,255};
                DrawRectangle(bx, 2, lw, tbH-4, lc);
                DrawText("Sky", bx+6, 7, 12, on ? WHITE : LIGHTGRAY);
                if (CheckCollisionPointRec(GetMousePosition(), {(float)bx, 2, (float)lw, (float)tbH-4}) &&
                    IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    OTEditor.ShowSkybox = !OTEditor.ShowSkybox;
                    EditorLog("Viewport skybox %s", OTEditor.ShowSkybox ? "shown" : "hidden");
                }
                bx += lw + 2;
            }
            bx+=6;
            tBtn("AddVolume","Zone",5); tBtn("ModeCamera","Node",7);
            tBtn("PolyTexInfo","Pickup",6);
            bx += 6;
            // Terrain editing buttons
            bool terrainMode = (g_placeMode == PlaceMode::TERRAIN);
            Color terrainColor = terrainMode ? (Color){90,70,50,255} : (Color){45,45,50,255};
            int tbW = 76;
            DrawRectangle(bx, 2, tbW, tbH-4, terrainColor);
            if (ico.Has("BBTerrain")) ico.Draw("BBTerrain", bx+3, 4, 20, WHITE);
            DrawText("Raise", bx+27, 7, 12, terrainMode ? WHITE : LIGHTGRAY);
            if (CheckCollisionPointRec(GetMousePosition(), {(float)bx, 2, (float)tbW, (float)tbH-4}) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                g_placeMode = PlaceMode::TERRAIN;
                g_editorPanels.terrainBrushMode = 0; // raise
            }
            bx += tbW + 2;
            DrawRectangle(bx, 2, tbW, tbH-4, terrainColor);
            if (ico.Has("BBTerrain")) ico.Draw("BBTerrain", bx+3, 4, 20, WHITE);
            DrawText("Lower", bx+27, 7, 12, terrainMode ? WHITE : LIGHTGRAY);
            if (CheckCollisionPointRec(GetMousePosition(), {(float)bx, 2, (float)tbW, (float)tbH-4}) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                g_placeMode = PlaceMode::TERRAIN;
                g_editorPanels.terrainBrushMode = 1; // lower
            }
            bx += tbW + 2;
            // Brush size adjustment buttons
            char sizeTxt[32];
            snprintf(sizeTxt, sizeof(sizeTxt), "Sz:%d", g_editorPanels.terrainBrushSize);
            DrawText(sizeTxt, bx+4, 7, 12, LIGHTGRAY);
            int szW = (int)strlen(sizeTxt) * 7 + 10;
            Rectangle szR = {(float)bx, 2, (float)szW, (float)tbH-4};
            if (CheckCollisionPointRec(GetMousePosition(), szR)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    g_editorPanels.terrainBrushSize = (g_editorPanels.terrainBrushSize % 10) + 1;
            }
            bx += szW + 2;
            // Brush strength
            char strTxt[32];
            snprintf(strTxt, sizeof(strTxt), "Str:%.1f", g_editorPanels.terrainBrushStrength);
            DrawText(strTxt, bx+4, 7, 12, LIGHTGRAY);
            int stW = (int)strlen(strTxt) * 7 + 10;
            Rectangle stR = {(float)bx, 2, (float)stW, (float)tbH-4};
            if (CheckCollisionPointRec(GetMousePosition(), stR)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    static float strengths[] = {0.01f, 0.03f, 0.05f, 0.1f, 0.2f};
                    static int si = 2;
                    si = (si + 1) % 5;
                    g_editorPanels.terrainBrushStrength = strengths[si];
                }
            }
            bx += stW + 2;
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

        // WorldGraph selection handler â€” set g_sel from explorer double-click
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
        // WorldGraph context menu: open properties
        if (g_editorPanels.actionWorldGraphProperties >= 0) {
            OpenPropertiesForSelection();
            g_editorPanels.actionWorldGraphProperties = -1;
        }
        // WorldGraph context menu: delete
        if (g_editorPanels.actionWorldGraphDelete >= 0) {
            if (g_sel.type != SelType::NONE) DeleteSelectedEntity();
            g_editorPanels.actionWorldGraphDelete = -1;
        }
        // WorldGraph context menu: duplicate
        if (g_editorPanels.actionWorldGraphDup >= 0) {
            if (g_sel.type != SelType::NONE) DuplicateSelectedEntity();
            g_editorPanels.actionWorldGraphDup = -1;
        }

        // --- Surface Properties apply / reset ------------------------------
        //
        // The dialog hands over the working copy it was showing. Both the
        // per-face mask and the brush index come from the panel, never from the
        // mouse, so an Apply that lands a frame late still writes to the faces
        // the user was looking at.
        if (g_editorPanels.actionApplySurface) {
            g_editorPanels.actionApplySurface = false;
            const int rIdx = g_editorPanels.surfaceRenderable;
            OzoneRenderable* r = OzoneLoader::Instance().Get(rIdx);
            if (!r) {
                EditorLog("Surface: apply ignored - renderable %d no longer exists", rIdx);
            } else if (g_editorPanels.surfaceFaceMask == 0) {
                // Guarded on purpose: an empty mask must never be treated as
                // "apply to all six faces".
                EditorLog("Surface: apply ignored - no face selected");
            } else {
                HistoryPush();
                for (int f = 0; f < oz::surface::FACE_COUNT; f++) {
                    if (!(g_editorPanels.surfaceFaceMask & (1u << f))) continue;
                    OzoneLoader::Instance().SetRenderableFace(
                        rIdx, (oz::surface::SurfaceFace)f, g_editorPanels.surfaceEdit);
                }
                EditorLog("Surface: applied to %d face(s) of renderable %d (flags=0x%X)",
                          __builtin_popcount(g_editorPanels.surfaceFaceMask),
                          rIdx, g_editorPanels.surfaceEdit.flags);
                SurfacePropsRefresh((HWND)g_editorPanels.hSurfaceProps);
            }
        }
        if (g_editorPanels.actionResetSurface) {
            g_editorPanels.actionResetSurface = false;
            const int rIdx = g_editorPanels.surfaceRenderable;
            OzoneRenderable* r = OzoneLoader::Instance().Get(rIdx);
            if (r && g_editorPanels.surfaceFaceMask != 0) {
                HistoryPush();
                for (int f = 0; f < oz::surface::FACE_COUNT; f++) {
                    if (!(g_editorPanels.surfaceFaceMask & (1u << f))) continue;
                    r->surface.ClearFace((oz::surface::SurfaceFace)f);
                }
                // Face meshes only exist while a brush is decorated, so
                // dropping the last override has to release them.
                OzoneLoader::Instance().RebuildSurfaceMeshes(rIdx);
                EditorLog("Surface: reset %d face(s) of renderable %d",
                          __builtin_popcount(g_editorPanels.surfaceFaceMask), rIdx);
                SurfacePropsRefresh((HWND)g_editorPanels.hSurfaceProps);
            }
        }

        // Properties apply handler â€” write values back from native panel
        if (g_editorPanels.actionApplyProperties) {
            HistoryPush();
            float px = g_editorPanels.propPosX;
            float py = g_editorPanels.propPosY;
            float pz = g_editorPanels.propPosZ;
            float prot = g_editorPanels.propRotation;
            int tgtIdx = g_editorPanels.propsTargetIndex;
            SelType tgtType = (SelType)g_editorPanels.propsTargetType;

            if (tgtType == SelType::NPC) {
                Pawn* p = PawnSystem::Instance().Get(tgtIdx);
                if (p) {
                    p->position = {px, py, pz};
                    // Instance overrides (def values stay in PawnDefs/.ozls)
                    int h = (int)g_editorPanels.propHealth;
                    if (h < 1) h = 1;
                    if (h > p->maxHealth) h = p->maxHealth;
                    p->health = h;
                    if (g_editorPanels.propSpeed > 0.01f) p->speed = g_editorPanels.propSpeed;
                }
            } else if (tgtType == SelType::PICKUP) {
                auto& pickups = PawnSystem::Instance().GetPickups();
                for (auto& pk : pickups) {
                    if ((int)pk.id == tgtIdx) {
                        pk.position = {px, py, pz};
                        if (g_editorPanels.propRespawnTime >= 0.0f)
                            pk.respawnTime = g_editorPanels.propRespawnTime;
                        break;
                    }
                }
            } else if (tgtType == SelType::BRUSH) {
                float sx = g_editorPanels.propSizeX;
                float sy = g_editorPanels.propSizeY;
                float sz = g_editorPanels.propSizeZ;
                if (sx < 0.01f) sx = 1.0f;
                if (sy < 0.01f) sy = 1.0f;
                if (sz < 0.01f) sz = 1.0f;
                Vector3 newSize = {sx, sy, sz};
                // Resolve renderable index: try direct, then find by AABB
                int rIdx = -1;
                if (tgtIdx >= 0 && tgtIdx < OzoneLoader::Instance().Count()) {
                    rIdx = tgtIdx;
                } else {
                    rIdx = OzoneLoader::Instance().FindRenderableByCollisionVol(tgtIdx);
                }
                if (rIdx >= 0) {
                    OzoneLoader::Instance().UpdateBrushRenderable(
                        rIdx, (Vector3){px, py, pz}, newSize, prot);
                    OzoneLoader::Instance().ApplyRenderableUV(
                        rIdx,
                        g_editorPanels.propTexScaleU,
                        g_editorPanels.propTexScaleV,
                        g_editorPanels.propTexOffsetU,
                        g_editorPanels.propTexOffsetV);
                }
                // Rebuild collision volumes from the updated renderable
                OzoneLoader::Instance().RebuildCollisionVolumes();
            } else if (tgtType == SelType::ZONE) {
                auto& zones = ZoneManager::Instance().GetZones();
                for (auto& zone : zones) {
                    if ((int)zone.id == tgtIdx) {
                        float szx = g_editorPanels.propSizeX;
                        float szy = g_editorPanels.propSizeY;
                        float szz = g_editorPanels.propSizeZ;
                        zone.bounds.min = {px - szx*0.5f, py - szy*0.5f, pz - szz*0.5f};
                        zone.bounds.max = {px + szx*0.5f, py + szy*0.5f, pz + szz*0.5f};
                        // Type/intensity/script-hook name (exported by ExportToOzone)
                        int zt = g_editorPanels.propZoneType;
                        if (zt < 0 || zt > 4) zt = 0;
                        zone.zoneType = (ZoneType)zt;
                        if (g_editorPanels.propZoneIntensity > 0.0f)
                            zone.intensity = g_editorPanels.propZoneIntensity;
                        if (!g_editorPanels.propZoneName.empty() &&
                            g_editorPanels.propZoneName != zone.name) {
                            // Rename the matching sky-zone node too so runtime
                            // script hooks (on_enter/on_exit) follow the new name.
                            std::string oldName = zone.name;
                            for (auto& sky : PawnSystem::Instance().GetSkyZones())
                                if (sky.name == oldName) sky.name = g_editorPanels.propZoneName;
                            zone.name = g_editorPanels.propZoneName;
                        }
                        // Per-zone physics overrides (exported by ExportToOzone)
                        zone.physics.gravity = g_editorPanels.propZoneGravity;
                        zone.physics.jumpSpeed = g_editorPanels.propZoneJump;
                        zone.physics.terminalVelocity = g_editorPanels.propZoneTerminal;
                        zone.physics.waterGravity = g_editorPanels.propZoneWaterGravity;
                        zone.physics.waterDrag = g_editorPanels.propZoneWaterDrag;
                        zone.physics.swimUpSpeed = g_editorPanels.propZoneSwimUp;
                        zone.physics.ladderSpeed = g_editorPanels.propZoneLadderSpeed;
                        zone.physics.flySpeedMult = g_editorPanels.propZoneFlyMult;
                        break;
                    }
                }
            } else if (tgtType == SelType::SPAWN) {
                if (PlayerStartNode* s = FindPlayerStartById(tgtIdx)) {
                    s->position = {px, py, pz};
                    // Only when the selection actually carried a yaw. The Rot row
                    // is seeded from propsTargetRotation, which defaults to 0 for
                    // any selection whose raycast did not populate it, so
                    // writing it unconditionally turned a PlayerStart's authored
                    // `playerstart` yaw into 0 on every Apply.
                    if (g_editorPanels.propsTargetHasRotation) s->yaw = prot;
                }
            } else if (tgtType == SelType::LIGHT) {
                if (LightNode* l = PawnSystem::Instance().GetLight(tgtIdx)) {
                    l->position = {px, py, pz};
                    l->name = g_editorPanels.propLightName;
                    l->color = (Color){(unsigned char)ClampPropInt(g_editorPanels.propLightR, 0, 255),
                                       (unsigned char)ClampPropInt(g_editorPanels.propLightG, 0, 255),
                                       (unsigned char)ClampPropInt(g_editorPanels.propLightB, 0, 255),
                                       255};
                    l->intensity = fmaxf(0.0f, g_editorPanels.propLightIntensity);
                    l->radius = fmaxf(0.1f, g_editorPanels.propLightRadius);
                    int lt = g_editorPanels.propLightType;
                    if (lt < 0 || lt > 2) lt = (int)LitLightType::POINT;
                    l->type = (LitLightType)lt;
                    int le = g_editorPanels.propLightEffect;
                    if (le < 0 || le > 4) le = 0;
                    l->effect = (LitLightEffect)le;
                    // The panel edits the cone in degrees; LightNode stores
                    // cos(half-angle) because that is what the shader compares
                    // against, so convert on the way in.
                    float innerDeg = fminf(fmaxf(g_editorPanels.propLightInnerAngle, 0.5f), 89.0f);
                    float outerDeg = fminf(fmaxf(g_editorPanels.propLightOuterAngle, 1.0f), 89.0f);
                    l->innerCone = cosf(innerDeg * DEG2RAD);
                    l->outerCone = cosf(outerDeg * DEG2RAD);
                    l->flare  = g_editorPanels.propLightFlare;
                    l->corona = g_editorPanels.propLightCorona;
                    l->target = {g_editorPanels.propLightTarget[0],
                                 g_editorPanels.propLightTarget[1],
                                 g_editorPanels.propLightTarget[2]};
                    // A directional light is authored by its SOURCE point and
                    // aimed at the world origin (see OzOzoneLoader), so the
                    // panel's target row must be forced back to the origin or
                    // the exported `light directional` line stops meaning what
                    // the editor shows.
                    if (l->type == LitLightType::DIRECTIONAL)
                        l->target = {0.0f, 0.0f, 0.0f};
                    // Lights are bound to the zone volume that contains them;
                    // a moved light needs that recomputed or it keeps lighting
                    // the volume it used to sit in.
                    PawnSystem::Instance().AssignLightZones();
                }
            } else if (tgtType == SelType::PORTAL) {
                auto& portals = ZoneManager::Instance().GetPortals();
                if (tgtIdx >= 0 && tgtIdx < (int)portals.size()) {
                    auto& p = portals[tgtIdx];
                    float szx = g_editorPanels.propSizeX;
                    float szy = g_editorPanels.propSizeY;
                    float szz = g_editorPanels.propSizeZ;
                    p.bounds.min = {px - szx*0.5f, py - szy*0.5f, pz - szz*0.5f};
                    p.bounds.max = {px + szx*0.5f, py + szy*0.5f, pz + szz*0.5f};
                    // Destination fields (exported by ExportToOzone)
                    if (!g_editorPanels.propPortalWorld.empty())
                        p.targetWorld = g_editorPanels.propPortalWorld;
                    p.targetSpawn = {g_editorPanels.propPortalSpawn[0],
                                     g_editorPanels.propPortalSpawn[1],
                                     g_editorPanels.propPortalSpawn[2]};
                    p.bidirectional = g_editorPanels.propPortalBidir;
                }
            } else if (tgtType == SelType::MESH) {
                MeshObjectNode* m = PawnSystem::Instance().GetMeshObject(tgtIdx);
                if (m) {
                    m->position = {px, py, pz};
                    if (g_editorPanels.propsTargetHasRotation) m->yaw = prot;
                    if (g_editorPanels.propScale > 0.001f) m->scale = g_editorPanels.propScale;
                    bool pathChanged = (g_editorPanels.propMeshPath != m->meshPath) ||
                                       (g_editorPanels.propMeshTex != m->texturePath);
                    if (!g_editorPanels.propMeshPath.empty())
                        m->meshPath = g_editorPanels.propMeshPath;
                    m->texturePath = g_editorPanels.propMeshTex;
                    bool animChanged = (g_editorPanels.propMeshAnimFile != m->animFile);
                    m->animFile = g_editorPanels.propMeshAnimFile;
                    if (g_editorPanels.propMeshAnimSpeed > 0.0f)
                        m->animSpeed = g_editorPanels.propMeshAnimSpeed;
                    // Embedded clip name only matters when there's no external clip.
                    if (m->animFile.empty() && m->skeletal)
                        m->animClip = g_editorPanels.propAnimClip;
                    if (!m->animFile.empty()) m->skeletal = true;
                    m->windAffected = g_editorPanels.propMeshWind;
                    if (pathChanged || animChanged) m->mesh.reset(); // re-resolve
                }
            } else if (tgtType == SelType::PARTICLE) {
                ParticleEmitterNode* e = PawnSystem::Instance().GetParticleEmitter(tgtIdx);
                if (e) {
                    e->position = {px, py, pz};
                    if (g_editorPanels.propsTargetHasRotation) e->yaw = prot;
                    if (!g_editorPanels.propEmitterType.empty()) e->type = g_editorPanels.propEmitterType;
                    e->texturePath = g_editorPanels.propEmitterTex;
                    if (g_editorPanels.propEmitterRate >= 0.0f) e->rate = g_editorPanels.propEmitterRate;
                    if (g_editorPanels.propEmitterLife > 0.0f) e->lifetime = g_editorPanels.propEmitterLife;
                    if (g_editorPanels.propEmitterSpeed >= 0.0f) e->speed = g_editorPanels.propEmitterSpeed;
                    if (g_editorPanels.propEmitterSize > 0.0f) e->sizeStart = g_editorPanels.propEmitterSize;
                    e->spread = g_editorPanels.propEmitterSpread;
                    e->colorStart.r = (unsigned char)g_editorPanels.propEmitterR;
                    e->colorStart.g = (unsigned char)g_editorPanels.propEmitterG;
                    e->colorStart.b = (unsigned char)g_editorPanels.propEmitterB;
                }
            } else if (tgtType == SelType::PATHNODE) {
                PathNode* pn = PawnSystem::Instance().GetPathNode(tgtIdx);
                if (pn) {
                    pn->position = {px, py, pz};
                    if (g_editorPanels.propPathRadius > 0.0f) pn->radius = g_editorPanels.propPathRadius;
                    // Rename â€” retarget any links that referenced the old name
                    std::string newName = g_editorPanels.propPathName;
                    if (!newName.empty() && newName != pn->name) {
                        std::string oldName = pn->name;
                        pn->name = newName;
                        for (auto& other : PawnSystem::Instance().GetPathNodes())
                            for (auto& link : other.next)
                                if (link == oldName) link = newName;
                    }
                    // Parse comma-separated successor list
                    pn->next.clear();
                    std::string ns = g_editorPanels.propPathNext;
                    size_t start = 0;
                    while (start <= ns.size()) {
                        size_t comma = ns.find(',', start);
                        std::string part = ns.substr(
                            start, comma == std::string::npos ? std::string::npos : comma - start);
                        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.erase(part.begin());
                        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
                        if (!part.empty()) pn->next.push_back(part);
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                    pn->loop = g_editorPanels.propPathLoop;
                }
            } else if (tgtType == SelType::WINDZONE) {
                WindZoneNode* z = PawnSystem::Instance().GetWindZone(tgtIdx);
                if (z) {
                    float sx = g_editorPanels.propWindSizeX;
                    float sy = g_editorPanels.propWindSizeY;
                    float sz = g_editorPanels.propWindSizeZ;
                    if (sx < 0.01f) sx = 1.0f;
                    if (sy < 0.01f) sy = 1.0f;
                    if (sz < 0.01f) sz = 1.0f;
                    z->bounds.min = {px - sx * 0.5f, py - sy * 0.5f, pz - sz * 0.5f};
                    z->bounds.max = {px + sx * 0.5f, py + sy * 0.5f, pz + sz * 0.5f};
                    z->direction = {g_editorPanels.propWindDirX,
                                    g_editorPanels.propWindDirY,
                                    g_editorPanels.propWindDirZ};
                    if (g_editorPanels.propWindStrength >= 0.0f)
                        z->strength = g_editorPanels.propWindStrength;
                    if (g_editorPanels.propWindFrequency > 0.0f)
                        z->frequency = g_editorPanels.propWindFrequency;
                }
            }
            EditorLog("Applied properties to %s idx=%d", g_sel.name.c_str(), tgtIdx);
            g_editorPanels.actionApplyProperties = false;
        }

        // --- Editable .ozls stat rows ---------------------------------------
        //
        // Separate from the SelType dispatch above on purpose: the stat rows
        // belong to the DEF, not to the world instance being selected, and the
        // Script Manager can reach them too. The patcher only touches the
        // `stats { }` block, so this is independent of every in-memory edit
        // above and cannot disturb them.
        if (!g_editorPanels.propDefPendingEdits.empty()) {
            std::vector<ozls::StatEdit> edits;
            edits.reserve(g_editorPanels.propDefPendingEdits.size());
            for (const auto& e : g_editorPanels.propDefPendingEdits) {
                if (e.key.empty()) continue;
                ozls::StatKind kind = ozls::StatKind::String;
                // Infer the kind from the authored value: a parenthesised
                // triple is a vec3, a bare number is a float, anything else a
                // string. The writer files it back under the matching map, so
                // Parse reads it the same way next time.
                if (e.value.size() > 2 && e.value.front() == '(' && e.value.back() == ')') {
                    kind = ozls::StatKind::Vec3;
                } else {
                    char* end = nullptr;
                    const float v = std::strtof(e.value.c_str(), &end);
                    if (end && *end == '\0' && !e.value.empty()) {
                        (void)v;
                        kind = ozls::StatKind::Float;
                    }
                }
                edits.push_back({e.key, e.value, kind});
            }

            if (edits.empty()) {
                g_editorPanels.propDefPendingEdits.clear();
            } else if (!g_editorPanels.propDefWritable || g_editorPanels.propDefPath.empty()) {
                // Packaged def: there is no source file to patch. The panel
                // already shows these rows read-only, so reaching here means the
                // state changed underneath us; say so rather than pretend.
                EditorLog("Stat edits skipped: '%s' has no editable source file",
                          g_editorPanels.propDefPath.c_str());
                g_editorPanels.propDefPendingEdits.clear();
            } else {
                const ozls::PatchResult res =
                    ozls::PatchOzlsStats(g_editorPanels.propDefPath, edits);
                if (res.ok) {
                    EditorLog("Patched %s: %d updated, %d inserted, %d erased",
                              g_editorPanels.propDefPath.c_str(),
                              res.updated, res.inserted, res.erased);
                    // Re-read the defs so the panel's read-only dump and the
                    // registry agree with the file immediately.
                    LightningEntityRegistry::Instance().Init();
                    // The defs just changed on disk, so any icon EngineBillboard
                    // memoised for these names is now stale.
                    EngineBillboard::InvalidateIconCache();
                    SendMessage((HWND)g_editorPanels.hPropsPanel, WM_USER + 50, 0, 0);
                } else {
                    EditorLog("Stat patch FAILED for %s: %s",
                              g_editorPanels.propDefPath.c_str(), res.error.c_str());
                    MessageBoxA(nullptr, res.error.c_str(),
                                "Could not write .ozls", MB_OK | MB_ICONERROR);
                }
                g_editorPanels.propDefPendingEdits.clear();
            }
        }

        // Preview a sound stat without saving.
        if (!g_editorPanels.propDefPreviewSound.empty()) {
            SoundManager::Instance().PlayStatSound(g_editorPanels.propDefPreviewSound);
            g_editorPanels.propDefPreviewSound.clear();
        }
        // "Reload Mesh" â€” drop the cached asset so it re-resolves next draw
        if (g_editorPanels.actionReloadMesh) {
            if (MeshObjectNode* m = PawnSystem::Instance().GetMeshObject(g_sel.index)) {
                if (g_sel.type == SelType::MESH) m->mesh.reset();
            }
            g_editorPanels.actionReloadMesh = false;
        }

        // ---- Animation tool (Phase B) --------------------------------------
        // Track the selected mesh as the animation target.
        {
            int selMesh = (g_sel.type == SelType::MESH) ? g_sel.index : -1;
            if (selMesh != g_editorPanels.animTargetMesh) {
                // Release the previous target so it resumes normal playback.
                if (MeshObjectNode* old = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh)) {
                    old->animPaused = false;
                    old->editPose.reset();
                }
                g_editorPanels.animTargetMesh = selMesh;
                g_editorPanels.animPlaying = false;
                g_editorPanels.animEditVerts = false;
                g_editorPanels.animSelVerts.clear();
                g_editorPanels.animTime = 0.0f;
                EnsureMeshNodeLoaded(PawnSystem::Instance().GetMeshObject(selMesh));
                RefreshAnimPanel();
            }
        }
        // Convert a static mesh into a vertex-keyframe animated one.
        if (g_editorPanels.actionConvertToAnimated) {
            g_editorPanels.actionConvertToAnimated = false;
            MeshObjectNode* n = (g_sel.type == SelType::MESH)
                ? PawnSystem::Instance().GetMeshObject(g_sel.index) : nullptr;
            if (n && n->animFile.empty()) {
                std::string file = n->meshPath;
                size_t slash = file.find_last_of("/\\");
                if (slash != std::string::npos) file = file.substr(slash + 1);
                size_t dot = file.rfind('.');
                if (dot != std::string::npos) file = file.substr(0, dot);
                if (file.empty()) file = "anim";
                std::string path = "GameData/Global/Anims/" + file + ".ozanim";
                std::error_code ec;
                fs::create_directories("GameData/Global/Anims", ec);
                if (!fs::exists(path)) {
                    ozanim::Animation a;
                    ozanim::Clip c; c.name = "Idle"; c.fps = 30.0f; c.loop = true;
                    a.clips.push_back(c);
                    std::ofstream out(path);
                    if (out.is_open()) out << ozanim::Serialize(a);
                }
                n->animFile = path;
                n->skeletal = true;
                n->animTime = 0.0f;
                n->animPaused = true;
                n->mesh.reset();
                EnsureMeshNodeLoaded(n);
                g_editorPanels.animTargetMesh = (int)n->id;
                g_editorPanels.animClipName = "Idle";
                ShowAnimPanel(true);
                RefreshAnimPanel();
                EditorLog("Converted '%s' to animated -> %s", n->meshPath.c_str(), path.c_str());
            } else {
                EditorLog("Convert: select a GameEngine.Mesh with no anim file");
            }
        }
        // New / delete clip
        if (g_editorPanels.actionAnimNewClip) {
            g_editorPanels.actionAnimNewClip = false;
            if (oz::AnimatedMesh* am = AnimTarget()) {
                static int s_clipN = 1;
                AnimSnapshotPush();
                ozanim::Clip c;
                c.name = "Clip" + std::to_string(s_clipN++);
                c.fps = g_editorPanels.animFps;
                c.loop = g_editorPanels.animLoop;
                am->MutableAnimation().clips.push_back(c);
                g_editorPanels.animClipName = c.name;
                g_editorPanels.animTime = 0.0f;
                g_editorPanels.actionAnimSave = true;
            }
        }
        if (g_editorPanels.actionAnimDeleteClip) {
            g_editorPanels.actionAnimDeleteClip = false;
            if (oz::AnimatedMesh* am = AnimTarget()) {
                auto& clips = am->MutableAnimation().clips;
                int idx = am->FindClip(g_editorPanels.animClipName);
                if (idx >= 0 && idx < (int)clips.size()) {
                    AnimSnapshotPush();
                    clips.erase(clips.begin() + idx);
                    g_editorPanels.animClipName = clips.empty() ? "" : clips.front().name;
                    g_editorPanels.animTime = 0.0f;
                    g_editorPanels.actionAnimSave = true;
                }
            }
        }
        // Add / delete a key at the current time (Phase C adds vertex offsets).
        if (g_editorPanels.actionAnimAddKey) {
            g_editorPanels.actionAnimAddKey = false;
            MeshObjectNode* kn = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
            if (oz::AnimatedMesh* am = AnimTarget()) {
                if (ozanim::Clip* c = am->MutableAnimation().FindClip(g_editorPanels.animClipName)) {
                    AnimSnapshotPush();
                    float t = g_editorPanels.animTime;
                    ozanim::Keyframe* kf = nullptr;
                    for (auto& k : c->keys) if (fabsf(k.time - t) < 1e-3f) kf = &k;
                    if (!kf) {
                        ozanim::Keyframe k; k.time = t;
                        c->keys.push_back(k);
                        std::sort(c->keys.begin(), c->keys.end(),
                                  [](const ozanim::Keyframe& a, const ozanim::Keyframe& b) { return a.time < b.time; });
                        for (auto& k : c->keys) if (fabsf(k.time - t) < 1e-3f) kf = &k;
                    }
                    if (kf) {
                        int vc = am->TotalVertexCount();
                        // Source offsets: live edit pose if editing, else the clip sample.
                        std::vector<float> sampled;
                        std::vector<float>* offs = nullptr;
                        if (kn && kn->editPose) offs = kn->editPose.get();
                        else { c->SampleOffsets(t, vc, sampled); offs = &sampled; }

                        std::vector<int> verts;
                        if (g_editorPanels.animEditVerts && !g_editorPanels.animSelVerts.empty())
                            verts = g_editorPanels.animSelVerts;
                        else for (int i = 0; i < vc; i++) verts.push_back(i);

                        for (int vi : verts) {
                            if (vi < 0 || vi >= vc) continue;
                            float dx = (*offs)[vi * 3 + 0];
                            float dy = (*offs)[vi * 3 + 1];
                            float dz = (*offs)[vi * 3 + 2];
                            kf->offsets.erase(std::remove_if(kf->offsets.begin(), kf->offsets.end(),
                                [vi](const ozanim::VertexOffset& o) { return o.index == vi; }),
                                kf->offsets.end());
                            if (fabsf(dx) > 1e-5f || fabsf(dy) > 1e-5f || fabsf(dz) > 1e-5f)
                                kf->offsets.push_back({vi, dx, dy, dz});
                        }
                        std::sort(kf->offsets.begin(), kf->offsets.end(),
                                  [](const ozanim::VertexOffset& a, const ozanim::VertexOffset& b) { return a.index < b.index; });
                    }
                    g_editorPanels.actionAnimSave = true;
                }
            }
        }
        if (g_editorPanels.actionAnimDeleteKey) {
            g_editorPanels.actionAnimDeleteKey = false;
            if (oz::AnimatedMesh* am = AnimTarget()) {
                if (ozanim::Clip* c = am->MutableAnimation().FindClip(g_editorPanels.animClipName)) {
                    AnimSnapshotPush();
                    int best = -1; float bd = 1e9f;
                    for (int i = 0; i < (int)c->keys.size(); i++) {
                        float d = fabsf(c->keys[i].time - g_editorPanels.animTime);
                        if (d < bd) { bd = d; best = i; }
                    }
                    if (best >= 0 && bd < 0.05f) c->keys.erase(c->keys.begin() + best);
                    g_editorPanels.actionAnimSave = true;
                }
            }
        }
        // FPS / loop edited in the panel â†’ update the current clip.
        if (g_editorPanels.actionAnimApplyClipMeta) {
            g_editorPanels.actionAnimApplyClipMeta = false;
            if (oz::AnimatedMesh* am = AnimTarget()) {
                if (ozanim::Clip* c = am->MutableAnimation().FindClip(g_editorPanels.animClipName)) {
                    if (g_editorPanels.animFps > 0.0f) c->fps = g_editorPanels.animFps;
                    c->loop = g_editorPanels.animLoop;
                    g_editorPanels.actionAnimSave = true;
                }
            }
        }
        // Save the clip file back to disk.
        if (g_editorPanels.actionAnimSave) {
            g_editorPanels.actionAnimSave = false;
            MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
            if (oz::AnimatedMesh* am = AnimTarget(); am && n && !n->animFile.empty()) {
                std::error_code ec;
                fs::path p(n->animFile);
                if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
                std::ofstream out(n->animFile);
                if (out.is_open()) out << ozanim::Serialize(am->GetAnimation());
                EditorLog("Saved anim '%s'", n->animFile.c_str());
            }
            g_editorPanels.actionAnimRefresh = true;
        }
        // Scrub / playback: drive the target node's animTime.
        {
            MeshObjectNode* tn = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
            if (tn && !tn->animFile.empty()) {
                if (g_editorPanels.actionAnimScrub) {
                    g_editorPanels.actionAnimScrub = false;
                    float dur = AnimDuration();
                    g_editorPanels.animTime = (dur > 0.0f)
                        ? (g_editorPanels.animTimeSlider / 1000.0f) * dur : 0.0f;
                    tn->animTime = g_editorPanels.animTime;
                    g_editorPanels.animPlaying = false;
                }
                tn->animPaused = !g_editorPanels.animPlaying;
                if (g_editorPanels.animPlaying) g_editorPanels.animTime = tn->animTime;
            }
        }
        // Enter/exit vertex-edit mode.
        if (g_editorPanels.actionAnimToggleEdit) {
            g_editorPanels.actionAnimToggleEdit = false;
            MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
            oz::AnimatedMesh* am = AnimTarget();
            if (n && am) {
                if (!g_editorPanels.animEditVerts) {
                    std::vector<float> offs((size_t)am->TotalVertexCount() * 3, 0.0f);
                    if (const ozanim::Clip* c = am->GetAnimation().FindClip(g_editorPanels.animClipName))
                        c->SampleOffsets(g_editorPanels.animTime, am->TotalVertexCount(), offs);
                    n->editPose = std::make_shared<std::vector<float>>(std::move(offs));
                    n->animPaused = true;
                    g_editorPanels.animEditVerts = true;
                    g_editorPanels.animSelVerts.clear();
                    g_animUndo.clear();
                    g_animRedo.clear();
                    g_editorPanels.animPrevValid = false;
                } else {
                    n->editPose.reset();
                    g_editorPanels.animEditVerts = false;
                    g_editorPanels.animSelVerts.clear();
                }
            }
            g_editorPanels.actionAnimRefresh = true;
        }
        if (g_editorPanels.actionAnimSelectAll) {
            g_editorPanels.actionAnimSelectAll = false;
            if (g_editorPanels.animEditVerts) {
                if (oz::AnimatedMesh* am = AnimTarget()) {
                    g_editorPanels.animSelVerts.clear();
                    for (int i = 0; i < am->TotalVertexCount(); i++)
                        g_editorPanels.animSelVerts.push_back(i);
                }
            }
        }
        if (g_editorPanels.actionAnimClearSel) {
            g_editorPanels.actionAnimClearSel = false;
            g_editorPanels.animSelVerts.clear();
        }
        if (g_editorPanels.actionAnimUndo) {
            g_editorPanels.actionAnimUndo = false;
            AnimUndo();
            g_editorPanels.actionAnimRefresh = true;
        }
        if (g_editorPanels.actionAnimRedo) {
            g_editorPanels.actionAnimRedo = false;
            AnimRedo();
            g_editorPanels.actionAnimRefresh = true;
        }
        // Ctrl+Z / Ctrl+Y while the anim tool is active.
        if (g_editorPanels.animEditVerts &&
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            if (IsKeyPressed(KEY_Z)) g_editorPanels.actionAnimUndo = true;
            if (IsKeyPressed(KEY_Y)) g_editorPanels.actionAnimRedo = true;
        }
        if (g_editorPanels.actionAnimRefresh) {
            g_editorPanels.actionAnimRefresh = false;
            RefreshAnimPanel();
        }

        // Apply active texture to selected entity (from context menu)
        if (g_editorPanels.actionApplyTextureToSel) {
            if (!g_editorPanels.activeTexturePath.empty() && g_sel.type != SelType::NONE) {
                HistoryPush();
                if (g_sel.type == SelType::BRUSH) {
                    // g_sel.index may be a renderable index or a collision-volume
                    // index depending on which raycast produced it. Resolve to the
                    // renderable (the export reads renderables, not collision vols).
                    int rIdx = -1;
                    if (g_sel.index >= 0 && g_sel.index < OzoneLoader::Instance().Count()) {
                        // Direct renderable hit â€” verify it matches the selection point
                        OzoneRenderable* r = OzoneLoader::Instance().Get(g_sel.index);
                        if (r && r->loaded) {
                            BoundingBox b = GetMeshBoundingBox(r->model.meshes[0]);
                            Vector3 mn = {r->position.x + b.min.x * r->scale,
                                          r->position.y + b.min.y * r->scale,
                                          r->position.z + b.min.z * r->scale};
                            Vector3 mxn = {r->position.x + b.max.x * r->scale,
                                           r->position.y + b.max.y * r->scale,
                                           r->position.z + b.max.z * r->scale};
                            const float eps = 0.75f;
                            if (g_sel.pos.x >= mn.x - eps && g_sel.pos.x <= mxn.x + eps &&
                                g_sel.pos.y >= mn.y - eps && g_sel.pos.y <= mxn.y + eps &&
                                g_sel.pos.z >= mn.z - eps && g_sel.pos.z <= mxn.z + eps)
                                rIdx = g_sel.index;
                        }
                    }
                    if (rIdx < 0)
                        rIdx = OzoneLoader::Instance().FindRenderableByCollisionVol(g_sel.index);
                    if (rIdx < 0)
                        rIdx = g_sel.index; // best effort
                    bool applied = OzoneLoader::Instance().ApplyRenderableTexture(
                        rIdx, g_editorPanels.activeTexturePath.c_str());
                    EditorLog("Applied texture to brush renderable idx=%d (sel=%d): %s",
                              rIdx, g_sel.index, applied ? "ok" : "FAILED");
                } else if (g_sel.type == SelType::MODEL && g_sel.index >= 0 && g_sel.index < CachedModelCounter) {
                    int mid = CachedModels[g_sel.index].ModelId;
                    if (ApplyTextureToModel(mid, g_editorPanels.activeTexturePath.c_str()))
                        EditorLog("Applied texture to model %d: %s", mid,
                                  g_editorPanels.activeTexturePath.c_str());
                }
            }
            g_editorPanels.actionApplyTextureToSel = false;
        }

    #ifdef _WIN32
    // --- Model preview render-to-texture (for Win32 Model Browser dialog) ---
    // Rendered inside the current frame (after the main viewport pass) so the
    // offscreen pass is flushed by EndDrawing on every raylib version.
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
                // Only need triangle/vertex counts for the info line; guard the
                // "replace last 4 chars" so short/extensionless paths can't UB.
                Model mdl = LoadModelWithFallback(entry.path.c_str());
                if (mdl.meshes != nullptr) {
                    entry.triangles = mdl.meshes[0].triangleCount;
                    entry.vertices = mdl.meshes[0].vertexCount;
                }
                UnloadModel(mdl);
                entry.loaded = true;
            }
            Model mdl = LoadModelWithFallback(entry.path.c_str());
            // Companion texture: "<stem>_texture.png" / "<stem>.png" beside the model.
            std::string texPath, texPath2;
            if (entry.path.size() > 4) {
                texPath = entry.path;  texPath.replace(texPath.end() - 4, texPath.end(), "_texture.png");
                texPath2 = entry.path; texPath2.replace(texPath2.end() - 4, texPath2.end(), ".png");
            }
            Texture2D tex = {0};
            if (!texPath.empty() && fs::exists(texPath)) tex = LoadTextureWithFallback(texPath.c_str());
            else if (!texPath2.empty() && fs::exists(texPath2)) tex = LoadTextureWithFallback(texPath2.c_str());
            else {
                size_t dot = entry.path.rfind('.');
                if (dot != std::string::npos) {
                    std::string fext = entry.path.substr(dot);
                    std::transform(fext.begin(), fext.end(), fext.begin(), ::tolower);
                    if (fext == ".png" || fext == ".jpg" || fext == ".jpeg" || fext == ".bmp" || fext == ".tga")
                        tex = LoadTextureWithFallback(entry.path.c_str());
                }
            }
            if (tex.id > 0) mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;

            // Auto-frame the camera on the model bounds so any size is visible.
            Camera3D prevCam = {0};
            prevCam.up = {0, 1, 0};
            prevCam.fovy = 45.0f;
            prevCam.projection = CAMERA_PERSPECTIVE;
            Vector3 center = {0, 0, 0};
            float radius = 3.0f;
            if (mdl.meshCount > 0 && mdl.meshes != nullptr) {
                BoundingBox bb = GetModelBoundingBox(mdl);
                center = {(bb.min.x + bb.max.x) * 0.5f,
                          (bb.min.y + bb.max.y) * 0.5f,
                          (bb.min.z + bb.max.z) * 0.5f};
                Vector3 ext = {bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z};
                radius = fmaxf(ext.x, fmaxf(ext.y, ext.z)) * 0.5f;
                if (radius < 0.05f) radius = 0.05f;
            }
            float dist = radius * 3.0f;
            Vector3 dir = Vector3Normalize({1.0f, 0.7f, 1.0f});
            prevCam.target = center;
            prevCam.position = Vector3Add(center, Vector3Scale(dir, dist));

            BeginTextureMode(g_previewRT);
            ClearBackground((Color){40, 40, 50, 255});
            BeginMode3D(prevCam);
            DrawGrid(20, radius / 2.0f); // scale the grid to the model
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
        EndDrawing();

        // Keep the camera aim point available to Win32 panels, which spawn
        // entities synchronously (see Win32Dialogs SpawnSelectedPawnTreeItem).
        g_editorPanels.spawnPos[0] = OTEditor.MainCamera.target.x;
        g_editorPanels.spawnPos[1] = OTEditor.MainCamera.target.y;
        g_editorPanels.spawnPos[2] = OTEditor.MainCamera.target.z;

        // Handle action flags from Win32 dialogs
        if (g_editorPanels.actionPickupType >= 0) {
            g_placeMode = PlaceMode::PICKUP;
            // A placement ghost and a selection must not coexist, or the drag
            // gate can't tell which the user means to move.
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
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
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.ActiveNodeType = (EditorNodeType)g_editorPanels.actionNodeType;
            OmegaTechEditor.DrawModel = true;
            OmegaTechEditor.X = OTEditor.MainCamera.position.x;
            OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
            OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
            OmegaTechEditor.R = 1;
            g_editorPanels.actionNodeType = -1;
        }
        if (!g_editorPanels.actionSpawnPickup.empty()) {
            // Pawn Manager weapon/item leaf â†’ spawn the pickup immediately at the
            // camera aim point (the Pickups panel still offers ghosted placement).
            HistoryPush();
            PickupNode node;
            node.position = OTEditor.MainCamera.target;
            node.typeName = g_editorPanels.actionSpawnPickup;
            PawnSystem::Instance().AddPickup(node);
            EditorLog("Spawned pickup '%s' at camera target",
                      g_editorPanels.actionSpawnPickup.c_str());
            g_editorPanels.actionSpawnPickup.clear();
        }
        if (!g_editorPanels.actionSpawnMesh.empty()) {
            // GameEngine.Mesh placement â€” uses the model selected in the Model
            // Browser, spawned at the camera target as an OZONE Mesh.* entity.
            int idx = g_editorPanels.selectedModel;
            if (idx >= 0 && idx < (int)g_editorPanels.modelEntries.size()) {
                HistoryPush();
                MeshObjectNode node;
                node.meshPath = g_editorPanels.modelEntries[idx].path;
                node.skeletal = (g_editorPanels.actionSpawnMesh == "skeletal");
                node.position = OTEditor.MainCamera.target;
                node.yaw = 0.0f;
                node.scale = 1.0f;
                PawnSystem::Instance().AddMeshObject(node);
                EditorLog("Placed %s '%s'",
                          node.skeletal ? "Mesh.Skeletal" : "Mesh.Static",
                          node.meshPath.c_str());
            } else {
                EditorLog("Mesh placement: select a model in the Model Browser first");
            }
            g_editorPanels.actionSpawnMesh.clear();
        }
        if (g_editorPanels.actionSpawnParticleEmitter) {
            HistoryPush();
            ParticleEmitterNode node;
            node.type = "fire";
            node.position = OTEditor.MainCamera.target;
            node.direction = {0, 1, 0};
            node.rate = 30.0f;
            node.lifetime = 0.9f;
            node.speed = 2.0f;
            node.spread = 0.5f;
            node.sizeStart = 0.5f;
            node.sizeEnd = 0.0f;
            node.colorStart = {255, 170, 60, 255};
            node.colorEnd = {80, 20, 10, 0};
            node.radius = 0.2f;
            PawnSystem::Instance().AddParticleEmitter(node);
            EditorLog("Placed ParticleEmitter at camera target");
            g_editorPanels.actionSpawnParticleEmitter = false;
        }
        if (g_editorPanels.actionSpawnPathNode) {
            static int s_pathCounter = 0;
            HistoryPush();
            PathNode node;
            node.name = "path_" + std::to_string(s_pathCounter++);
            node.position = OTEditor.MainCamera.target;
            node.radius = 1.0f;
            PawnSystem::Instance().AddPathNode(node);
            EditorLog("Placed PathNode '%s' at camera target", node.name.c_str());
            g_editorPanels.actionSpawnPathNode = false;
        }
        if (g_editorPanels.actionSpawnWindZone) {
            WindZoneNode zone;
            Vector3 c = OTEditor.MainCamera.target;
            zone.bounds.min = {c.x - 5.0f, c.y - 5.0f, c.z - 5.0f};
            zone.bounds.max = {c.x + 5.0f, c.y + 5.0f, c.z + 5.0f};
            zone.direction = {1.0f, 0.0f, 0.0f};
            zone.strength = 1.0f;
            zone.frequency = 1.0f;
            HistoryPush();
            PawnSystem::Instance().AddWindZone(zone);
            EditorLog("Placed WindZone at camera target");
            g_editorPanels.actionSpawnWindZone = false;
        }
        if (g_editorPanels.actionSpawnPlayerStart) {
            HistoryPush();
            PlayerStartNode node;
            node.position = OTEditor.MainCamera.target;
            node.yaw = 0.0f;
            PawnSystem::Instance().AddPlayerStart(node);
            EditorLog("Placed PlayerStartNode at camera target");
            g_editorPanels.actionSpawnPlayerStart = false;
        }
        if (!g_editorPanels.actionSpawnEmitter.empty()) {
            HistoryPush();
            EmitterNode node;
            node.type = (g_editorPanels.actionSpawnEmitter == "music")
                ? EmitterType::MUSIC : EmitterType::SOUND;
            node.position = OTEditor.MainCamera.target;
            PawnSystem::Instance().AddEmitter(node);
            EditorLog("Placed %s EmitterNode at camera target",
                      g_editorPanels.actionSpawnEmitter.c_str());
            g_editorPanels.actionSpawnEmitter.clear();
        }
        if (g_editorPanels.actionSpawnZone >= 0) {
            HistoryPush();
            ZoneVolumeNode node;
            Vector3 c = OTEditor.MainCamera.target;
            node.bounds.min = {c.x - 4.0f, c.y - 2.0f, c.z - 4.0f};
            node.bounds.max = {c.x + 4.0f, c.y + 2.0f, c.z + 4.0f};
            node.zoneType = (ZoneType)g_editorPanels.actionSpawnZone;
            ZoneManager::Instance().AddZone(node);
            EditorLog("Placed ZoneVolumeNode (type=%d) at camera target",
                      g_editorPanels.actionSpawnZone);
            g_editorPanels.actionSpawnZone = -1;
        }
        if (g_editorPanels.actionPlaceModel >= 0) {
            g_placeMode = PlaceMode::MODEL;
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
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
            g_sel = { SelType::NONE, -1, "", {0,0,0} };
            g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
            OmegaTechEditor.DrawModel = true;
            int primType = g_editorPanels.actionCsgPlace;
            // Map primitive type to EMID
            EMID = 200 + primType;
            // Initialize placement ghost at camera target with default size
            OmegaTechEditor.X = OTEditor.MainCamera.target.x;
            OmegaTechEditor.Y = OTEditor.MainCamera.target.y;
            OmegaTechEditor.Z = OTEditor.MainCamera.target.z;
            OmegaTechEditor.W = 4.0f;
            OmegaTechEditor.H = 4.0f;
            OmegaTechEditor.L = 4.0f;
            OmegaTechEditor.S = 1.0f;
            OmegaTechEditor.R = 0.0f;
            g_editorPanels.actionCsgPlace = -1;
        }
        // CSG commit immediately (from Solid/Add/Sub/Inter buttons)
        if (g_editorPanels.actionCsgCommitNow >= 0) {
            if (g_placeMode == PlaceMode::MODEL) {
                HistoryPush();
                // Default to box if no primitive is selected
                if (EMID < 200) {
                    EMID = 200; // Box
                    OmegaTechEditor.X = OTEditor.MainCamera.target.x;
                    OmegaTechEditor.Y = OTEditor.MainCamera.target.y;
                    OmegaTechEditor.Z = OTEditor.MainCamera.target.z;
                    OmegaTechEditor.W = 4.0f;
                    OmegaTechEditor.H = 4.0f;
                    OmegaTechEditor.L = 4.0f;
                }
                // Same CSG placement logic as ENTER key (collision rebuilt via
                // OzoneLoader::RebuildCollisionVolumes inside CommitBrushRenderable)
                int primType = EMID - 200;
                Vector3 center = {OmegaTechEditor.X, OmegaTechEditor.Y, OmegaTechEditor.Z};
                Vector3 size = {OmegaTechEditor.W, OmegaTechEditor.H, OmegaTechEditor.L};
                int op = (int)OmegaTechEditor.CSGOperation;
                int ridx = CommitBrushRenderable(primType, center, size,
                                                 OmegaTechEditor.R, OmegaTechEditor.S, op);
                if (ridx >= 0) {
                    EditorLog("CSG commit: op=%d prim=%d at (%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f)",
                              op, primType, center.x, center.y, center.z, size.x, size.y, size.z);
                }
            }
            // Placing via the toolbox is done â€” drop the ghost so the viewport
            // returns to normal selection (Enter does the same for the ghost).
            if (g_sel.type == SelType::NONE) OmegaTechEditor.DrawModel = false;
            g_editorPanels.actionCsgCommitNow = -1;
        }
        if (g_editorPanels.actionRefreshBrowser) {
            ScanModelBrowserFiles();
            g_lastPreviewSel = -1; // force a fresh preview after the list rebuilds
            g_previewNeedsUpdate = false;
            g_editorPanels.actionRefreshBrowser = false;
        }
        // Heightmap generate handler
        if (g_editorPanels.actionGenerateHeightmap) {
            auto& img = g_editorPanels.actionHeightmapImage;
            auto& tex = g_editorPanels.actionHeightmapTexture;
            if (!img.empty()) {
                HistoryPush();
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
        // Legacy Light Properties apply handler. Superseded by the properties
        // panel (SelType::LIGHT), but kept so the old window still functions.
        // lightPropTarget is a LightNode ID (that is what the selection and the
        // WorldGraph hand out), so it must be resolved with GetLight() - the
        // previous vector-index lookup addressed the wrong node whenever the
        // ids and positions diverged, e.g. after any deletion.
        if (g_editorPanels.actionApplyLight) {
            if (LightNode* ln = PawnSystem::Instance().GetLight(g_editorPanels.lightPropTarget)) {
                HistoryPush();
                ln->color.r = (unsigned char)ClampPropInt((int)g_editorPanels.lightColorR, 0, 255);
                ln->color.g = (unsigned char)ClampPropInt((int)g_editorPanels.lightColorG, 0, 255);
                ln->color.b = (unsigned char)ClampPropInt((int)g_editorPanels.lightColorB, 0, 255);
                ln->intensity = fmaxf(0.0f, g_editorPanels.lightIntensity);
                ln->radius = fmaxf(0.1f, g_editorPanels.lightRadius);
                ln->type = (LitLightType)g_editorPanels.lightType;
                ln->effect = (LitLightEffect)g_editorPanels.lightEffect;
                // Spot cone angles: edit fields are in degrees, LightNode stores cos(half-angle)
                ln->innerCone = cosf(fminf(fmaxf(g_editorPanels.lightInnerAngle, 0.5f), 89.0f) * DEG2RAD);
                ln->outerCone = cosf(fminf(fmaxf(g_editorPanels.lightOuterAngle, 1.0f), 89.0f) * DEG2RAD);
                ln->flare  = g_editorPanels.lightFlare;
                ln->corona = g_editorPanels.lightCorona;
                if (ln->type == LitLightType::DIRECTIONAL)
                    ln->target = {0.0f, 0.0f, 0.0f};
                PawnSystem::Instance().AssignLightZones();
                EditorLog("Applied legacy light properties to id=%d: color=(%.0f,%.0f,%.0f) "
                          "intensity=%.1f radius=%.0f type=%d effect=%d",
                    ln->id,
                    g_editorPanels.lightColorR, g_editorPanels.lightColorG, g_editorPanels.lightColorB,
                    g_editorPanels.lightIntensity, g_editorPanels.lightRadius,
                    g_editorPanels.lightType, g_editorPanels.lightEffect);
            }
            g_editorPanels.actionApplyLight = false;
        }

        if (!g_editorPanels.actionSpawnPawn.empty()) {
            Vector3 pos = OTEditor.MainCamera.target; // in front of the camera
            HistoryPush();
            PawnSystem::Instance().Spawn(pos, g_editorPanels.actionSpawnPawn.c_str());
            EditorLog("Spawned pawn '%s' at camera target",
                      g_editorPanels.actionSpawnPawn.c_str());
            g_editorPanels.actionSpawnPawn.clear();
        }

        // Portal editing actions (Portal tab in Zone Properties)
        if (g_editorPanels.actionApplyPortal >= 0) {
            int idx = g_editorPanels.actionApplyPortal;
            auto& portals = ZoneManager::Instance().GetPortals();
            if (idx >= 0 && idx < (int)portals.size()) {
                HistoryPush();
                PortalEditValues pe = GetPortalEditValues();
                ZonePortal& p = portals[idx];
                p.targetWorld = pe.targetWorld;
                p.targetSpawn = {pe.spawnX, pe.spawnY, pe.spawnZ};
                p.bidirectional = pe.bidirectional;
                g_sel.name = pe.targetWorld;
                EditorLog("Portal %d updated (target=%s spawn=%.1f,%.1f,%.1f bidir=%d)",
                          idx, pe.targetWorld.c_str(), pe.spawnX, pe.spawnY, pe.spawnZ,
                          pe.bidirectional ? 1 : 0);
                RefreshLevelList();
            }
            g_editorPanels.actionApplyPortal = -1;
        }
        if (g_editorPanels.actionDeletePortal >= 0) {
            int idx = g_editorPanels.actionDeletePortal;
            HistoryPush();
            ZoneManager::Instance().RemovePortal(idx);
            EditorLog("Portal %d deleted", idx);
            RefreshPortalList();
            RefreshLevelList();
            g_editorPanels.actionDeletePortal = -1;
        }
        if (g_editorPanels.actionSelectPortal >= 0) {
            int idx = g_editorPanels.actionSelectPortal;
            auto& portals = ZoneManager::Instance().GetPortals();
            if (idx >= 0 && idx < (int)portals.size()) {
                auto& p = portals[idx];
                g_sel = { SelType::PORTAL, idx, p.targetWorld, {
                    (p.bounds.min.x + p.bounds.max.x) * 0.5f,
                    (p.bounds.min.y + p.bounds.max.y) * 0.5f,
                    (p.bounds.min.z + p.bounds.max.z) * 0.5f }};
                SnapGizmoToSelection(g_sel);
                OmegaTechEditor.DrawModel = true;
            }
            g_editorPanels.actionSelectPortal = -1;
        }

        // LevelList / Campaign actions
        if (!g_editorPanels.actionLevelListOpen.empty()) {
            std::string worldName = g_editorPanels.actionLevelListOpen;
            g_editorPanels.actionLevelListOpen.clear();
            fs::path ozone = fs::path("GameData/Worlds") / worldName / "World.ozone";
            if (!fs::exists(ozone)) ozone = fs::path("../GameData/Worlds") / worldName / "World.ozone";
            fs::path target = ozone;
            if (fs::exists(target)) {
                g_pendingOpenPath = target;
                EditorLog("LevelList: opening world '%s'", worldName.c_str());
            } else {
                EditorLog("LevelList: world '%s' not found", worldName.c_str());
            }
        }
        if (!g_editorPanels.actionLevelListLink.empty()) {
            std::string target = g_editorPanels.actionLevelListLink;
            g_editorPanels.actionLevelListLink.clear();
            // Create a portal in front of the camera linking to the target world
            Vector3 pos = OTEditor.MainCamera.target;
            HistoryPush();
            ZonePortal portal;
            portal.bounds = {{pos.x - 2, pos.y - 2, pos.z - 2},
                             {pos.x + 2, pos.y + 2, pos.z + 2}};
            portal.targetWorld = target;
            portal.targetSpawn = {0, 20, 0};
            portal.bidirectional = true;
            ZoneManager::Instance().AddPortal(portal);
            g_editorPanels.portalTargetWorld = target;
            RefreshPortalList();
            RefreshLevelList();
            EditorLog("Created portal link to '%s' at camera target", target.c_str());
        }

        // Mode switching
        if (IsKeyPressed(KEY_ONE))   g_placeMode = PlaceMode::MODEL;
        if (IsKeyPressed(KEY_TWO))   g_placeMode = PlaceMode::PICKUP;
        if (IsKeyPressed(KEY_THREE)) g_placeMode = PlaceMode::NODE;
        if (IsKeyPressed(KEY_FOUR))  { g_placeMode = PlaceMode::ENV; ShowEnvPanel(!g_editorPanels.showEnvPanel); }
        if (IsKeyPressed(KEY_FIVE) && g_placeMode != PlaceMode::MODEL)
            g_placeMode = PlaceMode::TERRAIN;

        // Primitive type cycling (1-5 for box/cyl/sph/pyr/pln, only in MODEL mode)
        // Reads real state from EMID (200+prim); never re-fires the actionCsgPlace
        // one-shot (that would reset the ghost's size every frame).
        if (g_placeMode == PlaceMode::MODEL) {
            int prim = (EMID >= 200 && EMID <= 204) ? EMID - 200 : 0;
            if (IsKeyPressed(KEY_FIVE)) {
                prim = (prim + 1) % 5;
                EMID = 200 + prim;
                EditorLog("Primitive: %s", prim == 0 ? "box" : prim == 1 ? "cylinder" : prim == 2 ? "sphere" : prim == 3 ? "pyramid" : "plane");
            }
        }

        // CSG operation cycling (G key)
        if (IsKeyPressed(KEY_G) && g_placeMode == PlaceMode::MODEL) {
            OmegaTechEditor.CSGOperation = (OmegaTechEditor.CSGOperation + 1) % 5;
        }

        // Heightmap editor toggle (H key) â€” suppressed in vertex-edit mode (H moves verts)
        if (IsKeyPressed(KEY_H) && !g_editorPanels.animEditVerts) ToggleHeightmapEditor();

        // Panel keyboard shortcuts (F5-F12 replace old top menu bar)
        if (IsKeyPressed(KEY_F5))  ToggleModelBrowser();
        if (IsKeyPressed(KEY_F6))  ToggleSoundMgr();
        if (IsKeyPressed(KEY_F7))  ToggleTextureMgr();
        if (IsKeyPressed(KEY_F8))  TogglePawnMgr();
        if (IsKeyPressed(KEY_F9))  ToggleScriptMgr();
        if (IsKeyPressed(KEY_F10)) TogglePickupPanel();
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();  // supersedes old F11 handling
        if (IsKeyPressed(KEY_F12)) ToggleEnvPanel();

        // Selection shortcuts. Escape must also cancel an active placement ghost
        // (DrawModel true with nothing selected), otherwise there is no keyboard
        // way out of placement mode.
        if (IsKeyPressed(KEY_ESCAPE) &&
            (g_sel.type != SelType::NONE || OmegaTechEditor.DrawModel)) {
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
        // Undo / redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z). Deferred to the anim tool
        // while vertex-editing, which owns its own undo stack.
        if (!g_editorPanels.animEditVerts &&
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (IsKeyPressed(KEY_Z)) { if (shift) HistoryRedo(); else HistoryUndo(); }
            if (IsKeyPressed(KEY_Y)) HistoryRedo();
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
