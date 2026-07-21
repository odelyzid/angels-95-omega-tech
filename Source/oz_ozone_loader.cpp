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
// World texture loading (from worlddir/oztex/tileset/)
// ---------------------------------------------------------------------------
void OzoneLoader::LoadWorldTextures(const std::string& worldDir) {
    UnloadTextures();
    auto load = [&](const char* name) -> Texture2D {
        char path[512];
        snprintf(path, sizeof(path), "%soztex/tileset/%s", worldDir.c_str(), name);
        if (IsPathFile(path))
            return LoadTexture(path);
        return Texture2D{0};
    };
    m_floorTex  = load("concrete_floor_a1_32x32.png");
    m_wallTex   = load("concrete_ceiling_a1_32x32.png");
    m_columnTex = load("industrial_metal_a1_32x32.png");
    m_ceilTex   = load("industrial_tech_a1_128x128.png");
    if (m_floorTex.id || m_wallTex.id || m_columnTex.id || m_ceilTex.id)
        fprintf(stderr, "OzoneLoader: loaded world textures from %soztex/tileset/\n", worldDir.c_str());
}

void OzoneLoader::UnloadTextures() {
    if (m_floorTex.id)  { UnloadTexture(m_floorTex);  m_floorTex  = Texture2D{0}; }
    if (m_wallTex.id)   { UnloadTexture(m_wallTex);   m_wallTex   = Texture2D{0}; }
    if (m_columnTex.id) { UnloadTexture(m_columnTex); m_columnTex = Texture2D{0}; }
    if (m_ceilTex.id)   { UnloadTexture(m_ceilTex);   m_ceilTex   = Texture2D{0}; }
}

// ---------------------------------------------------------------------------
// Apply a texture to a model's diffuse map (or flat color fallback)
// ---------------------------------------------------------------------------
static void ApplyTex(Model& model, Texture2D tex, Color fallback) {
    if (tex.id)
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    else
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = fallback;
}

// ---------------------------------------------------------------------------
// Build* — each applies the best texture for its role
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildBox(float w, float h, float d) {
    Mesh mesh = GenMeshCube(w, h, d);
    Model model = LoadModelFromMesh(mesh);
    // Thin boxes (< 1.0 tall) are treated as floors, tall as walls
    if (h < 1.0f)
        ApplyTex(model, m_floorTex, LIGHTGRAY);
    else
        ApplyTex(model, m_wallTex, LIGHTGRAY);
    return model;
}

Model OzoneLoader::BuildCylinder(float rTop, float rBot, float h, int slices) {
    float r = (rTop > rBot) ? rTop : rBot;
    Mesh mesh = GenMeshCylinder(r, h, slices);
    Model model = LoadModelFromMesh(mesh);
    ApplyTex(model, m_columnTex, SKYBLUE);
    return model;
}

Model OzoneLoader::BuildSphere(float r, int segments) {
    Mesh mesh = GenMeshSphere(r, segments, segments);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = PURPLE;
    return model;
}

Model OzoneLoader::BuildPyramid(float w, float d, float h) {
    int triCount = 6;
    int vertCount = triCount * 3;
    Mesh mesh = {0};
    mesh.triangleCount = triCount;
    mesh.vertexCount = vertCount;

    mesh.vertices = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.normals  = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertCount * 2 * sizeof(float));

    float hw = w / 2.0f, hd = d / 2.0f;
    float ax = 0, ay = h, az = 0;
    float b[4][3] = {
        {-hw, 0, -hd},
        { hw, 0, -hd},
        { hw, 0,  hd},
        {-hw, 0,  hd}
    };
    int faces[6][3] = {
        {0,1,4}, {1,2,4}, {2,3,4}, {3,0,4},
        {3,2,1}, {1,0,3}
    };
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

Model OzoneLoader::BuildPlane(float nx, float ny, float nz, float dist) {
    Mesh mesh = GenMeshPlane(10.0f, 10.0f, 1, 1);
    Model model = LoadModelFromMesh(mesh);
    ApplyTex(model, m_floorTex, DARKGRAY);
    return model;
}

// ---------------------------------------------------------------------------
// BuildFromPrimitive
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildFromPrimitive(int type, const std::vector<float>& args) {
    switch ((OzonePrimitiveType)type) {
        case OzonePrimitiveType::BOX: {
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float h = (args.size() > 4) ? args[4] : 2.0f;
            float d = (args.size() > 5) ? args[5] : 2.0f;
            return BuildBox(w, h, d);
        }
        case OzonePrimitiveType::CYLINDER: {
            float rTop = (args.size() > 3) ? args[3] : 1.0f;
            float rBot = (args.size() > 4) ? args[4] : 1.0f;
            float h    = (args.size() > 5) ? args[5] : 2.0f;
            int slices = (args.size() > 6) ? (int)args[6] : 16;
            return BuildCylinder(rTop, rBot, h, slices);
        }
        case OzonePrimitiveType::SPHERE: {
            float r   = (args.size() > 3) ? args[3] : 1.0f;
            int segs  = (args.size() > 4) ? (int)args[4] : 16;
            return BuildSphere(r, segs);
        }
        case OzonePrimitiveType::PYRAMID: {
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float d = (args.size() > 4) ? args[4] : 2.0f;
            float h = (args.size() > 5) ? args[5] : 2.0f;
            return BuildPyramid(w, d, h);
        }
        case OzonePrimitiveType::PLANE: {
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
// LoadFile — also loads world textures from the .ozone file's directory
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadFile(const char* path) {
    // Extract world directory from the .ozone path and load textures
    std::string p(path);
    size_t slash = p.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::string worldDir = p.substr(0, slash + 1);
        LoadWorldTextures(worldDir);
    }

    auto primitives = OzoneParser::parse_file(path);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;

        if (prim.args.size() >= 3)
            r.position = {prim.args[0], prim.args[2], prim.args[1]};

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
    UnloadTextures();
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------
OzoneRenderable* OzoneLoader::Get(int index) {
    if (index < 0 || index >= (int)m_renderables.size()) return nullptr;
    return &m_renderables[index];
}
