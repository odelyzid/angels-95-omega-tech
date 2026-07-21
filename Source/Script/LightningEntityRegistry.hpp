#pragma once
#include "LightningEntityDef.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// LightningEntityRegistry — global registry of entity prototypes
// Scans packages + GameData/ for *.ozls files at startup
class LightningEntityRegistry {
public:
    static LightningEntityRegistry& Instance() {
        static LightningEntityRegistry instance;
        return instance;
    }

    // Scan all loaded packages + GameData/ for .ozls files
    void Init();

    // Look up an entity definition by name
    const EntityDef* Find(const std::string& name) const;

    // Find all definitions of a given type
    void FindByType(EntityType type, std::vector<const EntityDef*>& out) const;

    // Total definitions loaded
    int Count() const { return (int)m_defs.size(); }

    // Direct access
    const std::unordered_map<std::string, EntityDef>& GetAll() const { return m_defs; }

    // Register a single parsed definition (used by editor for live reload)
    bool Register(const EntityDef& def);

private:
    LightningEntityRegistry() = default;
    std::unordered_map<std::string, EntityDef> m_defs;
};
