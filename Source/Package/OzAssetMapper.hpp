#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// AssetMapper — central path-alias registry & texture cache
//
// Aliases map short logical names to real paths under GameData/.
// Two built-in groups are registered at Init():
//   "items"  → GameData/Global/Items/<alias>.png
//   "engine" → GameData/Global/Engine/<alias>.png
//
// Example usage:
//   AssetMapper::Instance().PreloadCategory("items");
//   Texture2D t = AssetMapper::Instance().GetTexture("Coin");
// ---------------------------------------------------------------------------

struct AssetMapEntry {
    const char* alias;
    const char* path;
    const char* category;
    Texture2D texture;
};

class AssetMapper {
public:
    // Register all known entries (called once at startup)
    void Init();

    // Resolve an alias to its full relative path; returns nullptr if unknown
    const char* ResolvePath(const char* alias) const;

    // Get a cached texture, loading it on first access
    Texture2D GetTexture(const char* alias);

    // Bulk preload every entry belonging to the given category
    void PreloadCategory(const char* category);

    // Unload textures for a specific category
    void UnloadCategory(const char* category);

    // Unload every cached texture
    void UnloadAll();

    // Singleton
    static AssetMapper& Instance();

    // Access registered entries (for iteration / debug)
    const std::vector<AssetMapEntry>& Entries() const { return m_entries; }

private:
    std::vector<AssetMapEntry> m_entries;
    std::vector<std::string> m_strings;  // owns the string data for entry paths
    std::unordered_map<std::string, Texture2D> m_cache;

    AssetMapEntry* Find(const char* alias);

    void RegisterItemTextures();
    void RegisterEngineTextures();
};
