#include "OzOzoneLoader.hpp"
#include "Package/OzAssetMapper.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include "Script/LightningEntityRegistry.hpp"
#include "Server/OzoneParser.hpp"
#include "Log.hpp"
#include "Package/PackageAssetLoader.hpp"
#include "Physics/OzBsp.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
namespace fs = std::filesystem;

// Strip surrounding quote characters (U+0022) from a string if present
static std::string StripQuotes(std::string s) {
    if (s.size() >= 2 && s.front() == '\"' && s.back() == '\"')
        return s.substr(1, s.size() - 2);
    return s;
}

static ZoneType ParseZoneType(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (name == "ladder") return ZoneType::ZONE_LADDER;
    if (name == "sky") return ZoneType::ZONE_SKY;
    if (name == "reverb") return ZoneType::ZONE_REVERB;
    if (name == "gameplay_sound") return ZoneType::ZONE_GAMEPLAY_SOUND;
    return ZoneType::ZONE_WATER;
}

static bool LoadOzoneEntity(const OzonePrimitive& prim,
                            std::unordered_map<std::string, int>& zoneCounters,
                            const std::string& worldDir) {
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
                // Generate unique zone name for .ozls script hook matching.
                // An explicit name= kwarg overrides the auto-generated one so
                // scripted zones survive zone reordering in the editor.
                auto& counter = zoneCounters[prim.entitySubType];
                node.name = prim.name.empty()
                    ? ("zone_" + prim.entitySubType + "_" + std::to_string(counter++))
                    : prim.name;
                // Extended env override args (optional, after intensity):
                // fogR fogG fogB fogDensity fogStart fogEnd ambR ambG ambB ambIntensity reverbMix reverbDecay
                if (prim.args.size() >= 17) {
                    node.envOverrides.applyFog = true;
                    node.envOverrides.fogR = (int)prim.args[7];
                    node.envOverrides.fogG = (int)prim.args[8];
                    node.envOverrides.fogB = (int)prim.args[9];
                    node.envOverrides.fogDensity = prim.args[10];
                    node.envOverrides.fogStart = prim.args[11];
                    node.envOverrides.fogEnd = prim.args[12];
                    node.envOverrides.applyAmbient = true;
                    node.envOverrides.ambR = (int)prim.args[13];
                    node.envOverrides.ambG = (int)prim.args[14];
                    node.envOverrides.ambB = (int)prim.args[15];
                    node.envOverrides.ambIntensity = prim.args[16];
                    if (prim.args.size() >= 18) {
                        node.envOverrides.reverbMix = prim.args[17];
                        node.envOverrides.reverbDecay = prim.args[18];
                    }
                }
                pawns.AddZone(node);

                // For sky zones, also register a SkyZoneNode
                if (ParseZoneType(prim.entitySubType) == ZoneType::ZONE_SKY) {
                    SkyZoneNode skyNode;
                    skyNode.bounds = node.bounds;
                    skyNode.position = {
                        (node.bounds.min.x + node.bounds.max.x) * 0.5f,
                        (node.bounds.min.y + node.bounds.max.y) * 0.5f,
                        (node.bounds.min.z + node.bounds.max.z) * 0.5f
                    };
                    skyNode.name = node.name;
                    skyNode.intensity = node.intensity;
                    // Look up .ozls SKYZONE entity by name for initial config.
                    // The global registry holds defs from ALL worlds, and zone
                    // names are generated per world (e.g. "zone_sky_0") — so a
                    // name hit may belong to another world. Disambiguate by
                    // matching the def's skybox path against this world dir.
                    const EntityDef* edef = LightningEntityRegistry::Instance().Find(node.name);
                    if (edef && edef->type == EntityType::SKYZONE) {
                        if (!worldDir.empty() && edef->skybox.find(worldDir) == std::string::npos)
                            edef = nullptr; // name collided with another world's def
                    } else {
                        edef = nullptr;
                    }
                    if (!edef) {
                        std::vector<const EntityDef*> skyDefs;
                        LightningEntityRegistry::Instance().FindByType(EntityType::SKYZONE, skyDefs);
                        for (auto* d : skyDefs) {
                            if (!worldDir.empty() &&
                                d->skybox.find(worldDir) != std::string::npos) {
                                edef = d;
                                break;
                            }
                        }
                    }
                    if (edef && edef->type == EntityType::SKYZONE) {
                        skyNode.def = edef;
                        skyNode.skyboxPath = edef->skybox;
                        OZ_INFO("OZONE sky zone '%s': resolved def '%s' skybox='%s'",
                                node.name.c_str(), edef->name.c_str(), edef->skybox.c_str());
                        // Look up fov from stats if available
                        auto fit = edef->stats.floats.find("fov");
                        if (fit != edef->stats.floats.end())
                            skyNode.fov = fit->second;
                        auto sit = edef->stats.vec3s.find("scroll_speed");
                        if (sit != edef->stats.vec3s.end()) {
                            skyNode.scrollSpeed = {sit->second[0], sit->second[1], sit->second[2]};
                        }
                    }
                    pawns.AddSkyZone(skyNode);
                }
            }
            return true;
        case OzonePrimitiveType::ENTITY_NPC:
            if (prim.args.size() >= 3)
                pawns.Spawn({prim.args[0], prim.args[2], prim.args[1]}, prim.entityType.c_str());
            return true;
        case OzonePrimitiveType::ENTITY_LIGHT: {
            LightNode node;
            node.active = true;
            std::string subtype = prim.entityType;
            auto arg = [&](int i) -> float {
                return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : 0.0f;
            };
            if (subtype == "point" && prim.args.size() >= 7) {
                // light point x y z r g b intensity radius [effect]
                node.type = LitLightType::POINT;
                node.position = {arg(0), arg(2), arg(1)}; // Z-up conversion
                node.color = (Color){(unsigned char)arg(3), (unsigned char)arg(4), (unsigned char)arg(5), 255};
                node.intensity = arg(6);
                node.radius = arg(7);
                if (prim.args.size() >= 9) node.effect = (LitLightEffect)(int)arg(8);
            } else if (subtype == "spot" && prim.args.size() >= 12) {
                // light spot x y z tx ty tz r g b intensity radius innerCone outerCone [effect]
                node.type = LitLightType::SPOT;
                node.position = {arg(0), arg(2), arg(1)};
                node.target = {arg(3), arg(5), arg(4)};
                node.color = (Color){(unsigned char)arg(6), (unsigned char)arg(7), (unsigned char)arg(8), 255};
                node.intensity = arg(9);
                node.radius = arg(10);
                node.innerCone = arg(11);
                node.outerCone = arg(12);
                if (prim.args.size() >= 14) node.effect = (LitLightEffect)(int)arg(13);
            } else if (subtype == "directional" && prim.args.size() >= 6) {
                // light directional tx ty tz r g b intensity
                node.type = LitLightType::DIRECTIONAL;
                node.target = {arg(0), arg(2), arg(1)};
                node.color = (Color){(unsigned char)arg(3), (unsigned char)arg(4), (unsigned char)arg(5), 255};
                node.intensity = arg(6);
            } else {
                OZ_WARN("OZONE: invalid light definition (subtype=%s args=%zu)", subtype.c_str(), prim.args.size());
                return true;
            }
            pawns.AddLight(node);
            return true;
        }
        case OzonePrimitiveType::ENTITY_PORTAL: {
            // portal targetWorld minX minY minZ maxX maxY maxZ [spawnX spawnY spawnZ] [bidir]
            // Coordinates are OZONE Z-up — convert to engine Y-up.
            if (prim.args.size() >= 6) {
                ZonePortal portal;
                portal.targetWorld = prim.entityType;
                portal.bounds.min = {
                    std::min(prim.args[0], prim.args[3]),
                    std::min(prim.args[2], prim.args[5]),
                    std::min(prim.args[1], prim.args[4])
                };
                portal.bounds.max = {
                    std::max(prim.args[0], prim.args[3]),
                    std::max(prim.args[2], prim.args[5]),
                    std::max(prim.args[1], prim.args[4])
                };
                if (prim.args.size() >= 9) {
                    portal.targetSpawn = {prim.args[6], prim.args[8], prim.args[7]}; // Z-up conversion
                } else {
                    portal.targetSpawn = {
                        (portal.bounds.min.x + portal.bounds.max.x) * 0.5f,
                        portal.bounds.min.y,
                        (portal.bounds.min.z + portal.bounds.max.z) * 0.5f
                    };
                }
                if (prim.args.size() >= 10) portal.bidirectional = prim.args[9] != 0.0f;
                pawns.AddPortal(portal);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_LEVELINFO: {
            // levelinfo gameType maxPlayers respawnTime timeLimitEnabled timeLimitMinutes
            //           scoreLimit friendlyFire skyboxPath
            auto arg = [&](int i) -> float {
                return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : 0.0f;
            };
            LevelSettings& s = pawns.GetWorldInfo().settings;
            s.gameType = (int)arg(0);
            s.maxPlayers = (int)arg(1);
            s.respawnTime = arg(2);
            s.timeLimitEnabled = arg(3) != 0.0f;
            s.timeLimitMinutes = arg(4);
            s.scoreLimit = (int)arg(5);
            s.friendlyFire = arg(6) != 0.0f;
            s.skyboxPath = prim.entityType;
            s.skyboxSidePath = prim.entitySubType;
            return true;
        }
        case OzonePrimitiveType::ENTITY_PARTICLES: {
            // particles type density speed r g b windX windZ
            auto arg = [&](int i) -> float {
                return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : 0.0f;
            };
            LevelSettings& s = pawns.GetWorldInfo().settings;
            s.particleType = (int)arg(0);
            s.particleDensity = arg(1);
            s.particleSpeed = arg(2);
            s.particleR = (int)arg(3);
            s.particleG = (int)arg(4);
            s.particleB = (int)arg(5);
            s.particleWindX = arg(6);
            s.particleWindZ = arg(7);
            return true;
        }
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
Shader OzoneLoader::s_backupLitFogShader = {0};

void OzoneLoader::SetLitFogShader(Shader shader) {
    s_litFogShader = shader;
    for (auto& r : m_renderables) {
        if (r.loaded && r.model.meshCount > 0 && shader.id > 0)
            r.model.materials[0].shader = shader;
    }
    if (m_hmReady && m_hmModel.meshCount > 0 && shader.id > 0)
        m_hmModel.materials[0].shader = shader;
}

void OzoneLoader::SetLitFogShaderEnabled(bool enabled) {
    if (enabled) {
        s_litFogShader = s_backupLitFogShader;
        // Re-apply restored shader to all existing renderables
        Shader sh = s_litFogShader;
        for (auto& r : m_renderables) {
            if (r.loaded && r.model.meshCount > 0 && sh.id > 0)
                r.model.materials[0].shader = sh;
        }
        if (m_hmReady && m_hmModel.meshCount > 0 && sh.id > 0)
            m_hmModel.materials[0].shader = sh;
    } else {
        if (s_litFogShader.id > 0)
            s_backupLitFogShader = s_litFogShader;
        for (auto& r : m_renderables) {
            if (r.loaded && r.model.meshCount > 0)
                r.model.materials[0].shader = Shader{0};
        }
        if (m_hmReady && m_hmModel.meshCount > 0)
            m_hmModel.materials[0].shader = Shader{0};
        s_litFogShader = Shader{0};
    }
}

// ---------------------------------------------------------------------------
// World texture loading — loads ALL .png from oztex/tileset/ into vector
// Textures are indexed 1..N by their sorted filename order.
// ---------------------------------------------------------------------------
void OzoneLoader::LoadWorldTextures(const std::string& worldDir) {
    UnloadTextures();
    std::string tilesetDir = worldDir + "oztex/tileset/";
    if (!fs::exists(tilesetDir)) return;

    std::vector<std::string> texFiles;
    for (auto& entry : fs::directory_iterator(tilesetDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png")
            texFiles.push_back(entry.path().filename().string());
    }
    if (texFiles.empty()) return;

    std::sort(texFiles.begin(), texFiles.end());
    for (auto& f : texFiles) {
        Texture2D tex = LoadTexture((tilesetDir + f).c_str());
        if (tex.id) {
            m_tilesetTex.push_back(tex);
            OZ_INFO("OzoneLoader: tex[%zu] = %s", m_tilesetTex.size(), f.c_str());
        }
    }
    OZ_INFO("OzoneLoader: loaded %zu textures from %s", m_tilesetTex.size(), tilesetDir.c_str());
}

void OzoneLoader::UnloadTextures() {
    for (auto& tex : m_tilesetTex)
        if (tex.id) UnloadTexture(tex);
    m_tilesetTex.clear();
}

// ---------------------------------------------------------------------------
// Re-apply texture on a model based on texSlot (1-based index into tileset)
// ---------------------------------------------------------------------------
void OzoneLoader::ApplyTexSlotToModel(Model& model, int slot) {
    if (model.meshCount == 0) return;
    Texture2D tex{0};
    Color fallback = LIGHTGRAY;
    if (slot >= 1 && slot <= (int)m_tilesetTex.size()) {
        tex = m_tilesetTex[slot - 1];
    }
    if (tex.id)
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    else
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = fallback;
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();
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
    // Walls (h >= 1.0) get 16x UV tiling so the 32x32 tileset texture
    // doesn't look stretched across large faces
    // Floors (h < 1.0) get 8x tiling so ground tiles repeat sensibly
    float tiling = (h >= 1.0f) ? 16.0f : 8.0f;
    if (mesh.texcoords) {
        for (int i = 0; i < mesh.vertexCount; i++) {
            mesh.texcoords[i*2 + 0] *= tiling;
            mesh.texcoords[i*2 + 1] *= tiling;
        }
        UpdateMeshBuffer(mesh, 1, mesh.texcoords,
                         mesh.vertexCount * 2 * (int)sizeof(float), 0);
    }
    Model model = LoadModelFromMesh(mesh);
    // Auto-select: slot 1 (floor) if thin, slot 2 (wall) if tall
    Texture2D tex = (m_tilesetTex.size() >= 1 && h < 1.0f) ? m_tilesetTex[0] :
                    (m_tilesetTex.size() >= 2) ? m_tilesetTex[1] : Texture2D{0};
    Color fallback = (h < 1.0f) ? LIGHTGRAY : (Color){180,180,200,255};
    ApplyTex(model, tex, fallback);
    return model;
}

Model OzoneLoader::BuildCylinder(float rTop, float rBot, float h, int slices) {
    float r = (rTop > rBot) ? rTop : rBot;
    Mesh mesh = GenMeshCylinder(r, h, slices);
    Model model = LoadModelFromMesh(mesh);
    Texture2D tex = (m_tilesetTex.size() >= 3) ? m_tilesetTex[2] : Texture2D{0};
    ApplyTex(model, tex, SKYBLUE);
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
    Texture2D tex = (m_tilesetTex.size() >= 1) ? m_tilesetTex[0] : Texture2D{0};
    ApplyTex(model, tex, DARKGRAY);
    return model;
}

// ---------------------------------------------------------------------------
// BuildHeightmapMesh — build a tessellated grid mesh from a height array
// Vertices are placed with cellX/cellZ spacing, height = h * heightScale.
// Mesh is centered at origin. Normals computed from adjacent triangle faces.
// ---------------------------------------------------------------------------
Model OzoneLoader::BuildHeightmapMesh(const std::vector<float>& heights,
                                      int gw, int gh,
                                      float cellX, float cellZ,
                                      float heightScale, float uvTileSize) {
    if (gw < 2 || gh < 2 || heights.size() < (size_t)(gw * gh))
        return Model{0};
    if (uvTileSize <= 0.0f) uvTileSize = 8.0f;

    int quadsW = gw - 1;
    int quadsH = gh - 1;
    int triCount = quadsW * quadsH * 2;
    int vertCount = triCount * 3;

    Mesh mesh = {0};
    mesh.triangleCount = triCount;
    mesh.vertexCount = vertCount;

    mesh.vertices  = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.normals   = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(vertCount * 2 * sizeof(float));

    float halfW = cellX * quadsW * 0.5f;
    float halfD = cellZ * quadsH * 0.5f;

    auto vertexPos = [&](int col, int row) -> Vector3 {
        return {
            col * cellX - halfW,
            heights[row * gw + col] * heightScale,
            row * cellZ - halfD
        };
    };

    // Compute normal for a triangle given three vertices
    auto triNormal = [](Vector3 a, Vector3 b, Vector3 c) -> Vector3 {
        Vector3 ab = {b.x - a.x, b.y - a.y, b.z - a.z};
        Vector3 ac = {c.x - a.x, c.y - a.y, c.z - a.z};
        Vector3 n = Vector3CrossProduct(ab, ac);
        return Vector3Normalize(n);
    };

    int vi = 0;
    for (int iz = 0; iz < quadsH; iz++) {
        for (int ix = 0; ix < quadsW; ix++) {
            Vector3 v00 = vertexPos(ix,     iz);
            Vector3 v10 = vertexPos(ix + 1, iz);
            Vector3 v11 = vertexPos(ix + 1, iz + 1);
            Vector3 v01 = vertexPos(ix,     iz + 1);

            // Tri 1: v00, v11, v10 — CCW seen from above so the face
            // normal points up (lit shader needs upward normals)
            Vector3 n1 = triNormal(v00, v11, v10);
            // Tri 2: v00, v01, v11
            Vector3 n2 = triNormal(v00, v01, v11);

            // World-space tiled UVs (uvTileSize world units per repeat)
            float u0 = ((float) ix * cellX)      / uvTileSize;
            float u1 = ((float)(ix + 1) * cellX) / uvTileSize;
            float v0 = ((float) iz * cellZ)      / uvTileSize;
            float v1 = ((float)(iz + 1) * cellZ) / uvTileSize;

            // Triangle 1: v00, v11, v10
            mesh.vertices[vi * 3 + 0] = v00.x;
            mesh.vertices[vi * 3 + 1] = v00.y;
            mesh.vertices[vi * 3 + 2] = v00.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            mesh.vertices[vi * 3 + 0] = v11.x;
            mesh.vertices[vi * 3 + 1] = v11.y;
            mesh.vertices[vi * 3 + 2] = v11.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;

            mesh.vertices[vi * 3 + 0] = v10.x;
            mesh.vertices[vi * 3 + 1] = v10.y;
            mesh.vertices[vi * 3 + 2] = v10.z;
            mesh.normals[vi * 3 + 0] = n1.x;
            mesh.normals[vi * 3 + 1] = n1.y;
            mesh.normals[vi * 3 + 2] = n1.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            // Triangle 2: v00, v01, v11
            mesh.vertices[vi * 3 + 0] = v00.x;
            mesh.vertices[vi * 3 + 1] = v00.y;
            mesh.vertices[vi * 3 + 2] = v00.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v0;
            vi++;

            mesh.vertices[vi * 3 + 0] = v01.x;
            mesh.vertices[vi * 3 + 1] = v01.y;
            mesh.vertices[vi * 3 + 2] = v01.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u0;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;

            mesh.vertices[vi * 3 + 0] = v11.x;
            mesh.vertices[vi * 3 + 1] = v11.y;
            mesh.vertices[vi * 3 + 2] = v11.z;
            mesh.normals[vi * 3 + 0] = n2.x;
            mesh.normals[vi * 3 + 1] = n2.y;
            mesh.normals[vi * 3 + 2] = n2.z;
            mesh.texcoords[vi * 2 + 0] = u1;
            mesh.texcoords[vi * 2 + 1] = v1;
            vi++;
        }
    }

    // raylib 5.5's LoadModelFromMesh does NOT upload the mesh to the GPU
    // (unlike GenMesh*/LoadOBJ) — upload explicitly or nothing will draw
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    return model;
}

// ---------------------------------------------------------------------------
// BuildHeightmap — load grayscale PNG, generate terrain mesh
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
    if (m_hmImage.data == 0) {
        OZ_WARN("OZONE heightmap: LoadImage failed for '%s'", imagePath.c_str());
        return Model{0};
    }
    OZ_INFO("OZONE heightmap: image loaded %dx%d from '%s'", m_hmImage.width, m_hmImage.height, imagePath.c_str());
    ImageFormat(&m_hmImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);

    // Load texture overlay
    m_hmTexture = LoadTexture(texPath.c_str());
    if (!m_hmTexture.id) {
        OZ_WARN("OZONE heightmap: texture load failed for '%s'", texPath.c_str());
    } else {
        // Tiled UVs need repeat wrapping; mipmaps + trilinear reduce shimmer
        SetTextureWrap(m_hmTexture, TEXTURE_WRAP_REPEAT);
        GenTextureMipmaps(&m_hmTexture);
        SetTextureFilter(m_hmTexture, TEXTURE_FILTER_TRILINEAR);
    }

    // Position (Z-up â†’ Y-up swap: args[1]=OZONE Y becomes raylib Z)
    m_hmPosition = {args[0], args[2], args[1]};
    m_hmScale = (args.size() > 3) ? args[3] : 1.0f;
    m_hmSize = {(args.size() > 4) ? args[4] : 100.0f,
                (args.size() > 5) ? args[5] : 50.0f,
                (args.size() > 6) ? args[6] : 100.0f};

    // Read heights from grayscale image into m_hmHeights
    m_hmGridW = m_hmImage.width;
    m_hmGridH = m_hmImage.height;
    uint8_t* pixels = (uint8_t*)m_hmImage.data;
    m_hmHeights.resize(m_hmGridW * m_hmGridH);
    for (int i = 0; i < m_hmGridW * m_hmGridH; i++)
        m_hmHeights[i] = pixels[i] / 255.0f;

    float cellX = (m_hmGridW > 1) ? (m_hmSize.x / (float)(m_hmGridW - 1)) : 0;
    float cellZ = (m_hmGridH > 1) ? (m_hmSize.z / (float)(m_hmGridH - 1)) : 0;
    Model model = BuildHeightmapMesh(m_hmHeights, m_hmGridW, m_hmGridH,
                                     cellX, cellZ, m_hmSize.y);
    if (m_hmTexture.id)
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = m_hmTexture;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    if (GetLitFogShader().id > 0)
        model.materials[0].shader = GetLitFogShader();

    // Keep the model for rendering (Draw/DrawWorldGeometry draw m_hmModel)
    if (m_hmModel.meshCount > 0)
        UnloadModel(m_hmModel);
    m_hmModel = model;

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

    // Extract world directory from the .ozone path and load textures
    std::string p(path);
    std::string worldDir;
    std::string gameDataWorldDir;
    size_t slash = p.find_last_of("/\\");
    if (slash != std::string::npos) {
        worldDir = p.substr(0, slash + 1);
        // Extract world name for GameData path resolution (e.g. "world_EngineTest.ozone" → "EngineTest")
        std::string fname = p.substr(slash + 1);
        std::string prefix = "world_";
        std::string suffix = ".ozone";
        if (fname.rfind(prefix, 0) == 0 && fname.size() > prefix.size() + suffix.size()) {
            std::string worldName = fname.substr(prefix.size(), fname.size() - prefix.size() - suffix.size());
            gameDataWorldDir = std::string("GameData/Worlds/") + worldName + "/";
            // Prefer GameData tileset over package directory
            if (fs::exists(gameDataWorldDir + "oztex/tileset/")) {
                LoadWorldTextures(gameDataWorldDir);
            } else {
                LoadWorldTextures(worldDir);
            }
        } else {
            LoadWorldTextures(worldDir);
        }
    }
    // Remember the world directory for .ozls def matching (GameData path when
    // available so world-local skyzone defs win over same-named ones elsewhere)
    m_worldDir = gameDataWorldDir.empty() ? worldDir : gameDataWorldDir;

    auto primitives = OzoneParser::parse_file(path);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
        if (LoadOzoneEntity(prim, m_zoneCounters, m_worldDir)) continue;

        // Heightmap is handled specially — builds its own model from image path
        if (prim.type == OzonePrimitiveType::HEIGHTMAP) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        r.position = {0,0,0};
        r.scale = 1.0f;
        // Resolve relative paths: prefer GameData/Worlds/<name>/, fall back to package directory
        std::string imgPath = StripQuotes(prim.entityType);
        std::string texPath = StripQuotes(prim.entitySubType);
        if (!gameDataWorldDir.empty()) {
            imgPath = gameDataWorldDir + imgPath;
            if (!texPath.empty()) texPath = gameDataWorldDir + texPath;
        } else if (!worldDir.empty()) {
            imgPath = worldDir + imgPath;
            if (!texPath.empty()) texPath = worldDir + texPath;
        }
        r.model = BuildHeightmap(imgPath, texPath, prim.args);
        r.loaded = m_hmReady;
        r.csgOp = 0;
        m_renderables.push_back(r);
        continue;
    }

    OzoneRenderable r;
    r.typeId = (int)prim.type;
    r.csgOp = prim.csgOp;
    r.surfaceFlags = prim.surfaceFlags;

    if (prim.args.size() >= 3)
        r.position = {prim.args[0], prim.args[2], prim.args[1]};

    // Center cylinder and pyramid at position (mesh sits with bottom at Y=0, but
    // box/sphere/plane are centered; adjust to match the center convention)
    if ((prim.type == OzonePrimitiveType::CYLINDER ||
         prim.type == OzonePrimitiveType::PYRAMID) && prim.args.size() >= 6)
        r.position.y -= prim.args[5] / 2.0f;

    r.scale = 1.0f;
    if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 7)
        r.rotation = prim.args[6] * DEG2RAD;
    else if (prim.type == OzonePrimitiveType::CYLINDER && prim.args.size() >= 8)
        r.rotation = prim.args[7] * DEG2RAD;

    r.model = BuildFromPrimitive((int)prim.type, prim.args);
    r.loaded = (r.model.meshCount > 0);
    // Sync texScaleU/V with BuildBox's default tiling so that
    // ApplyRenderableUV correctly undoes it before applying overrides
    if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 5) {
        float h = prim.args[4];
        r.texScaleU = (h >= 1.0f) ? 16.0f : 8.0f;
        r.texScaleV = (h >= 1.0f) ? 16.0f : 8.0f;
    }
    // Parse optional texSlot after rotation: box has 7+1=8 args, cyl has 8+1=9
    if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 8)
        r.texSlot = (int)prim.args[7];
    else if (prim.type == OzonePrimitiveType::CYLINDER && prim.args.size() >= 9)
        r.texSlot = (int)prim.args[8];
    else if (prim.type == OzonePrimitiveType::PYRAMID && prim.args.size() >= 7)
        r.texSlot = (int)prim.args[6];
    // Apply texture override if set
    if (r.texSlot > 0) ApplyTexSlotToModel(r.model, r.texSlot);
    m_renderables.push_back(r);
    // Apply optional texture UV scaling/offset
    if (prim.texScaleU != 1.0f || prim.texScaleV != 1.0f ||
        prim.texOffsetU != 0.0f || prim.texOffsetV != 0.0f) {
        ApplyRenderableUV((int)m_renderables.size() - 1,
                          prim.texScaleU, prim.texScaleV,
                          prim.texOffsetU, prim.texOffsetV);
    }
    // Custom per-brush diffuse texture (texPath= attribute), resolved against
    // the world directory unless already rooted at GameData/
    if (!prim.texPath.empty()) {
        std::string tp = StripQuotes(prim.texPath);
        if (!tp.empty() && tp.rfind("GameData/", 0) != 0) {
            if (!gameDataWorldDir.empty()) tp = gameDataWorldDir + tp;
            else if (!worldDir.empty()) tp = worldDir + tp;
        }
        ApplyRenderableTexture((int)m_renderables.size() - 1, tp.c_str());
    }
    }

    // Post-process: assign zoneId to lights based on containing zone
    {
        auto& lights = PawnSystem::Instance().GetLights();
        auto& zones = PawnSystem::Instance().GetZones();
        for (auto& l : lights) {
            l.zoneId = -1; // default: affects all zones
            for (auto& z : zones) {
                if (l.position.x >= z.bounds.min.x && l.position.x <= z.bounds.max.x &&
                    l.position.y >= z.bounds.min.y && l.position.y <= z.bounds.max.y &&
                    l.position.z >= z.bounds.min.z && l.position.z <= z.bounds.max.z) {
                    l.zoneId = (int)z.id;
                    break; // first containing zone wins
                }
            }
        }
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

    auto primitives = OzoneParser::parse_string(data);
    if (primitives.empty()) return false;

    for (auto& prim : primitives) {
if (LoadOzoneEntity(prim, m_zoneCounters, m_worldDir)) continue;

        // Heightmap is handled specially
        if (prim.type == OzonePrimitiveType::HEIGHTMAP) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        r.position = {0,0,0};
        r.scale = 1.0f;
        r.model = BuildHeightmap(StripQuotes(prim.entityType),
                                 StripQuotes(prim.entitySubType), prim.args);
        r.loaded = m_hmReady;
        r.csgOp = 0;
        m_renderables.push_back(r);
        continue;
    }

    OzoneRenderable r;
    r.typeId = (int)prim.type;
    r.csgOp = prim.csgOp;
    r.surfaceFlags = prim.surfaceFlags;
    if (prim.args.size() >= 3)
        r.position = {prim.args[0], prim.args[2], prim.args[1]};

    // Center cylinder and pyramid at position (mesh sits with bottom at Y=0)
    if ((prim.type == OzonePrimitiveType::CYLINDER ||
         prim.type == OzonePrimitiveType::PYRAMID) && prim.args.size() >= 6)
        r.position.y -= prim.args[5] / 2.0f;

    r.scale = 1.0f;
    r.model = BuildFromPrimitive((int)prim.type, prim.args);
    r.loaded = (r.model.meshCount > 0);
    // Sync texScaleU/V with BuildBox's default tiling
    if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 5) {
        float h = prim.args[4];
        r.texScaleU = (h >= 1.0f) ? 16.0f : 8.0f;
        r.texScaleV = (h >= 1.0f) ? 16.0f : 8.0f;
    }
    if (prim.type == OzonePrimitiveType::BOX && prim.args.size() >= 8)
        r.texSlot = (int)prim.args[7];
    else if (prim.type == OzonePrimitiveType::CYLINDER && prim.args.size() >= 9)
        r.texSlot = (int)prim.args[8];
    else if (prim.type == OzonePrimitiveType::PYRAMID && prim.args.size() >= 7)
        r.texSlot = (int)prim.args[6];
    if (r.texSlot > 0) ApplyTexSlotToModel(r.model, r.texSlot);
    m_renderables.push_back(r);
    if (prim.texScaleU != 1.0f || prim.texScaleV != 1.0f ||
        prim.texOffsetU != 0.0f || prim.texOffsetV != 0.0f) {
        ApplyRenderableUV((int)m_renderables.size() - 1,
                          prim.texScaleU, prim.texScaleV,
                          prim.texOffsetU, prim.texOffsetV);
    }
    if (!prim.texPath.empty()) {
        std::string tp = StripQuotes(prim.texPath);
        if (!tp.empty() && tp.rfind("GameData/", 0) != 0 && !m_worldDir.empty())
            tp = m_worldDir + tp;
        ApplyRenderableTexture((int)m_renderables.size() - 1, tp.c_str());
    }
    }
    // Post-process: assign zoneId to lights based on containing zone
    {
        auto& lights = PawnSystem::Instance().GetLights();
        auto& zones = PawnSystem::Instance().GetZones();
        for (auto& l : lights) {
            l.zoneId = -1;
            for (auto& z : zones) {
                if (l.position.x >= z.bounds.min.x && l.position.x <= z.bounds.max.x &&
                    l.position.y >= z.bounds.min.y && l.position.y <= z.bounds.max.y &&
                    l.position.z >= z.bounds.min.z && l.position.z <= z.bounds.max.z) {
                    l.zoneId = (int)z.id;
                    break;
                }
            }
        }
    }
    RebuildCollisionVolumes();
    return true;
}

// ---------------------------------------------------------------------------
// Draw — all renderables (backward compat, used by editor)
// ---------------------------------------------------------------------------
void OzoneLoader::Draw(Camera3D& camera) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
            DrawModelEx(m_hmModel, m_hmPosition, (Vector3){0,1,0}, 0,
                        (Vector3){m_hmScale, m_hmScale, m_hmScale}, WHITE);
        } else {
            DrawModel(r.model, r.position, r.scale, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// DrawWorldGeometry — skip SURF_FAKEBACKDROP flagged brushes
// ---------------------------------------------------------------------------
void OzoneLoader::DrawWorldGeometry(Camera3D& camera) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (r.surfaceFlags & SURF_FAKEBACKDROP) continue;
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
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
            out.min = {position.x - w/2, position.y - h/2, position.z - d/2};
            out.max = {position.x + w/2, position.y + h/2, position.z + d/2};
            break;
        }
        case OzonePrimitiveType::CYLINDER: {
            float rTop = (args.size() > 3) ? args[3] : 1.0f;
            float rBot = (args.size() > 4) ? args[4] : 1.0f;
            float h    = (args.size() > 5) ? args[5] : 2.0f;
            float maxR = (rTop > rBot) ? rTop : rBot;
            out.min = {position.x - maxR, position.y - h/2, position.z - maxR};
            out.max = {position.x + maxR, position.y + h/2, position.z + maxR};
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
            out.min = {position.x - w/2, position.y - h/2, position.z - d/2};
            out.max = {position.x + w/2, position.y + h/2, position.z + d/2};
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
            r.typeId == (int)OzonePrimitiveType::ENTITY_LIGHT)
            continue;

        // Heightmap: emit an AABB covering the full terrain extent.
        // Per-cell collision is handled by SampleHeightmapY ground clamp.
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
            CsgBrush brush;
            brush.minX = m_hmPosition.x - m_hmSize.x * m_hmScale * 0.5f;
            brush.minY = m_hmPosition.y;
            brush.minZ = m_hmPosition.z - m_hmSize.z * m_hmScale * 0.5f;
            brush.maxX = m_hmPosition.x + m_hmSize.x * m_hmScale * 0.5f;
            brush.maxY = m_hmPosition.y + m_hmSize.y * m_hmScale;
            brush.maxZ = m_hmPosition.z + m_hmSize.z * m_hmScale * 0.5f;
            brush.op   = CsgOp::SOLID;
            csg.Apply(brush);
            continue;
        }

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
        if (m_hmReady) {
            float hmMinX = m_hmPosition.x - m_hmSize.x * m_hmScale * 0.5f;
            float hmMinZ = m_hmPosition.z - m_hmSize.z * m_hmScale * 0.5f;
            float hmMaxX = m_hmPosition.x + m_hmSize.x * m_hmScale * 0.5f;
            float hmMaxY = m_hmPosition.y + m_hmSize.y * m_hmScale;
            float hmMaxZ = m_hmPosition.z + m_hmSize.z * m_hmScale * 0.5f;
            const float eps = 0.01f;
            if (fabsf(v.minX - hmMinX) < eps && fabsf(v.minY - m_hmPosition.y) < eps &&
                fabsf(v.minZ - hmMinZ) < eps && fabsf(v.maxX - hmMaxX) < eps &&
                fabsf(v.maxY - hmMaxY) < eps && fabsf(v.maxZ - hmMaxZ) < eps)
                cv.isHeightmap = true;
        }
        m_collisionVolumes.push_back(cv);
    }

    // Phase 2b: copy texture settings from renderables to collision volumes
    // when the counts match (no CSG merging occurred).
    {
        int rCount = 0;
        for (auto& r : m_renderables) {
            if (!r.loaded) continue;
            if (r.typeId == (int)OzonePrimitiveType::ENTITY_PLAYERSTART ||
                r.typeId == (int)OzonePrimitiveType::ENTITY_PICKUP    ||
                r.typeId == (int)OzonePrimitiveType::ENTITY_ZONE      ||
                r.typeId == (int)OzonePrimitiveType::ENTITY_NPC       ||
                r.typeId == (int)OzonePrimitiveType::ENTITY_LIGHT     ||
                r.typeId == (int)OzonePrimitiveType::HEIGHTMAP)
                continue;
            if (rCount < (int)m_collisionVolumes.size()) {
                m_collisionVolumes[rCount].texScaleU = r.texScaleU;
                m_collisionVolumes[rCount].texScaleV = r.texScaleV;
                m_collisionVolumes[rCount].texOffsetU = r.texOffsetU;
                m_collisionVolumes[rCount].texOffsetV = r.texOffsetV;
                m_collisionVolumes[rCount].texSlot = r.texSlot;
                m_collisionVolumes[rCount].texPath = r.texPath;
            }
            rCount++;
        }
    }

    // Phase 3: rebuild spatial partition from processed volumes
    std::vector<WorldChunkManager::Volume> wcVols;
    wcVols.reserve(vols.size());
    for (auto& v : vols)
        wcVols.push_back({v.minX, v.minY, v.minZ, v.maxX, v.maxY, v.maxZ});
    m_chunkManager.Build(wcVols);
}

// ---------------------------------------------------------------------------
// AddBrushRenderable — editor helper to make a new brush visible
// ---------------------------------------------------------------------------
int OzoneLoader::AddBrushRenderable(int primType, const Vector3& pos,
                                    const Vector3& size, float rot,
                                    float scale, int csgOp) {
    Model mdl = {0};
    switch (primType) {
        case 0: mdl = BuildBox(size.x, size.y, size.z); break;
        case 1: mdl = BuildCylinder(size.x, size.y, size.z, 16); break;
        case 2: mdl = BuildSphere(size.x, 16); break;
        case 3: mdl = BuildPyramid(size.x, size.z, size.y); break;
        case 4: mdl = BuildPlane(0, 1, 0, 0); break;
        case 5: {
            // Flat platform heightmap (no file paths needed for editor preview)
            int gw = 9, gh = 9;
            std::vector<float> h(gw * gh, 0.5f);
            float cx = size.x / (float)(gw - 1);
            float cz = size.z / (float)(gh - 1);
            mdl = BuildHeightmapMesh(h, gw, gh, cx, cz, size.y);
            if (mdl.meshCount > 0)
                ApplyTex(mdl, Texture2D{0}, DARKGRAY);
            break;
        }
        default: return -1;
    }
    if (mdl.meshes == nullptr) return -1;

    OzoneRenderable r;
    r.typeId = primType;
    r.position = pos;
    // Center cylinder/pyramid at position (mesh sits with bottom at Y=0)
    if (primType == 1) r.position.y -= size.z / 2.0f;  // cylinder: h = size.z
    if (primType == 3) r.position.y -= size.y / 2.0f;  // pyramid: h = size.y
    r.scale = scale;
    r.rotation = rot;
    r.model = mdl;
    r.loaded = true;
    r.csgOp = csgOp;
    m_renderables.push_back(r);
    return (int)m_renderables.size() - 1;
}

// ---------------------------------------------------------------------------
// ApplyRenderableUV -- modify texture UV tiling/offset on a renderable's mesh
// The transform is: new_u = old_u * su + ou, new_v = old_v * sv + ov
// This updates both the CPU-side texcoords and the GPU vertex buffer.
// Stores the params on the renderable so they can be re-applied after rebuild.
// ---------------------------------------------------------------------------
void OzoneLoader::ApplyRenderableUV(int idx, float su, float sv, float ou, float ov) {
    OzoneRenderable* r = Get(idx);
    if (!r || !r->loaded || r->model.meshCount == 0) return;
    Mesh& mesh = r->model.meshes[0];
    if (!mesh.texcoords) return;

    // Undo previous transform first so transforms don't compound
    float invSu = (r->texScaleU != 0.0f) ? 1.0f / r->texScaleU : 1.0f;
    float invSv = (r->texScaleV != 0.0f) ? 1.0f / r->texScaleV : 1.0f;
    for (int i = 0; i < mesh.vertexCount; i++) {
        float baseU = (mesh.texcoords[i*2 + 0] - r->texOffsetU) * invSu;
        float baseV = (mesh.texcoords[i*2 + 1] - r->texOffsetV) * invSv;
        mesh.texcoords[i*2 + 0] = baseU * su + ou;
        mesh.texcoords[i*2 + 1] = baseV * sv + ov;
    }

    // Store new params
    r->texScaleU = su;
    r->texScaleV = sv;
    r->texOffsetU = ou;
    r->texOffsetV = ov;

    // Upload updated UVs to GPU (attribute index 1 = texcoords)
    UpdateMeshBuffer(mesh, 1, mesh.texcoords,
                     mesh.vertexCount * 2 * (int)sizeof(float), 0);
}

// ---------------------------------------------------------------------------
// ApplyRenderableTexture -- load a custom texture from a file path and apply
// it to the renderable's model material. Replaces any tileset or previous
// custom texture. Stores the path on the renderable for persistence.
// Returns true if the texture was loaded and applied successfully.
// ---------------------------------------------------------------------------
bool OzoneLoader::ApplyRenderableTexture(int idx, const char* path) {
    OzoneRenderable* r = Get(idx);
    if (!r || !r->loaded || r->model.meshCount == 0) return false;

    Texture2D tex = LoadTextureWithFallback(path);
    if (tex.id == 0) return false;

    // Unload previous custom texture if any
    if (r->customTex.id > 0) UnloadTexture(r->customTex);

    r->customTex = tex;
    r->texPath = path ? path : "";
    r->texSlot = 0; // custom texture overrides tileset

    r->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    if (GetLitFogShader().id > 0)
        r->model.materials[0].shader = GetLitFogShader();

    OZ_INFO("Applied custom texture to renderable %d: %s", idx, path);
    return true;
}

// ---------------------------------------------------------------------------
// RemoveRenderable -- remove a brush renderable at the given index.
// Unloads the model and custom texture, then erases from the list.
// ---------------------------------------------------------------------------
void OzoneLoader::RemoveRenderable(int idx) {
    OzoneRenderable* r = Get(idx);
    if (!r) return;
    if (r->customTex.id > 0) UnloadTexture(r->customTex);
    if (r->loaded) UnloadModel(r->model);
    m_renderables.erase(m_renderables.begin() + idx);
}

// ---------------------------------------------------------------------------
// FindRenderableByCollisionVol -- given a collision volume index, find the
// best-matching renderable by comparing world-space AABB centers.
// Returns -1 if no match found (counts differ, merged volumes, etc.)
// ---------------------------------------------------------------------------
int OzoneLoader::FindRenderableByCollisionVol(int cvIdx) {
    if (cvIdx < 0 || cvIdx >= (int)m_collisionVolumes.size()) return -1;
    BoundingBox target = m_collisionVolumes[cvIdx].aabb;
    float targetCx = (target.min.x + target.max.x) * 0.5f;
    float targetCy = (target.min.y + target.max.y) * 0.5f;
    float targetCz = (target.min.z + target.max.z) * 0.5f;

    int bestIdx = -1;
    float bestDist = 1e9f;
    for (size_t i = 0; i < m_renderables.size(); i++) {
        auto& r = m_renderables[i];
        if (!r.loaded || r.model.meshCount == 0) continue;
        BoundingBox mb = GetMeshBoundingBox(r.model.meshes[0]);
        float cx = r.position.x + (mb.min.x + mb.max.x) * 0.5f * r.scale;
        float cy = r.position.y + (mb.min.y + mb.max.y) * 0.5f * r.scale;
        float cz = r.position.z + (mb.min.z + mb.max.z) * 0.5f * r.scale;
        float dx = cx - targetCx, dy = cy - targetCy, dz = cz - targetCz;
        float dist = dx*dx + dy*dy + dz*dz;
        if (dist < bestDist) { bestDist = dist; bestIdx = (int)i; }
    }
    // Only return match if close enough (within half the target's longest axis)
    float maxDim = fmaxf(target.max.x - target.min.x,
                         fmaxf(target.max.y - target.min.y, target.max.z - target.min.z));
    if (bestDist > maxDim * maxDim * 0.25f) return -1;
    return bestIdx;
}

// ---------------------------------------------------------------------------
// UpdateBrushRenderable -- regenerate a brush renderable's mesh from new
// position, size, and rotation. Preserves texture slot, custom texture,
// and UV transform. Called from the editor Properties panel when the user
// applies changes.
// ---------------------------------------------------------------------------
void OzoneLoader::UpdateBrushRenderable(int idx, const Vector3& pos, const Vector3& size, float rot) {
    OzoneRenderable* r = Get(idx);
    if (!r || !r->loaded) return;
    if (r->typeId < 0 || r->typeId > 4) return; // only primitive types 0..4

    // Save texture state to re-apply on the new mesh
    int oldTexSlot = r->texSlot;
    std::string oldTexPath = r->texPath;
    Texture2D oldCustomTex = r->customTex;
    float oldSu = r->texScaleU;
    float oldSv = r->texScaleV;
    float oldOu = r->texOffsetU;
    float oldOv = r->texOffsetV;

    // Build new model with the requested size
    Model newModel = {0};
    switch (r->typeId) {
        case 0: newModel = BuildBox(size.x, size.y, size.z); break;
        case 1: newModel = BuildCylinder(size.x, size.y, size.z, 16); break;
        case 2: newModel = BuildSphere(size.x, 16); break;
        case 3: newModel = BuildPyramid(size.x, size.z, size.y); break;
        case 4: newModel = BuildPlane(0, 1, 0, 0); break;
    }
    if (newModel.meshCount == 0) return;

    // Re-apply tileset texture (only if no custom texture overrides it)
    if (oldCustomTex.id > 0) {
        newModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = oldCustomTex;
        if (GetLitFogShader().id > 0)
            newModel.materials[0].shader = GetLitFogShader();
    } else if (oldTexSlot > 0) {
        ApplyTexSlotToModel(newModel, oldTexSlot);
    }

    // Re-apply UV transform on the fresh mesh
    if (newModel.meshes[0].texcoords) {
        Mesh& m = newModel.meshes[0];
        for (int i = 0; i < m.vertexCount; i++) {
            m.texcoords[i*2 + 0] = m.texcoords[i*2 + 0] * oldSu + oldOu;
            m.texcoords[i*2 + 1] = m.texcoords[i*2 + 1] * oldSv + oldOv;
        }
        UpdateMeshBuffer(m, 1, m.texcoords, m.vertexCount * 2 * (int)sizeof(float), 0);
    }

    // Replace old model
    UnloadModel(r->model);
    r->model = newModel;

    // Update transform
    r->position = pos;
    r->rotation = rot;

    // Re-apply the Y-center adjustment for cylinder/pyramid
    if (r->typeId == 1) r->position.y -= size.z / 2.0f;  // cylinder: h = size.z
    if (r->typeId == 3) r->position.y -= size.y / 2.0f;  // pyramid: h = size.y
}

// ---------------------------------------------------------------------------
// DrawZoneGeometry â€” draw renderables with SURF_FAKEBACKDROP flag set
// (with optional bounds filter for backward compat)
// ---------------------------------------------------------------------------
void OzoneLoader::DrawZoneGeometry(Camera3D& camera, const BoundingBox& zoneBounds) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (!(r.surfaceFlags & SURF_FAKEBACKDROP)) continue;
        // Optional bounds filter: skip if brush AABB doesn't overlap zone
        BoundingBox mb = GetMeshBoundingBox(r.model.meshes[0]);
        BoundingBox worldBounds;
        worldBounds.min = {r.position.x + mb.min.x * r.scale,
                           r.position.y + mb.min.y * r.scale,
                           r.position.z + mb.min.z * r.scale};
        worldBounds.max = {r.position.x + mb.max.x * r.scale,
                           r.position.y + mb.max.y * r.scale,
                           r.position.z + mb.max.z * r.scale};
        if (!CheckCollisionBoxes(worldBounds, zoneBounds)) continue;
        if (r.typeId == (int)OzonePrimitiveType::ENTITY_PLAYERSTART ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_PICKUP    ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_ZONE      ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_NPC       ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_LIGHT     ||
            r.typeId == (int)OzonePrimitiveType::HEIGHTMAP)
            continue;

        DrawModel(r.model, r.position, r.scale, WHITE);
    }
}

// ---------------------------------------------------------------------------
// DrawZoneGeometry â€” draw all SURF_FAKEBACKDROP brushes (no bounds filter)
// ---------------------------------------------------------------------------
void OzoneLoader::DrawZoneGeometry(Camera3D& camera) {
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (!(r.surfaceFlags & SURF_FAKEBACKDROP)) continue;
        if (r.typeId == (int)OzonePrimitiveType::ENTITY_PLAYERSTART ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_PICKUP    ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_ZONE      ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_NPC       ||
            r.typeId == (int)OzonePrimitiveType::ENTITY_LIGHT     ||
            r.typeId == (int)OzonePrimitiveType::HEIGHTMAP)
            continue;

        DrawModel(r.model, r.position, r.scale, WHITE);
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
        m_hmGridW = 0;
        m_hmGridH = 0;
        m_hmHeights.clear();
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
// RebuildHeightmapMesh — rebuild the GPU mesh from m_hmHeights without
// re-reading the source image. Called after SetHeightAtGrid() modifies
// individual cell heights. Preserves texture and shader.
// ---------------------------------------------------------------------------
void OzoneLoader::RebuildHeightmapMesh() {
    if (!m_hmReady || m_hmHeights.empty()) return;

    float cellX = (m_hmGridW > 1) ? (m_hmSize.x / (float)(m_hmGridW - 1)) : 0;
    float cellZ = (m_hmGridH > 1) ? (m_hmSize.z / (float)(m_hmGridH - 1)) : 0;
    Model newModel = BuildHeightmapMesh(m_hmHeights, m_hmGridW, m_hmGridH,
                                        cellX, cellZ, m_hmSize.y);

    // Save old texture and shader references
    Texture2D oldTex = m_hmModel.meshCount > 0
        ? m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture
        : Texture2D{0};
    Shader oldShader = m_hmModel.meshCount > 0
        ? m_hmModel.materials[0].shader
        : Shader{0};

    // Replace the model
    if (m_hmModel.meshCount > 0)
        UnloadModel(m_hmModel);

    m_hmModel = newModel;
    if (oldTex.id || m_hmTexture.id) {
        m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            oldTex.id ? oldTex : m_hmTexture;
    }
    m_hmModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    if (oldShader.id > 0)
        m_hmModel.materials[0].shader = oldShader;
    else if (GetLitFogShader().id > 0)
        m_hmModel.materials[0].shader = GetLitFogShader();
}

// ---------------------------------------------------------------------------
// GetHeightAtGrid — read height value at grid cell (col, row) in [0..1] range
// ---------------------------------------------------------------------------
float OzoneLoader::GetHeightAtGrid(int col, int row) const {
    if (col < 0 || col >= m_hmGridW || row < 0 || row >= m_hmGridH)
        return 0.0f;
    return m_hmHeights[row * m_hmGridW + col];
}

// ---------------------------------------------------------------------------
// SetHeightAtGrid — set height at grid cell, clamp to [0..1], update image
// pixel, then rebuild the mesh
// ---------------------------------------------------------------------------
void OzoneLoader::SetHeightAtGrid(int col, int row, float height, bool triggerRebuild) {
    if (!m_hmReady || col < 0 || col >= m_hmGridW || row < 0 || row >= m_hmGridH)
        return;
    if (height < 0.0f) height = 0.0f;
    if (height > 1.0f) height = 1.0f;

    int idx = row * m_hmGridW + col;
    m_hmHeights[idx] = height;

    // Keep the grayscale image in sync (for SampleHeightmapY bilinear reads)
    if (m_hmImage.data) {
        uint8_t* p = (uint8_t*)m_hmImage.data;
        p[idx] = (uint8_t)(height * 255.0f);
    }

    if (triggerRebuild)
        RebuildHeightmapMesh();
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void OzoneLoader::Unload() {
    for (auto& r : m_renderables) {
        if (r.customTex.id > 0) UnloadTexture(r.customTex);
        // Heightmap model is owned by m_hmModel (unloaded in UnloadHeightmap)
        if (r.loaded && r.typeId != (int)OzonePrimitiveType::HEIGHTMAP)
            UnloadModel(r.model);
    }
    m_renderables.clear();
    m_collisionVolumes.clear();
    m_zoneCounters.clear();
    m_worldDir.clear();
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

