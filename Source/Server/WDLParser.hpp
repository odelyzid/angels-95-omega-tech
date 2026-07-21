#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdint>

// Standalone WDL parser - no raylib dependency.
// Used by oz_server to serialize .wdl files to JSON.

enum class WDLElementType : uint8_t {
    MODEL,                 // 1 = Model entity
    COLLISION,              // 2 = Collision wireframe
    ADV_COLLISION,          // 3 = Advanced Collision box
    CLIP_BOX,               // 4 = ClipBox wireframe
    LIGHT,                  // 5 = Light entity (position)
    HEIGHTMAP,              // 6 = HeightMap entity
    SCRIPT,                 // 7 = Script entity
    OBJECT,                 // 8 = Generic Object entity
    PICKUP,                 // 9 = Pickup entity (position, type)
    SPAWN,                  // 10 = PlayerStart entity (position, yaw)
    NPC,                    // 11 = NPC entity (position, type)
    FOG,                    // 12 = Fog settings
    LIGHT_TYPE,             // 13 = Light entity (detailed)
    AMBIENT_TYPE,           // 14 = Ambient entity (RGB, intensity)
    ZONE_INFO,              // 15 = ZoneInfo entity (AABB, zone type)
    ENTITY_WALKER,          // 16 = Walker optimization node (legacy)
    NOISE_EMITTER,          // 17 = Legacy noise emitter
    COL_FLAG,               // 18 = Collision flag data
    UNKNOWN = 255           // Unrecognized type
};

struct WDLElement {
    WDLElementType type = WDLElementType::UNKNOWN;
    int int_id = 0;           // Primary identifier (e.g. model number)
    std::vector<float> args;  // 2-6 float values for coordinates/scale/rotation

    // Entity-specific fields
    std::string entityType = "";    // e.g., "Walker", "HealthVial", "PlayerStart"
    std::string zoneType = "";      // e.g., "Water", "Ladder", "Sky"
    std::string pickupType = "";    // e.g., "HealthVial", "Coin"
    float scale = 1.0f;
    float yaw = 0.0f;
    float intensity = 1.0f;
};

class WDLParser {
public:
    // Parse a .wdl file. Returns empty vector on failure.
    static std::vector<WDLElement> parse_file(const std::string& path);

    // Parse already-loaded content string.
    static std::vector<WDLElement> parse_string(const std::string& content);

    // Helper to get element name from type.
    static const char* GetWDLTypeName(WDLElementType type);
private:
    static WDLElementType classify(const std::string& token);
};
