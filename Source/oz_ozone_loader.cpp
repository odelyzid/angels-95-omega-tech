#include "oz_ozone_loader.h"
#include "oz_assetmapper.h"
#include "oz_pawn_system.h"
#include "Server/OzoneParser.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
OzoneLoader& OzoneLoader::Instance() {
    static OzoneLoader instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Generate a colored cube mesh as a Model
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildBox(float w, float h, float d) {
    Mesh mesh = GenMeshCube(w, h, d);
    Model model = LoadModelFromMesh(mesh);
    // Apply a basic gray material
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = LIGHTGRAY;
    return model;
}

// ---------------------------------------------------------------------------
// Cylinder
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildCylinder(float rTop, float rBot, float h, int slices) {
    // raylib 5.5 GenMeshCylinder takes (radius, height, slices) — use larger radius
    float r = (rTop > rBot) ? rTop : rBot;
    Mesh mesh = GenMeshCylinder(r, h, slices);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = SKYBLUE;
    return model;
}

// ---------------------------------------------------------------------------
// Sphere
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildSphere(float r, int segments) {
    Mesh mesh = GenMeshSphere(r, segments, segments);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = PURPLE;
    return model;
}

// ---------------------------------------------------------------------------
// Pyramid — built from custom mesh (GenMeshCube + scale, or manual tris)
// We'll build a simple 4-sided pyramid via GenMeshHemiSphere or manual.
// Actually raylib has no built-in pyramid, so we build one.
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildPyramid(float w, float d, float h) {
    // Manual pyramid mesh: 4 side triangles + 1 base quad = 5 faces
    // 4 triangles per side (2 per tri), 2 for base = 6 triangles = 18 verts
    int triCount = 6;
    int vertCount = triCount * 3;
    Mesh mesh = {0};
    mesh.triangleCount = triCount;
    mesh.vertexCount = vertCount;

    mesh.vertices = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.normals  = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertCount * 2 * sizeof(float));

    float hw = w / 2.0f, hd = d / 2.0f;
    // Apex
    float ax = 0, ay = h, az = 0;
    // Base corners
    float b[4][3] = {
        {-hw, 0, -hd},
        { hw, 0, -hd},
        { hw, 0,  hd},
        {-hw, 0,  hd}
    };
    // Face indices (triangle pairs): side0, side1, side2, side3, base0, base1
    int faces[6][3] = {
        {0,1,4}, // side 0-1-apex
        {1,2,4}, // side 1-2-apex
        {2,3,4}, // side 2-3-apex
        {3,0,4}, // side 3-0-apex
        {3,2,1}, // base lower
        {1,0,3}  // base upper
    };
    // Vertices for indexing: 0-3 = base, 4 = apex
    float verts[5][3] = {
        {b[0][0], b[0][1], b[0][2]},
        {b[1][0], b[1][1], b[1][2]},
        {b[2][0], b[2][1], b[2][2]},
        {b[3][0], b[3][1], b[3][2]},
        {ax, ay, az}
    };
    int vi = 0;
    for (int f = 0; f < triCount; f++) {
        for (int j = 0; j < 3; j++) {
            int idx = faces[f][j];
            mesh.vertices[vi * 3 + 0] = verts[idx][0];
            mesh.vertices[vi * 3 + 1] = verts[idx][1];
            mesh.vertices[vi * 3 + 2] = verts[idx][2];
            // Simple face normal (not per-vertex accurate)
            mesh.normals[vi * 3 + 0] = 0;
            mesh.normals[vi * 3 + 1] = (f < 4) ? 0.5f : -1.0f;
            mesh.normals[vi * 3 + 2] = 0;
            mesh.texcoords[vi * 2 + 0] = (j == 0) ? 0 : (j == 1) ? 1 : 0.5f;
            mesh.texcoords[vi * 2 + 1] = (j == 2) ? 1 : 0;
            vi++;
        }
    }

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = GOLD;
    return model;
}

// ---------------------------------------------------------------------------
// Plane — generate a flat quad
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildPlane(float nx, float ny, float nz, float dist) {
    // Ignore normal/dist — just produce a flat 10x10 quad
    Mesh mesh = GenMeshPlane(10.0f, 10.0f, 1, 1);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = DARKGRAY;
    return model;
}

// ---------------------------------------------------------------------------
// BuildFromPrimitive — dispatch to type-specific builder
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildFromPrimitive(int type, const std::vector<float>& args) {
    switch ((OzonePrimitiveType)type) {
        case OzonePrimitiveType::BOX: {
            // box x y z w h d rot — args = [x,y,z,w,h,d,rot]
            // We use w, h, d from args[3..5]
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float h = (args.size() > 4) ? args[4] : 2.0f;
            float d = (args.size() > 5) ? args[5] : 2.0f;
            return BuildBox(w, h, d);
        }
        case OzonePrimitiveType::CYLINDER: {
            // cyl x y z rTop rBot h slices rot — args = [x,y,z,rTop,rBot,h,slices,rot]
            float rTop = (args.size() > 3) ? args[3] : 1.0f;
            float rBot = (args.size() > 4) ? args[4] : 1.0f;
            float h    = (args.size() > 5) ? args[5] : 2.0f;
            int slices = (args.size() > 6) ? (int)args[6] : 16;
            return BuildCylinder(rTop, rBot, h, slices);
        }
        case OzonePrimitiveType::SPHERE: {
            // sph x y z r [segments] — args = [x,y,z,r] or [x,y,z,r,segments]
            float r   = (args.size() > 3) ? args[3] : 1.0f;
            int segs  = (args.size() > 4) ? (int)args[4] : 16;
            return BuildSphere(r, segs);
        }
        case OzonePrimitiveType::PYRAMID: {
            // pyr x y z w d h — args = [x,y,z,w,d,h]
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float d = (args.size() > 4) ? args[4] : 2.0f;
            float h = (args.size() > 5) ? args[5] : 2.0f;
            return BuildPyramid(w, d, h);
        }
        case OzonePrimitiveType::PLANE: {
            // pln x y z nx ny nz dist — args = [x,y,z,nx,ny,nz,dist]
            float nx   = (args.size() > 3) ? args[3] : 0.0f;
            float ny   = (args.size() > 4) ? args[4] : 1.0f;
            float nz   = (args.size() > 5) ? args[5] : 0.0f;
            float dist = (args.size() > 6) ? args[6] : 0.0f;
            return BuildPlane(nx, ny, nz, dist);
        }
        default:
            return BuildBox(1, 1, 1);
    }
}

// ---------------------------------------------------------------------------
// LoadFile
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadFile(const char* path) {
    auto primitives = OzoneParser::parse_file(path);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;

        // Position is always args[0..2]
        if (prim.args.size() >= 3) {
            // OZONE is Z-up, convert to Y-up: (x, z, y)
            r.position = {prim.args[0], prim.args[2], prim.args[1]};
        }

        // Rotation from type-specific index
        r.scale = 1.0f;
        if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 7)
            r.rotation = prim.args[6] * DEG2RAD;
        else if (prim.type == OzonePrimitiveType::CYLINDER && prim.args.size() >= 8)
            r.rotation = prim.args[7] * DEG2RAD;

        r.model = BuildFromPrimitive((int)prim.type, prim.args);
        r.loaded = (r.model.meshCount > 0);
        m_renderables.push_back(r);
    }

    fprintf(stderr, "OzoneLoader: loaded %zu primitives from %s\n", primitives.size(), path);
    return !m_renderables.empty();
}

// ---------------------------------------------------------------------------
// LoadString
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadString(const char* data) {
    auto primitives = OzoneParser::parse_string(data);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        if (prim.args.size() >= 3)
            r.position = {prim.args[0], prim.args[2], prim.args[1]};
        r.scale = 1.0f;
        r.model = BuildFromPrimitive((int)prim.type, prim.args);
        r.loaded = (r.model.meshCount > 0);
        m_renderables.push_back(r);
    }
    return !m_renderables.empty();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void OzoneLoader::Draw(Camera3D& camera) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        DrawModel(r.model, r.position, r.scale, WHITE);
    }
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void OzoneLoader::Unload() {
    for (auto& r : m_renderables) {
        if (r.loaded) UnloadModel(r.model);
    }
    m_renderables.clear();
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------
OzoneRenderable* OzoneLoader::Get(int index) {
    if (index < 0 || index >= (int)m_renderables.size()) return nullptr;
    return &m_renderables[index];
}
