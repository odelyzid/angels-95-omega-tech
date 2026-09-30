#ifndef OMEGA_OZONE_PARSER_HPP
#define OMEGA_OZONE_PARSER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdint>
#include "../Physics/PhysicsInfo.hpp"

// Standalone .ozone parser - no raylib dependency.
// Parses the OZONE text format used by the OzWorld editor.

enum class OzonePrimitiveType : uint8_t {
    BOX,
    CYLINDER,
    SPHERE,
    PYRAMID,
    PLANE,
    ENTITY_PLAYERSTART,  // Player spawn point
    ENTITY_PICKUP,       // Pickup node (item)
    ENTITY_ZONE,         // Zone volume
    ENTITY_NPC,          // NPC spawn node
    ENTITY_LIGHT,        // Light node (point/spot/directional)
    ENTITY_PORTAL,       // Portal zone (level-to-level connection)
    ENTITY_LEVELINFO,    // Level metadata (game rules, skybox)
    ENTITY_PARTICLES,    // Ambient particle weather settings
    ENTITY_EMITTER,      // Sound/music emitter marker (emitter sound|music x y z)
    ENTITY_MESH_STATIC,  // GameEngine.Mesh.Static   (placed static prop/object)
    ENTITY_MESH_SKELETAL,// GameEngine.Mesh.Skeletal (placed animated object)
    ENTITY_PARTICLE_EMITTER, // GameEngine.ParticleEmitter (local 3D particles)
    ENTITY_PATH_NODE,    // GameEngine.PathNode (NPC waypoint)
    ENTITY_WIND_ZONE,    // WindZone (foliage wind region)
    HEIGHTMAP,           // Terrain heightmap (grayscale image)
    UNKNOWN
};

struct OzonePrimitive {
    OzonePrimitiveType type;
    std::vector<float> args;        // position, dimensions, etc.
    std::string entityType;         // for entity types: "Walker", "HealthVial", etc.
    std::string entitySubType;      // for zones: "Water", "Ladder", "Sky", "Reverb"
    std::string name;               // optional explicit name (zone name= kwarg for script hooks)
    int csgOp = 0;                  // CSG operation: 0=SOLID, 1=ADD, 2=SUB, 3=INTERSECT, 4=DE_RESC
    int surfaceFlags = 0;           // surface behavior flags (e.g. SURF_FAKEBACKDROP)
    std::string texPath;            // custom diffuse texture path (texPath="..." attribute)
    float texScaleU = 1.0f;         // texture tiling U (applied to mesh UVs)
    float texScaleV = 1.0f;         // texture tiling V (applied to mesh UVs)
    float texOffsetU = 0.0f;        // texture shift U
    float texOffsetV = 0.0f;        // texture shift V
    std::string meshPath;           // GameEngine.Mesh.*: model path (mesh=Skeletal/default)
    std::string animClip;           // GameEngine.Mesh.Skeletal: embedded clip name
    std::string animFile;           // GameEngine.Mesh.Skeletal: external .ozanim file
    float animSpeed = 1.0f;         // GameEngine.Mesh.Skeletal: playback speed
    bool pathLoop = false;          // GameEngine.PathNode: loop to first node
    bool meshWind = false;          // GameEngine.Mesh.*: foliage wind-affection flag
    bool hasPhysics = false;        // zones: PhysicsInfo kwargs were authored
    oz::physics::PhysicsInfo physics; // zones: overrides (defaults when !hasPhysics)
};

class OzoneParser {
public:
    static std::vector<OzonePrimitive> parse_file(const std::string& path);
    static std::vector<OzonePrimitive> parse_string(const std::string& content);
};

#endif // OMEGA_OZONE_PARSER_HPP
