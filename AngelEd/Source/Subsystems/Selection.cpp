// =============================================================================
// Subsystems/Selection.cpp
//
// Picking and selection: viewport raycast, surface-face pick, gizmo snap, hover, and adopting a selection.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

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
            // Portals have no list window any more; the Entity Properties panel
            // edits them, so picking one in the viewport opens it there.
            ShowPropertiesPanel(true);
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

// Adopt a bus event's captured selection as the live one.
//
// Returns false when the event named nothing selectable, so a caller can skip the
// destructive action rather than applying it to whatever happens to be selected —
// the exact failure the loose `action*` fields made possible, where
// actionWorldGraphDelete stored an entity index that the handler then ignored.
//
// SelKind mirrors SelType one-for-one (see ToBusKind in SelType.hpp), so the cast
// is safe; the switch there is what guarantees they cannot drift.
static bool AdoptSelection(const ed::Selection& pick) {
    if (!pick.valid()) return false;
    g_sel.type = (SelType)pick.ref.kind;
    g_sel.index = pick.ref.index;
    g_sel.name = pick.name;
    g_sel.pos = {pick.x, pick.y, pick.z};
    EditorLog("Selected from WorldGraph: %s (type=%d idx=%d)",
              g_sel.name.c_str(), (int)g_sel.type, g_sel.index);
    // SelType::MAP is the level, not a placed object: it has no transform, so
    // arming the gizmo would put a draggable handle at {0,0,0} that the Move tool
    // then writes into g_sel.pos every frame. Only an instance selection gets one.
    if (g_sel.type != SelType::MAP) {
        OmegaTechEditor.DrawModel = true;
        SnapGizmoToSelection(g_sel);
    } else {
        OmegaTechEditor.DrawModel = false;
    }
    return true;
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