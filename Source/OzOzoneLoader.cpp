#include "OzOzoneLoader.hpp"
#include "Package/OzAssetMapper.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include "Server/OzoneParser.hpp"
#include "Log.hpp"
#include "Package/PackageAssetLoader.hpp"
#include "Physics/OzBsp.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

static ZoneType ParseZoneType(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (name == "ladder") return ZoneType::ZONE_LADDER;
    if (name == "sky") return ZoneType::ZONE_SKY;
    if (name == "reverb") return ZoneType::ZONE_REVERB;
    if (name == "gameplay_sound") return ZoneType::ZONE_GAMEPLAY_SOUND;
    return ZoneType::ZONE_WATER;
}

static bool LoadOzoneEntity(const OzonePrimitive& prim) {
    auto& pawns = PawnSystem::Instance();
    switch (prim.type) {
        case OzonePrimitiveType::ENTITY_PLAYERSTART:
            if (prim.args.size() >= 3) {
                pawns.AddPlayerStart({0, {prim.args[0], prim.args[2], prim.args[1]},
                                      prim.args.size() >= 4 ? prim.args[3] : 0.0f});
            }
            return true;
        case OzonePrimitiveType::ENTITY_PICKUP:
            if (prim.args.size() >= 3) {
                PickupNode node;
                node.position = {prim.args[0], prim.args[2], prim.args[1]};
                node.typeName = prim.entityType;
                if (prim.args.size() >= 4) node.respawnTime = prim.args[3];
                pawns.AddPickup(node);
            }
            return true;
        case OzonePrimitiveType::ENTITY_ZONE:
            if (prim.args.size() >= 6) {
                ZoneVolumeNode node;
                node.bounds.min = {
                    std::min(prim.args[0], prim.args[3]),
                    std::min(prim.args[2], prim.args[5]),
                    std::min(prim.args[1], prim.args[4])
                };
                node.bounds.max = {
                    std::max(prim.args[0], prim.args[3]),
                    std::max(prim.args[2], prim.args[5]),
                    std::max(prim.args[1], prim.args[4])
                };
                node.zoneType = ParseZoneType(prim.entitySubType);
                if (prim.args.size() >= 7) node.intensity = prim.args[6];
                pawns.AddZone(node);
            }
            return true;
        case OzonePrimitiveType::ENTITY_NPC:
            if (prim.args.size() >= 3)
                pawns.Spawn({prim.args[0], prim.args[2], prim.args[1]}, prim.entityType.c_str());
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
OzoneLoader& OzoneLoader::Instance() {
    static OzoneLoader instance;
    return instance;
}

Shader OzoneLoader::s_litFogShader = {0};

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
        OZ_INFO("OzoneLoader: loaded world textures from %soztex/tileset/", worldDir.c_str());
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
    if (OzoneLoader::GetLitFogShader().id > 0)
        model.materials[0].shader = OzoneLoader::GetLitFogShader();
}

// ---------------------------------------------------------------------------
// Build* â€” each applies the best texture for its role
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
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();
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
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();
    return model;
}

Model OzoneLoader::BuildPlane(float nx, float ny, float nz, float dist) {
    Mesh mesh = GenMeshPlane(10.0f, 10.0f, 1, 1);
    Model model = LoadModelFromMesh(mesh);
    ApplyTex(model, m_floorTex, DARKGRAY);
    return model;
}

// ---------------------------------------------------------------------------
// BuildHeightmap â€” load grayscale PNG, generate terrain mesh
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildHeightmap(const std::string& imagePath,
                                  const std::string& texPath,
                                  const std::vector<float>& args) {
    // args: x y z scale sizeX sizeY sizeZ
    if (args.size() < 6) return Model{0};

    // Load grayscale heightmap image
    m_hmImage = LoadImage(imagePath.c_str());
    if (m_hmImage.data == 0) return Model{0};
    ImageFormat(&m_hmImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);

    // Load texture overlay
    m_hmTexture = LoadTexture(texPath.c_str());

    // Position (Z-up â†’ Y-up swap: args[1]=OZONE Y becomes raylib Z)
    m_hmPosition = {args[0], args[2], args[1]};
    m_hmScale = (args.size() > 3) ? args[3] : 1.0f;
    m_hmSize = {(args.size() > 4) ? args[4] : 100.0f,
                (args.size() > 5) ? args[5] : 50.0f,
                (args.size() > 6) ? args[6] : 100.0f};

    Mesh mesh = GenMeshHeightmap(m_hmImage, m_hmSize);
    Model model = LoadModelFromMesh(mesh);
    if (m_hmTexture.id)
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = m_hmTexture;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();

    m_hmReady = (m_hmImage.data != 0);
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
// LoadFile â€” also loads world textures from the .ozone file's directory
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadFile(const char* path) {
    OZ_INFO("OzoneLoader: loading %s", path);
    Unload();
    auto& pawns = PawnSystem::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    pawns.ClearZones();

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
        if (LoadOzoneEntity(prim)) continue;

        // Heightmap is handled specially â€” builds its own model from image path
        if (prim.type == OzonePrimitiveType::HEIGHTMAP) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        r.position = {0,0,0};
        r.scale = 1.0f;
        r.model = BuildHeightmap(prim.entityType, prim.entitySubType, prim.args);
        r.loaded = m_hmReady;
        r.csgOp = 0;
        m_renderables.push_back(r);
        continue;
    }

    OzoneRenderable r;
    r.typeId = (int)prim.type;
    r.csgOp = prim.csgOp;

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

    RebuildCollisionVolumes();
    OZ_INFO("OzoneLoader: loaded %zu primitives, %zu collision volumes from %s",
            primitives.size(), m_collisionVolumes.size(), path);
    return true;
}

// ---------------------------------------------------------------------------
// LoadString
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadString(const char* data) {
    Unload();
    auto& pawns = PawnSystem::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    pawns.ClearZones();

    auto primitives = OzoneParser::parse_string(data);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
        if (LoadOzoneEntity(prim)) continue;

        // Heightmap is handled specially
        if (prim.type == OzonePrimitiveType::HEIGHTMAP) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        r.position = {0,0,0};
        r.scale = 1.0f;
        r.model = BuildHeightmap(prim.entityType, prim.entitySubType, prim.args);
        r.loaded = m_hmReady;
        r.csgOp = 0;
        m_renderables.push_back(r);
        continue;
    }

    OzoneRenderable r;
    r.typeId = (int)prim.type;
    r.csgOp = prim.csgOp;
    if (prim.args.size() >= 3)
        r.position = {prim.args[0], prim.args[2], prim.args[1]};
    r.scale = 1.0f;
    r.model = BuildFromPrimitive((int)prim.type, prim.args);
    r.loaded = (r.model.meshCount > 0);
    m_renderables.push_back(r);
    }
    RebuildCollisionVolumes();
    return true;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void OzoneLoader::Draw(Camera3D& camera) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
            // Heightmap uses its own position/scale stored from the primitive
            DrawModelEx(m_hmModel, m_hmPosition, (Vector3){0,1,0}, 0,
                        (Vector3){m_hmScale, m_hmScale, m_hmScale}, WHITE);
        } else {
            DrawModel(r.model, r.position, r.scale, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// ComputeCollisionAABB â€” generate world-space AABB from primitive params
// ---------------------------------------------------------------------------
void OzoneLoader::ComputeCollisionAABB(int type, const std::vector<float>& args,
                                       Vector3 position, BoundingBox& out) {
    out = {{0,0,0},{0,0,0}};
    switch ((OzonePrimitiveType)type) {
        case OzonePrimitiveType::BOX: {
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float h = (args.size() > 4) ? args[4] : 2.0f;
            float d = (args.size() > 5) ? args[5] : 2.0f;
            out.min = {position.x - w/2, position.y,       position.z - d/2};
            out.max = {position.x + w/2, position.y + h,   position.z + d/2};
            break;
        }
        case OzonePrimitiveType::CYLINDER: {
            float rTop = (args.size() > 3) ? args[3] : 1.0f;
            float rBot = (args.size() > 4) ? args[4] : 1.0f;
            float h    = (args.size() > 5) ? args[5] : 2.0f;
            float maxR = (rTop > rBot) ? rTop : rBot;
            out.min = {position.x - maxR, position.y,       position.z - maxR};
            out.max = {position.x + maxR, position.y + h,   position.z + maxR};
            break;
        }
        case OzonePrimitiveType::SPHERE: {
            float r = (args.size() > 3) ? args[3] : 1.0f;
            out.min = {position.x - r, position.y - r, position.z - r};
            out.max = {position.x + r, position.y + r, position.z + r};
            break;
        }
        case OzonePrimitiveType::PYRAMID: {
            float w = (args.size() > 3) ? args[3] : 2.0f;
            float d = (args.size() > 4) ? args[4] : 2.0f;
            float h = (args.size() > 5) ? args[5] : 2.0f;
            out.min = {position.x - w/2, position.y,       position.z - d/2};
            out.max = {position.x + w/2, position.y + h,   position.z + d/2};
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// RebuildCollisionVolumes â€” iterate renderables and generate AABBs
// ---------------------------------------------------------------------------
void OzoneLoader::RebuildCollisionVolumes() {
    m_collisionVolumes.clear();

    // Phase 1: collect all brush AABBs with their CSG operations
    CsgProcessor csg;
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        // Skip entity types â€” handled by PawnSystem
        if (r.typeId == (int)OzonePrimitiveType::ENTITY_PLAYERSTART ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_PICKUP    ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_ZONE      ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_NPC       ||
            r.typeId == (int)OzonePrimitiveType::HEIGHTMAP)
            continue;

        BoundingBox mb = GetMeshBoundingBox(r.model.meshes[0]);
        CsgBrush brush;
        brush.minX = r.position.x + mb.min.x * r.scale;
        brush.minY = r.position.y + mb.min.y * r.scale;
        brush.minZ = r.position.z + mb.min.z * r.scale;
        brush.maxX = r.position.x + mb.max.x * r.scale;
        brush.maxY = r.position.y + mb.max.y * r.scale;
        brush.maxZ = r.position.z + mb.max.z * r.scale;
        brush.op   = (CsgOp)r.csgOp;
        csg.Apply(brush);
    }

    // Overflow protection: merge adjacent coplanar AABBs
    int merges = csg.MergePass();
    if (merges > 0) {
        OZ_INFO("CSG: merged %d adjacent volumes (count: %d â†’ %d)",
                merges, csg.Count() + merges, csg.Count());
    }

    // Phase 2: convert CSG-processed volumes to collision volumes
    std::vector<CsgProcessor::Volume> vols;
    csg.GetVolumes(vols);
    m_collisionVolumes.reserve(vols.size());
    for (auto& v : vols) {
        OzoneCollisionVolume cv;
        cv.aabb.min = {v.minX, v.minY, v.minZ};
        cv.aabb.max = {v.maxX, v.maxY, v.maxZ};
        cv.typeId = 0;
        m_collisionVolumes.push_back(cv);
    }

    // Phase 3: rebuild spatial partition from processed volumes
    std::vector<WorldChunkManager::Volume> wcVols;
    wcVols.reserve(vols.size());
    for (auto& v : vols)
        wcVols.push_back({v.minX, v.minY, v.minZ, v.maxX, v.maxY, v.maxZ});
    m_chunkManager.Build(wcVols);
}

// ---------------------------------------------------------------------------
// DrawZoneGeometry â€” draw renderables whose AABBs overlap the given zone
// ---------------------------------------------------------------------------
void OzoneLoader::DrawZoneGeometry(Camera3D& camera, const BoundingBox& zoneBounds) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (r.typeId == (int)OzonePrimitiveType::ENTITY_PLAYERSTART ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_PICKUP    ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_ZONE      ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_NPC       ||
            r.typeId == (int)OzonePrimitiveType::HEIGHTMAP)
            continue;

        // Compute world-space AABB for this renderable
        BoundingBox mb = GetMeshBoundingBox(r.model.meshes[0]);
        BoundingBox worldBounds;
        worldBounds.min = {r.position.x + mb.min.x * r.scale,
                           r.position.y + mb.min.y * r.scale,
                           r.position.z + mb.min.z * r.scale};
        worldBounds.max = {r.position.x + mb.max.x * r.scale,
                           r.position.y + mb.max.y * r.scale,
                           r.position.z + mb.max.z * r.scale};

        // Check overlap with sky zone
        if (CheckCollisionBoxes(worldBounds, zoneBounds)) {
            if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady)
                DrawModelEx(m_hmModel, m_hmPosition, (Vector3){0,1,0}, 0,
                            (Vector3){m_hmScale, m_hmScale, m_hmScale}, WHITE);
            else
                DrawModel(r.model, r.position, r.scale, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// UnloadHeightmap
// ---------------------------------------------------------------------------
void OzoneLoader::UnloadHeightmap() {
    if (m_hmReady) {
        UnloadModel(m_hmModel);
        if (m_hmImage.data) UnloadImage(m_hmImage);
        if (m_hmTexture.id) UnloadTexture(m_hmTexture);
        m_hmReady = false;
        m_hmImage = Image{0};
        m_hmTexture = Texture2D{0};
        m_hmModel = Model{0};
    }
}

// ---------------------------------------------------------------------------
// SampleHeightmapY â€” bilinear sample the OZONE-loaded heightmap
// Returns -99999.0f if no heightmap loaded or out of bounds.
// ---------------------------------------------------------------------------
float OzoneLoader::SampleHeightmapY(float px, float pz) const {
    if (!m_hmReady || m_hmImage.data == 0) return -99999.0f;

    Vector3 o = m_hmPosition;
    float scale = m_hmScale;
    float sx = m_hmSize.x * scale;
    float sz = m_hmSize.z * scale;
    int iw = m_hmImage.width;
    int ih = m_hmImage.height;
    if (iw < 1 || ih < 1) return -99999.0f;

    float hx = (px - o.x) / sx;
    float hz = (pz - o.z) / sz;
    float fx = hx * (float)(iw - 1);
    float fz = hz * (float)(ih - 1);
    int ix = (int)fx;
    int iz = (int)fz;
    if (ix < 0 || ix >= iw - 1 || iz < 0 || iz >= ih - 1)
        return o.y;

    float tx = fx - ix;
    float tz = fz - iz;
    uint8_t* p = (uint8_t*)m_hmImage.data;
    float h00 = p[iz * iw + ix] / 255.0f;
    float h10 = p[iz * iw + ix + 1] / 255.0f;
    float h01 = p[(iz + 1) * iw + ix] / 255.0f;
    float h11 = p[(iz + 1) * iw + ix + 1] / 255.0f;
    float ht = h00 * (1 - tx) * (1 - tz)
             + h10 * tx * (1 - tz)
             + h01 * (1 - tx) * tz
             + h11 * tx * tz;
    return o.y + ht * m_hmSize.y * scale;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void OzoneLoader::Unload() {
    for (auto& r : m_renderables) {
        if (r.loaded) UnloadModel(r.model);
    }
    m_renderables.clear();
    m_collisionVolumes.clear();
    UnloadHeightmap();
    UnloadTextures();
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------
OzoneRenderable* OzoneLoader::Get(int index) {
    if (index < 0 || index >= (int)m_renderables.size()) return nullptr;
    return &m_renderables[index];
}

