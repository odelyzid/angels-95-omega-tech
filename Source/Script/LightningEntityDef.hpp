#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// LightningScript entity type system — replaces hardcoded Objects.hpp
// Entity types
enum class EntityType : uint8_t {
    WEAPON,
    ARMOR,
    CONSUMABLE,
    UPGRADE,
    PICKUP,
    PROJECTILE,
    PAWN,
    SKYZONE,
    UNKNOWN
};

inline const char* EntityTypeName(EntityType t) {
    switch (t) {
        case EntityType::WEAPON:     return "weapon";
        case EntityType::ARMOR:      return "armor";
        case EntityType::CONSUMABLE: return "consumable";
        case EntityType::UPGRADE:    return "upgrade";
        case EntityType::PICKUP:     return "pickup";
        case EntityType::PROJECTILE: return "projectile";
        case EntityType::PAWN:       return "pawn";
        case EntityType::SKYZONE:    return "skyzone";
        default:                     return "unknown";
    }
}

inline EntityType EntityTypeFromName(const std::string& n) {
    if (n == "weapon")     return EntityType::WEAPON;
    if (n == "armor")      return EntityType::ARMOR;
    if (n == "consumable") return EntityType::CONSUMABLE;
    if (n == "upgrade")    return EntityType::UPGRADE;
    if (n == "pickup")     return EntityType::PICKUP;
    if (n == "projectile") return EntityType::PROJECTILE;
    if (n == "pawn")       return EntityType::PAWN;
    if (n == "skyzone")    return EntityType::SKYZONE;
    return EntityType::UNKNOWN;
}

// One stat (float) or string value parsed from .ozls
struct EntityStatBlock {
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, float[3]> vec3s;  // e.g. fog_color
};

// Action block — a named list of script lines
struct EntityAction {
    std::string name;           // "on_fire", "on_enter", etc.
    std::vector<std::string> scriptLines;
};

// Level variant — mesh/texture override per level
struct EntityVariant {
    std::string name;
    std::string meshOverride;
    std::string textureOverride;
};

// Full entity definition parsed from .ozls
struct EntityDef {
    std::string name;
    EntityType type = EntityType::UNKNOWN;
    std::string mesh;
    std::string texture;
    std::string icon;
    EntityStatBlock stats;
    std::vector<EntityAction> actions;
    std::vector<EntityVariant> variants;
    // Skyzone-specific fields (stored in stats.strings/vec3s for simplicity)
    std::string skybox;
    std::string music;
    std::string sourcePath;  // where this was parsed from
};
