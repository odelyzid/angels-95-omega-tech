#include "LightningEntityRegistry.hpp"
#include "LightningScriptParser.hpp"
#include "../Log.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Init — scan packages + GameData/ for *.ozls files
// ---------------------------------------------------------------------------
void LightningEntityRegistry::Init() {
    m_defs.clear();
    std::vector<std::string> contents;
    std::vector<std::string> paths;

    // Scan GameData/ recursively for .ozls
    fs::path gd = fs::current_path() / "GameData";
    if (fs::exists(gd)) {
        for (auto& entry : fs::recursive_directory_iterator(gd)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".ozls") {
                    std::ifstream file(entry.path());
                    if (file.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(file)),
                                             std::istreambuf_iterator<char>());
                        contents.push_back(content);
                        paths.push_back(entry.path().string());
                    }
                }
            }
        }
    }

    // Scan packages for .ozls entries
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        std::string ext = pkgPath.substr(pkgPath.rfind('.'));
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".ozls") {
            size_t sz;
            const uint8_t* data = PackageAssetLoader::Instance().Find(pkgPath.c_str(), sz);
            if (data) {
                contents.push_back(std::string((const char*)data, sz));
                paths.push_back(pkgPath);
            }
        }
    }

    // Parse all found .ozls files
    auto defs = LightningScriptParser::ParseAll(contents, paths);
    for (auto& def : defs) {
        m_defs[def.name] = std::move(def);
    }

    OZ_INFO("LightningRegistry: loaded %zu entity definitions%s",
            m_defs.size(),
            m_defs.size() > 0 ? "" : " (no .ozls files found)");
}

// ---------------------------------------------------------------------------
// Find — look up by exact name
// ---------------------------------------------------------------------------
const EntityDef* LightningEntityRegistry::Find(const std::string& name) const {
    auto it = m_defs.find(name);
    if (it != m_defs.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// FindByType — collect all defs of a given type
// ---------------------------------------------------------------------------
void LightningEntityRegistry::FindByType(EntityType type, std::vector<const EntityDef*>& out) const {
    out.clear();
    for (auto& [name, def] : m_defs) {
        if (def.type == type) out.push_back(&def);
    }
}

// ---------------------------------------------------------------------------
// Register — add or update a single definition (used by editor live reload)
// ---------------------------------------------------------------------------
bool LightningEntityRegistry::Register(const EntityDef& def) {
    if (def.name.empty() || def.type == EntityType::UNKNOWN) return false;
    m_defs[def.name] = def;
    return true;
}
