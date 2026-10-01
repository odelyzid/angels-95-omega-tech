#include "OzOzoneLoader.hpp"
#include "rlgl.h"
#include "OzoneFrustum.hpp"
#include "../Renderer/OzAssetMapper.hpp"
#include "../Pawn/OzPawnSystem.hpp"
#include "../Script/LightningEntityRegistry.hpp"
#include "OzoneParser.hpp"
#include "../Log.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include "../Physics/OzBsp.hpp"
#include "../Physics/AutoConvex.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <filesystem>
namespace fs = std::filesystem;

// Strip surrounding quote characters (U+0022) from a string if present
static std::string StripQuotes(std::string s) {
    if (s.size() >= 2 && s.front() == '\"' && s.back() == '\"')
        return s.substr(1, s.size() - 2);
    return s;
}

// True for primitives that carry entity/level metadata rather than brush
// geometry â€” they are handled by ParseOzoneEntities, not the mesh builder.
static bool IsEntityPrimitive(OzonePrimitiveType t) {
    switch (t) {
        case OzonePrimitiveType::ENTITY_PLAYERSTART:
        case OzonePrimitiveType::ENTITY_PICKUP:
        case OzonePrimitiveType::ENTITY_ZONE:
        case OzonePrimitiveType::ENTITY_NPC:
        case OzonePrimitiveType::ENTITY_LIGHT:
        case OzonePrimitiveType::ENTITY_PORTAL:
        case OzonePrimitiveType::ENTITY_LEVELINFO:
        case OzonePrimitiveType::ENTITY_PARTICLES:
        case OzonePrimitiveType::ENTITY_EMITTER:
        case OzonePrimitiveType::ENTITY_MESH_STATIC:
        case OzonePrimitiveType::ENTITY_MESH_SKELETAL:
        case OzonePrimitiveType::ENTITY_PARTICLE_EMITTER:
        case OzonePrimitiveType::ENTITY_PATH_NODE:
        case OzonePrimitiveType::ENTITY_WIND_ZONE:
            return true;
        default:
            return false;
    }
}

// Fill a light's unauthored fields from its `.ozls` def (EntityType::LIGHT),
// resolved by the light's `name=` kwarg - the same lookup zones use for their
// skyzone defs, so a world-scoped `torch.ozls` next to the world works without
// a new OZONE kwarg.
//
// DEFAULTS ONLY. A value the OZONE line authored always wins, which means
// intensity / radius / color / position / target can never be moved by editing
// the def: those are positional on every light line, and a def that could
// override them would silently retune every already saved level.
static void ApplyLightDefDefaults(LightNode& node, const OzonePrimitive& prim) {
    if (node.name.empty()) return;
    const EntityDef* def = LightningEntityRegistry::Instance().Find(node.name);
    if (!def || def->type != EntityType::LIGHT) return;

    const auto& S = def->stats;
    auto stat = [&](const char* key, float& dst, bool authored) {
        if (authored) return;
        auto it = S.floats.find(key);
        if (it != S.floats.end()) dst = it->second;
    };

    // effect / flare / corona apply only when the line omitted them, which the
    // parser records as -1 (see OzonePrimitive). They are authored as floats in
    // the def and clamped into the enum's range rather than reinterpret_cast -
    // a def is data, and a bad value in it must not reach memory as bits.
    if (prim.lightEffect < 0) {
        auto it = S.floats.find("effect");
        if (it != S.floats.end()) {
            const int e = (int)it->second;
            if (e >= (int)LitLightEffect::NONE && e <= (int)LitLightEffect::LAMP)
                node.effect = (LitLightEffect)e;
        }
    }
    if (prim.lightFlare < 0) {
        auto it = S.floats.find("flare");
        if (it != S.floats.end()) node.flare = (it->second != 0.0f);
    }
    if (prim.lightCorona < 0) {
        auto it = S.floats.find("corona");
        if (it != S.floats.end()) node.corona = (it->second != 0.0f);
    }

    // Always def-owned: the light line cannot express these at all.
    stat("period", node.period, false);
    if (auto it = S.floats.find("cast_shadow"); it != S.floats.end())
        node.castShadow = (it->second != 0.0f);
    if (auto it = S.floats.find("is_static"); it != S.floats.end())
        node.isStatic = (it->second != 0.0f);
    stat("inner_cone", node.innerCone, false);
    stat("outer_cone", node.outerCone, false);
}

static bool ParseOzoneEntity(const OzonePrimitive& prim,
                             OzoneZoneCounters& zoneCounters,
                             const std::string& worldDir,
                             OzoneEntitySet& out) {
    auto& settings = out.settings;
    switch (prim.type) {
        case OzonePrimitiveType::ENTITY_PLAYERSTART:
            if (prim.args.size() >= 3) {
                out.playerStarts.push_back(
                    {0, {prim.args[0], prim.args[2], prim.args[1]},
                     prim.args.size() >= 4 ? prim.args[3] : 0.0f});
            }
            return true;
        case OzonePrimitiveType::ENTITY_PICKUP:
            if (prim.args.size() >= 3) {
                PickupNode node;
                node.position = {prim.args[0], prim.args[2], prim.args[1]};
                node.typeName = prim.entityType;
                if (prim.args.size() >= 4) node.respawnTime = prim.args[3];
                out.pickups.push_back(node);
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
                node.zoneType = ZoneTypeFromString(prim.entitySubType);
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
                    // Reverb args: 18 args => reverbMix only (decay keeps its
                    // default); 19 args => mix + decay (off-by-one fixed).
                    if (prim.args.size() >= 18)
                        node.envOverrides.reverbMix = prim.args[17];
                    if (prim.args.size() >= 19)
                        node.envOverrides.reverbDecay = prim.args[18];
                }
                // Per-zone physics overrides (named kwargs; absent = defaults)
                node.physics = prim.physics;
                out.zones.push_back(node);

                // For sky zones, also build a SkyZoneNode
                if (node.zoneType == ZoneType::ZONE_SKY) {
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
                    // names are generated per world (e.g. "zone_sky_0") â€” so a
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
                    } else if (!worldDir.empty()) {
                        // Registry lookup failed (name collided with another world's
                        // def). Read the skybox path straight from this world's own
                        // .ozls files so the correct texture still applies.
                        namespace fs = std::filesystem;
                        std::error_code ec;
                        for (auto& entry : fs::directory_iterator(worldDir, ec)) {
                            if (ec || !entry.is_regular_file()) continue;
                            std::string ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if (ext != ".ozls") continue;
                            std::ifstream in(entry.path());
                            if (!in.is_open()) continue;
                            std::string line;
                            while (std::getline(in, line)) {
                                size_t sky = line.find("skybox");
                                if (sky == std::string::npos) continue;
                                size_t q1 = line.find('"', sky);
                                size_t q2 = (q1 == std::string::npos)
                                    ? std::string::npos : line.find('"', q1 + 1);
                                if (q2 == std::string::npos) continue;
                                skyNode.skyboxPath = line.substr(q1 + 1, q2 - q1 - 1);
                                OZ_INFO("OZONE sky zone '%s': skybox from world file '%s'",
                                        node.name.c_str(), skyNode.skyboxPath.c_str());
                                break;
                            }
                            if (!skyNode.skyboxPath.empty()) break;
                        }
                    }
                    out.skyZones.push_back(skyNode);
                }
            }
            return true;
        case OzonePrimitiveType::ENTITY_NPC:
            // NPC spawning goes through PawnDef resolution, which only exists in
            // the entity system, so it is recorded as a pending spawn request.
            if (prim.args.size() >= 3) {
                PawnSpawnRequest req;
                req.position = {prim.args[0], prim.args[2], prim.args[1]};
                req.defName = prim.entityType;
                out.pawnSpawns.push_back(req);
            }
            return true;
        case OzonePrimitiveType::ENTITY_MESH_STATIC:
        case OzonePrimitiveType::ENTITY_MESH_SKELETAL: {
            // Mesh.Static|Skeletal <meshPath> x y z yaw [scale=N] [tex=path] [anim=Clip] [speed=N]
            if (prim.args.size() >= 4) {
                MeshObjectNode node;
                node.meshPath = StripQuotes(prim.meshPath);
                node.texturePath = StripQuotes(prim.texPath);
                node.position = {prim.args[0], prim.args[2], prim.args[1]}; // Z-up -> Y-up
                node.yaw = prim.args[3];
                node.scale = prim.args.size() >= 5 ? prim.args[4] : 1.0f;
                node.skeletal = (prim.type == OzonePrimitiveType::ENTITY_MESH_SKELETAL);
                node.windAffected = prim.meshWind;
                node.animClip = StripQuotes(prim.animClip);
                node.animFile = StripQuotes(prim.animFile);
                node.animSpeed = prim.animSpeed;
                node.baseDir = worldDir.empty() ? std::string() : (worldDir + "/");
                out.meshObjects.push_back(node);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_WIND_ZONE: {
            // WindZone minX minY minZ maxX maxY maxZ dirX dirY dirZ strength [freq]
            if (prim.args.size() >= 10) {
                auto arg = [&](int i, float def) -> float {
                    return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : def;
                };
                WindZoneNode z;
                z.bounds.min = {std::min(prim.args[0], prim.args[3]),
                                std::min(prim.args[2], prim.args[5]),
                                std::min(prim.args[1], prim.args[4])};
                z.bounds.max = {std::max(prim.args[0], prim.args[3]),
                                std::max(prim.args[2], prim.args[5]),
                                std::max(prim.args[1], prim.args[4])};
                z.direction = {arg(6, 1.0f), arg(8, 0.0f), arg(7, 0.0f)}; // Z-up -> Y-up
                z.strength = arg(9, 1.0f);
                z.frequency = arg(10, 1.0f);
                out.windZones.push_back(z);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_PATH_NODE: {
            // PathNode <name> x y z [radius=R] [next=a,b,c] [loop]
            if (prim.args.size() >= 3) {
                PathNode node;
                node.name = prim.entityType.empty()
                    ? ("path_" + std::to_string((int)prim.args[0]) + "_" +
                       std::to_string((int)prim.args[2]))
                    : prim.entityType;
                node.position = {prim.args[0], prim.args[2], prim.args[1]}; // Z-up -> Y-up
                if (prim.args.size() >= 4) node.radius = prim.args[3];
                node.loop = prim.pathLoop;
                std::string names = StripQuotes(prim.entitySubType);
                size_t start = 0;
                while (start <= names.size()) {
                    size_t comma = names.find(',', start);
                    std::string part = names.substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start);
                    if (!part.empty()) node.next.push_back(part);
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                out.pathNodes.push_back(node);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_PARTICLE_EMITTER: {
            // ParticleEmitter <type> x y z [rate life speed spread sizeStart sizeEnd
            //   r g b rEnd gEnd bEnd gravity radius dirX dirY dirZ yaw] [tex=path]
            if (prim.args.size() >= 3) {
                auto arg = [&](int i, float def) -> float {
                    return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : def;
                };
                ParticleEmitterNode node;
                node.type = prim.entityType.empty() ? "fire" : prim.entityType;
                node.position = {prim.args[0], prim.args[2], prim.args[1]}; // Z-up -> Y-up
                node.rate = arg(3, 20.0f);
                node.lifetime = arg(4, 1.0f);
                node.speed = arg(5, 2.0f);
                node.spread = arg(6, 0.4f);
                node.sizeStart = arg(7, 0.4f);
                node.sizeEnd = arg(8, 0.0f);
                node.colorStart = (Color){(unsigned char)arg(9, 255), (unsigned char)arg(10, 180),
                                          (unsigned char)arg(11, 80), 255};
                node.colorEnd = (Color){(unsigned char)arg(12, 60), (unsigned char)arg(13, 20),
                                        (unsigned char)arg(14, 10), 0};
                node.gravity = arg(15, 0.0f);
                node.radius = arg(16, 0.0f);
                float dx = arg(17, 0.0f), dy = arg(18, 1.0f), dz = arg(19, 0.0f);
                node.direction = {dx, dz, dy}; // Z-up -> Y-up
                node.yaw = arg(20, 0.0f);
                node.texturePath = StripQuotes(prim.texPath);
                out.particleEmitters.push_back(node);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_EMITTER:
            // emitter sound|music x y z  (Z-up conversion like the other entities)
            if (prim.args.size() >= 3) {
                EmitterNode node;
                node.type = (prim.entityType == "music") ? EmitterType::MUSIC : EmitterType::SOUND;
                node.position = {prim.args[0], prim.args[2], prim.args[1]};
                out.emitters.push_back(node);
            }
            return true;

        case OzonePrimitiveType::ENTITY_LIGHT: {
            LightNode node;
            node.active = true;
            std::string subtype = prim.entityType;
            auto arg = [&](int i) -> float {
                return (i >= 0 && i < (int)prim.args.size()) ? prim.args[i] : 0.0f;
            };
            if (subtype == "point" && prim.args.size() >= 7) {
                // light point x y z r g b intensity radius [effect] [flare] [corona]
                node.type = LitLightType::POINT;
                node.position = {arg(0), arg(2), arg(1)}; // Z-up conversion
                node.color = (Color){(unsigned char)arg(3), (unsigned char)arg(4), (unsigned char)arg(5), 255};
                node.intensity = arg(6);
                node.radius = arg(7);
                if (prim.args.size() >= 9) node.effect = (LitLightEffect)(int)arg(8);
                if (prim.args.size() >= 10) node.flare = arg(9) != 0.0f;
                if (prim.args.size() >= 11) node.corona = arg(10) != 0.0f;
            } else if (subtype == "spot" && prim.args.size() >= 12) {
                // light spot x y z tx ty tz r g b intensity radius innerCone
                //          outerCone [effect] [flare] [corona]
                node.type = LitLightType::SPOT;
                node.position = {arg(0), arg(2), arg(1)};
                node.target = {arg(3), arg(5), arg(4)};
                node.color = (Color){(unsigned char)arg(6), (unsigned char)arg(7), (unsigned char)arg(8), 255};
                node.intensity = arg(9);
                node.radius = arg(10);
                node.innerCone = arg(11);
                node.outerCone = arg(12);
                if (prim.args.size() >= 14) node.effect = (LitLightEffect)(int)arg(13);
                if (prim.args.size() >= 15) node.flare = arg(14) != 0.0f;
                if (prim.args.size() >= 16) node.corona = arg(15) != 0.0f;
            } else if (subtype == "directional" && prim.args.size() >= 6) {
                // light directional x y z r g b intensity [flare] [corona]
                node.type = LitLightType::DIRECTIONAL;
                node.target = {arg(0), arg(2), arg(1)};
                // A directional light has no emitter position, only an aim
                // point. The lighting shader resolves its direction as
                //     lightDir = normalize(position - target)
                // so leaving position at the origin made lightDir point from the
                // authored point DOWN to the world: for an overhead sun
                // (`... 100` in OZONE z) every up-facing surface got NdotL = 0 and
                // the ground was lit by ambient alone. That is why outdoor
                // levels needed absurd ambient values to be visible at all.
                //
                // Treat the authored point as the light SOURCE and aim at the
                // world origin, which is the intuitive reading of
                // `light directional <x> <y> <z>` and makes an overhead sun
                // light the ground.
                node.position = node.target;
                node.target = {0.0f, 0.0f, 0.0f};
                node.color = (Color){(unsigned char)arg(3), (unsigned char)arg(4), (unsigned char)arg(5), 255};
                node.intensity = arg(6);
                if (prim.args.size() >= 8) node.flare = arg(7) != 0.0f;
                if (prim.args.size() >= 9) node.corona = arg(8) != 0.0f;
            } else {
                OZ_WARN("OZONE: invalid light definition (subtype=%s args=%zu)", subtype.c_str(), prim.args.size());
                return true;
            }
            // Named kwargs win over the positional tail. They are what the
            // editor writes, because the positional form is ambiguous: with no
            // `effect` authored, a following flare float slid into the effect
            // slot and a TORCH-less flickering light came back as WATERY.
            if (prim.lightEffect >= 0) node.effect = (LitLightEffect)prim.lightEffect;
            if (prim.lightFlare  >= 0) node.flare  = prim.lightFlare != 0;
            if (prim.lightCorona >= 0) node.corona = prim.lightCorona != 0;
            if (!prim.name.empty())   node.name = prim.name;

            // `.ozls` defaults layer. `name=` resolves a def exactly the way a
            // zone's name= resolves its skyzone def, and the def fills only the
            // values the line did NOT author.
            //
            // intensity / radius / color / position / target stay line-owned on
            // purpose: the OZONE light line always writes them positionally, so a
            // def that could override them would silently retune every already
            // saved level the next time the def was touched - the exact hazard
            // the positional/kwarg split above was added to avoid.
            ApplyLightDefDefaults(node, prim);
            out.lights.push_back(node);
            return true;
        }
        case OzonePrimitiveType::ENTITY_PORTAL: {
            // portal targetWorld minX minY minZ maxX maxY maxZ [spawnX spawnY spawnZ] [bidir]
            // Coordinates are OZONE Z-up â€” convert to engine Y-up.
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
                out.portals.push_back(portal);
            }
            return true;
        }
        case OzonePrimitiveType::ENTITY_LEVELINFO:
            // levelinfo gameType maxPlayers respawnTime timeLimitEnabled timeLimitMinutes
            //           scoreLimit friendlyFire skyboxPath [skyboxSidePath]
            ParseLevelInfo(settings, prim.args,
                           StripQuotes(prim.entityType),
                           StripQuotes(prim.entitySubType));
            return true;
        case OzonePrimitiveType::ENTITY_PARTICLES:
            // particles type density speed r g b windX windZ
            ParseLevelParticles(settings, prim.args);
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Entity ingestion
// ---------------------------------------------------------------------------
void ParseOzoneEntities(const std::vector<OzonePrimitive>& primitives,
                        const std::string& worldDir,
                        OzoneZoneCounters& zoneCounters,
                        OzoneEntitySet& out) {
    for (const auto& prim : primitives)
        ParseOzoneEntity(prim, zoneCounters, worldDir, out);
}

void InjectOzoneEntities(const OzoneEntitySet& entities, PawnSystem& pawns) {
    auto& zones = ZoneManager::Instance();

    for (const auto& n : entities.playerStarts)
        pawns.AddPlayerStart(n);
    for (const auto& n : entities.pickups)
        pawns.AddPickup(n);
    for (const auto& n : entities.zones)
        zones.AddZone(n);
    for (const auto& n : entities.skyZones)
        pawns.AddSkyZone(n);
    for (const auto& n : entities.portals)
        zones.AddPortal(n);
    for (const auto& n : entities.lights)
        pawns.AddLight(n);
    for (const auto& n : entities.meshObjects)
        pawns.AddMeshObject(n);
    for (const auto& n : entities.pathNodes)
        pawns.AddPathNode(n);
    for (const auto& n : entities.windZones)
        pawns.AddWindZone(n);
    for (const auto& n : entities.particleEmitters)
        pawns.AddParticleEmitter(n);
    for (const auto& n : entities.emitters)
        pawns.AddEmitter(n);
    // NPC spawns resolve PawnDefs, so they must run after the defs are registered.
    for (const auto& req : entities.pawnSpawns)
        pawns.Spawn(req.position, req.defName.c_str());

    // Lights only make sense bound to a volume, and volumes live in ZoneManager.
    pawns.AssignLightZones();

    pawns.GetWorldInfo().settings = entities.settings;
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
// World texture loading â€” loads ALL .png from oztex/tileset/ into vector
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
// Build* Ã¢â‚¬â€ each applies the best texture for its role
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
// Resolve a texture/heightmap path from an OZONE file against the world dir.
// Accepts, in order: an existing path, a repo-relative GameData/ path, an
// absolute path, and finally a world-relative path (oztex/...).
// ---------------------------------------------------------------------------
static std::string ResolveWorldAssetPath(const std::string& raw,
                                         const std::string& gameDataWorldDir,
                                         const std::string& worldDir) {
    std::string s = StripQuotes(raw);
    if (s.empty()) return s;
    if (IsPathFile(s.c_str())) return s;
    if (s.rfind("GameData/", 0) == 0 || s.rfind("GameData\\", 0) == 0) return s;
    bool absolute = (s.size() > 1 && s[1] == ':') || s[0] == '/' || s[0] == '\\';
    if (absolute) return s;
    if (!gameDataWorldDir.empty()) return gameDataWorldDir + s;
    if (!worldDir.empty()) return worldDir + s;
    return s;
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
// LoadFile Ã¢â‚¬â€ also loads world textures from the .ozone file's directory
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
        // Extract world name for GameData path resolution (e.g. "world_EngineTest.ozone" â†’ "EngineTest")
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

    // Entities are parsed into a plain set first; the orchestrator decides when
    // (and whether) to inject them into the runtime systems.
    ParseOzoneEntities(primitives, m_worldDir, m_zoneCounters, m_entities);

    for (auto& prim : primitives) {
        if (IsEntityPrimitive(prim.type)) continue;

        // Heightmap is handled specially â€” builds its own model from image path
        if (prim.type == OzonePrimitiveType::HEIGHTMAP) {
        OzoneRenderable r;
        r.typeId = (int)prim.type;
        r.position = {0,0,0};
        r.scale = 1.0f;
        // Resolve relative paths: prefer GameData/Worlds/<name>/, fall back to package directory
        std::string imgPath = StripQuotes(prim.entityType);
        std::string texPath = StripQuotes(prim.entitySubType);
        // Keep the authored relative paths for round-trip export
        m_hmImageRel = imgPath;
        m_hmTexRel = texPath;
        imgPath = ResolveWorldAssetPath(imgPath, gameDataWorldDir, worldDir);
        if (!texPath.empty()) texPath = ResolveWorldAssetPath(texPath, gameDataWorldDir, worldDir);
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
    // the world directory unless already rooted at GameData/ or absolute
    if (!prim.texPath.empty()) {
        std::string tp = ResolveWorldAssetPath(prim.texPath, gameDataWorldDir, worldDir);
        ApplyRenderableTexture((int)m_renderables.size() - 1, tp.c_str());
    }
    }

    // Lights are bound to zone volumes by InjectOzoneEntities(), not here.
    RebuildCollisionVolumes();
    int r0shader = (!m_renderables.empty() && m_renderables[0].model.materialCount > 0)
        ? m_renderables[0].model.materials[0].shader.id : -1;
    OZ_INFO("OZONE: entities=%zu litShader=%d renderable0.shader=%d",
            m_entities.Count(), GetLitFogShader().id, r0shader);
    OZ_INFO("OzoneLoader: loaded %zu primitives, %zu collision volumes from %s",
            primitives.size(), m_collisionVolumes.size(), path);
    return true;
}

// ---------------------------------------------------------------------------
// LoadString
// ---------------------------------------------------------------------------
bool OzoneLoader::LoadString(const char* data, const char* worldDir) {
    Unload();

    // Optional world directory: load the tileset before parsing so texSlot
    // indices resolve to real textures (editor snapshot restore path).
    if (worldDir && worldDir[0]) {
        std::string wd(worldDir);
        if (!wd.empty() && wd.back() != '/' && wd.back() != '\\')
            wd += '/';
        LoadWorldTextures(wd);
        m_worldDir = wd;
    }

    auto primitives = OzoneParser::parse_string(data);
    if (primitives.empty()) return false;

    ParseOzoneEntities(primitives, m_worldDir, m_zoneCounters, m_entities);

    for (auto& prim : primitives) {
        if (IsEntityPrimitive(prim.type)) continue;

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
    // World-space AABB for frustum culling
    ComputeCollisionAABB((int)prim.type, prim.args, r.position, r.bounds);
    r.hasBounds = true;
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
    // Lights are bound to zone volumes by InjectOzoneEntities(), not here.
    RebuildCollisionVolumes();
    return true;
}


// ---------------------------------------------------------------------------
// Draw â€” all renderables (backward compat, used by editor)
// ---------------------------------------------------------------------------
void OzoneLoader::Draw(Camera3D& camera) {
    FrustumPlane planes[6];
    BuildFrustum(camera, planes);
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        // Generated collision proxies are invisible unless the editor has
        // explicitly asked for them - otherwise a single AutoConvex pass would
        // bury the level in a wall of grey boxes.
        if ((r.surfaceFlags & SURF_COLLISION_PROXY) && !m_drawCollisionProxies) continue;
        if (r.hasBounds && (r.bounds.min.x < r.bounds.max.x ||
                            r.bounds.min.y < r.bounds.max.y ||
                            r.bounds.min.z < r.bounds.max.z) &&
            !AabbInFrustum(planes, r.bounds)) continue;
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
            DrawModelEx(m_hmModel, m_hmPosition, (Vector3){0,1,0}, 0,
                        (Vector3){m_hmScale, m_hmScale, m_hmScale}, WHITE);
        } else {
            DrawModel(r.model, r.position, r.scale, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// DrawWorldGeometry â€” skip SURF_FAKEBACKDROP flagged brushes
// ---------------------------------------------------------------------------
void OzoneLoader::DrawWorldGeometry(Camera3D& camera) {
    FrustumPlane planes[6];
    BuildFrustum(camera, planes);
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        if (r.surfaceFlags & SURF_FAKEBACKDROP) continue;
        // Collision proxies are never drawn in-game, whatever the editor toggle
        // says: they exist to stop the player falling through a prop.
        if (r.surfaceFlags & SURF_COLLISION_PROXY) continue;
        if (r.hasBounds && (r.bounds.min.x < r.bounds.max.x ||
                            r.bounds.min.y < r.bounds.max.y ||
                            r.bounds.min.z < r.bounds.max.z) &&
            !AabbInFrustum(planes, r.bounds)) continue;
        if (r.typeId == (int)OzonePrimitiveType::HEIGHTMAP && m_hmReady) {
            DrawModelEx(m_hmModel, m_hmPosition, (Vector3){0,1,0}, 0,
                        (Vector3){m_hmScale, m_hmScale, m_hmScale}, WHITE);
        } else {
            DrawModel(r.model, r.position, r.scale, WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// ComputeCollisionAABB Ã¢â‚¬â€ generate world-space AABB from primitive params
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
        case OzonePrimitiveType::PLANE: {
            // BuildPlane renders a fixed 10x10 horizontal patch (normal args are
            // not applied yet), so the AABB is a thin slab to match.
            constexpr float kPlaneExtent = 5.0f;   // half of GenMeshPlane(10,10)
            constexpr float kPlaneThickness = 0.05f;
            out.min = {position.x - kPlaneExtent, position.y - kPlaneThickness, position.z - kPlaneExtent};
            out.max = {position.x + kPlaneExtent, position.y + kPlaneThickness, position.z + kPlaneExtent};
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// RebuildCollisionVolumes Ã¢â‚¬â€ iterate renderables and generate AABBs
// ---------------------------------------------------------------------------
void OzoneLoader::RebuildCollisionVolumes() {
    m_collisionVolumes.clear();

    // Phase 1: collect all brush AABBs with their CSG operations
    CsgProcessor csg;
    for (auto& r : m_renderables) {
        if (!r.loaded) continue;
        // Skip entity types Ã¢â‚¬â€ handled by PawnSystem
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
        // Zero-thickness geometry (plane slabs) can't overlap in CSG â€” inflate
        // slightly so SUB/INTERSECT against them still produces a volume.
        if (brush.maxY - brush.minY < 0.02f) {
            brush.minY -= 0.05f;
            brush.maxY += 0.05f;
        }
        brush.op   = (CsgOp)r.csgOp;
        csg.Apply(brush);
    }

    // Overflow protection: merge adjacent coplanar AABBs
    int merges = csg.MergePass();
    if (merges > 0) {
        OZ_INFO("CSG: merged %d adjacent volumes (count: %d Ã¢â€ â€™ %d)",
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
// AddBrushRenderable â€” editor helper to make a new brush visible
// ---------------------------------------------------------------------------
int OzoneLoader::AddBrushRenderable(int primType, const Vector3& pos,
                                    const Vector3& size, float rot,
                                    float scale, int csgOp, int surfaceFlags) {
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
    // Store rotation in radians (matches file-loaded renderables); the editor
    // passes degrees from its UI fields.
    r.rotation = rot * DEG2RAD;
    r.model = mdl;
    r.loaded = true;
    r.csgOp = csgOp;
    r.surfaceFlags = surfaceFlags;
    m_renderables.push_back(r);
    return (int)m_renderables.size() - 1;
}

// ---------------------------------------------------------------------------
// AppendAutoConvexCollision - voxelise a mesh into convex collision boxes.
//
// The engine's collision world is AABB-only (CsgProcessor, see OzBsp.hpp), so a
// placed Mesh.Static prop has no collision whatsoever and the player walks
// through it. Each generated box is appended as a real SURF_COLLISION_PROXY
// brush so it exports to the .ozone as a plain `add box ... flags=N` line and
// survives a save/load round trip - the proxies are authored data, not a
// derived cache that would be lost on the next edit.
//
// Everything is built up front and only committed once the whole set is known
// to fit inside `maxBoxes`: a half-appended hull would leave a gap the player
// can fall through, which is exactly the bug this feature exists to fix.
// ---------------------------------------------------------------------------
int OzoneLoader::AppendAutoConvexCollision(const float* verts, int vertFloatCount,
                                           float cellSize, int maxBoxes) {
    AutoConvexParams params;
    params.cellSize = cellSize;
    params.maxBoxes = maxBoxes;

    std::vector<ConvexBox> boxes;
    int n = AutoConvex_Build(verts, vertFloatCount, params, boxes);
    if (n <= 0) return 0;

    for (int i = 0; i < n; i++) {
        const ConvexBox& b = boxes[i];
        Vector3 size = {b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ};
        Vector3 center = {(b.minX + b.maxX) * 0.5f,
                          (b.minY + b.maxY) * 0.5f,
                          (b.minZ + b.maxZ) * 0.5f};
        // SOLID (not ADD): a proxy is a plain solid volume, and SOLID is what
        // CsgProcessor treats as additive - see CsgOp in OzBsp.hpp.
        AddBrushRenderable((int)OzonePrimitiveType::BOX, center, size,
                           0.0f, 1.0f, (int)CsgOp::SOLID, SURF_COLLISION_PROXY);
    }

    RebuildCollisionVolumes();
    OZ_INFO("AutoConvex: appended %d collision proxies (%zu total renderables)",
            n, m_renderables.size());
    return n;
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
    r->rotation = rot * DEG2RAD; // caller passes degrees; store radians

    // Re-apply the Y-center adjustment for cylinder/pyramid
    if (r->typeId == 1) r->position.y -= size.z / 2.0f;  // cylinder: h = size.z
    if (r->typeId == 3) r->position.y -= size.y / 2.0f;  // pyramid: h = size.y
}

// ---------------------------------------------------------------------------
// DrawZoneGeometry Ã¢â‚¬â€ draw renderables with SURF_FAKEBACKDROP flag set
// (with optional bounds filter for backward compat)
// ---------------------------------------------------------------------------
void OzoneLoader::DrawZoneGeometry(Camera3D& camera, const BoundingBox& zoneBounds) {
    // SURF_FAKEBACKDROP brushes are 2D painted backdrops, the Ocarina of Time /
    // Majora's Mask trick: they stand in for distant scenery and must be seen at
    // their full painted value. Through the lit shader they do not get that:
    //   litColor = baseColor * (colDiffuse * lightAccum)
    //            + baseColor * (ambient / 10) * colDiffuse
    // so any panel facing away from the directional sun has colDiffuse ~ 0 and
    // collapses to black â€” which is what turned the fortress perimeter into a
    // black void. For this pass only, force colDiffuse to white and ambient to
    // 1.0-in-units, which reduces the expression to exactly the painted texture.
    // Both uniforms are re-applied by the lighting/zone-env passes every frame.
    Shader lit = s_litFogShader;
    static int locDiffuse = -1, locAmbient = -1;
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    // 5.5 rather than 10.0: full value (10.0) renders the painted panels at 100%
    // and blows the pale stone out to flat white. 5.5 keeps them clearly brighter
    // than the lit geometry while preserving the painting's own tonality.
    const float backdrop[4] = { 5.5f, 5.5f, 5.5f, 1.0f };
    if (lit.id > 0) {
        if (locDiffuse < 0) {
            locDiffuse = GetShaderLocation(lit, "colDiffuse");
            locAmbient = GetShaderLocation(lit, "ambient");
        }
        if (locDiffuse >= 0)
            SetShaderValue(lit, locDiffuse, white, SHADER_UNIFORM_VEC4);
        if (locAmbient >= 0)
            SetShaderValue(lit, locAmbient, backdrop, SHADER_UNIFORM_VEC4);
    }


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

        // SURF_FAKEBACKDROP brushes are 2D painted backdrops, the Ocarina of
        // Time / Majora's Mask trick: they stand in for distant scenery and are
        // meant to be seen at full painted value. Drawing them through the lit
        // shader made every panel facing away from the directional sun collapse
        // to black (litColor = baseColor * colDiffuse * lightAccum, and ambient
        // only contributes ambient/10), which turned the fortress perimeter into
        // a black void. The uniforms set above reduce the lit expression to the
        // painted texture for this pass.
        DrawModel(r.model, r.position, r.scale, WHITE);
    }

    // Restore a neutral ambient. The env/lighting passes re-apply the world's
    // real value next frame, but leaving ambient=10 here would leak into
    // anything drawn after this function.
    if (lit.id > 0 && locAmbient >= 0) {
        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        SetShaderValue(lit, locAmbient, one, SHADER_UNIFORM_VEC4);
    }
}


// ---------------------------------------------------------------------------
// DrawZoneGeometry - draw all SURF_FAKEBACKDROP brushes (no bounds filter)
// ---------------------------------------------------------------------------
// Delegates to the bounds-filtered overload with a box that always contains the
// backdrop, so the unlit-painted-backdrop handling lives in exactly one place.
// (Two separate loops previously meant the fix had to be duplicated, and this
// overload - the one the renderer actually calls - was missed.)
void OzoneLoader::DrawZoneGeometry(Camera3D& camera) {
    const BoundingBox all = { {-1e9f, -1e9f, -1e9f}, {1e9f, 1e9f, 1e9f} };
    DrawZoneGeometry(camera, all);
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
    m_entities.Clear();
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
