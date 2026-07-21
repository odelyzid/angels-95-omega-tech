#include "oz_assetmapper.h"
#include "Log.hpp"
#include <cstring>
#include <cctype>
#include <cstdio>

// ---------------------------------------------------------------------------
// Case-insensitive filename matching helper
// ---------------------------------------------------------------------------
static bool iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
        a++; b++;
    }
    return *a == *b;
}

static void to_lower_inplace(std::string& s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
}

// ---------------------------------------------------------------------------
// Fallback file probe: try path as-is, then lowercase, then .png fallback
// Returns the first path that exists, or the original path (caller handles).
// ---------------------------------------------------------------------------
static std::string probe_path(const char* baseDir, const char* name, const char* ext) {
    std::string path = std::string(baseDir) + "/" + name + ext;
    if (IsPathFile(path.c_str()))
        return path;

    // Try lowercase filename
    std::string lower = name;
    to_lower_inplace(lower);
    std::string path2 = std::string(baseDir) + "/" + lower + ext;
    if (IsPathFile(path2.c_str()))
        return path2;

    // Try .png fallback if ext was .gif
    if (strcmp(ext, ".gif") == 0) {
        std::string pngPath = std::string(baseDir) + "/" + name + ".png";
        if (IsPathFile(pngPath.c_str()))
            return pngPath;
        std::string pngPath2 = std::string(baseDir) + "/" + lower + ".png";
        if (IsPathFile(pngPath2.c_str()))
            return pngPath2;
    }

    // Return original probe — caller will get a fallback texture
    return path;
}

// ---------------------------------------------------------------------------
// Generate a small "missing" grid texture
// ---------------------------------------------------------------------------
static Texture2D MakeGridTexture() {
    const int w = 16, h = 16;
    Image img = GenImageChecked(w, h, 4, 4, (Color){80, 0, 80, 255}, (Color){40, 0, 40, 255});
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
AssetMapper& AssetMapper::Instance() {
    static AssetMapper instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Find entry by alias (case-insensitive)
// ---------------------------------------------------------------------------
AssetMapEntry* AssetMapper::Find(const char* alias) {
    for (auto& e : m_entries) {
        if (iequals(e.alias, alias))
            return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Init — register all known Engine and Items textures
// ---------------------------------------------------------------------------
void AssetMapper::Init() {
    RegisterEngineTextures();
    RegisterItemTextures();
}

// ---------------------------------------------------------------------------
// Register engine icons (always .png, never .gif)
// ---------------------------------------------------------------------------
void AssetMapper::RegisterEngineTextures() {
    struct { const char* alias; } engineIcons[] = {
        {"Light"},
        {"Music"},
        {"PawnNode"},
        {"PlayerStart"},
        {"Sound"},
        {"ZoneInfo"},
    };
    const char* baseDir = "GameData/Global/Engine";
    for (auto& ic : engineIcons) {
        std::string path = probe_path(baseDir, ic.alias, ".png");
        m_strings.push_back(path);
        m_entries.push_back({ic.alias, m_strings.back().c_str(), "engine", {0}});
    }
}

// ---------------------------------------------------------------------------
// Register item textures (some have .gif variants for animated billboards)
// ---------------------------------------------------------------------------
void AssetMapper::RegisterItemTextures() {
    // Items with both .png and .gif
    struct { const char* alias; } itemIcons[] = {
        {"Coin"},
        {"EnergyCrystal"},
        {"HealthVial"},
        {"ManaVial"},
        {"Powerup"},
    };
    const char* baseDir = "GameData/Global/Items";
    for (auto& ic : itemIcons) {
        // Try .gif first (animated billboard), fall back to .png
        std::string path = probe_path(baseDir, ic.alias, ".gif");
        m_strings.push_back(path);
        m_entries.push_back({ic.alias, m_strings.back().c_str(), "items", {0}});
    }

    // Key — file on disk is lowercase "key.png"
    {
        std::string keyPath = probe_path(baseDir, "Key", ".png");
        m_strings.push_back(keyPath);
        m_entries.push_back({"Key", m_strings.back().c_str(), "items", {0}});
    }

    // alpha_key — file on disk is lowercase "alpha_key.png"
    {
        std::string akPath = probe_path(baseDir, "alpha_key", ".png");
        m_strings.push_back(akPath);
        m_entries.push_back({"alpha_key", m_strings.back().c_str(), "items", {0}});
    }
}

// ---------------------------------------------------------------------------
// ResolvePath — returns full relative path for alias, or nullptr
// ---------------------------------------------------------------------------
const char* AssetMapper::ResolvePath(const char* alias) const {
    for (auto& e : m_entries) {
        if (iequals(e.alias, alias))
            return e.path;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetTexture — load on first access, cache, fallback to grid
// ---------------------------------------------------------------------------
Texture2D AssetMapper::GetTexture(const char* alias) {
    // Check cache first
    auto it = m_cache.find(alias);
    if (it != m_cache.end())
        return it->second;

    // Find entry
    AssetMapEntry* e = Find(alias);
    if (!e) {
        // Unknown alias — return grid texture
        Texture2D grid = MakeGridTexture();
        m_cache[alias] = grid;
        return grid;
    }

    // If texture already loaded on entry, return it
    if (e->texture.id > 0)
        return e->texture;

    // Try loading the path
    Texture2D tex = {0};
    if (IsPathFile(e->path)) {
        tex = LoadTexture(e->path);
        if (tex.id > 0)
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    }

    // Fallback to grid
    if (tex.id == 0) {
        OZ_WARN("AssetMapper: missing texture '%s' at '%s' — using grid", alias, e->path);
        tex = MakeGridTexture();
    }

    e->texture = tex;
    m_cache[alias] = tex;
    return tex;
}

// ---------------------------------------------------------------------------
// PreloadCategory — eagerly load every texture in a category
// ---------------------------------------------------------------------------
void AssetMapper::PreloadCategory(const char* category) {
    for (auto& e : m_entries) {
        if (category && strcmp(e.category, category) == 0)
            GetTexture(e.alias);
    }
}

// ---------------------------------------------------------------------------
// UnloadCategory
// ---------------------------------------------------------------------------
void AssetMapper::UnloadCategory(const char* category) {
    for (auto& e : m_entries) {
        if (category && strcmp(e.category, category) == 0) {
            if (e.texture.id > 0) {
                UnloadTexture(e.texture);
                e.texture = {0};
            }
        }
    }
    // Remove from cache
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        bool inCat = false;
        for (auto& e : m_entries) {
            if (category && strcmp(e.category, category) == 0 && iequals(e.alias, it->first.c_str())) {
                inCat = true;
                break;
            }
        }
        if (inCat) it = m_cache.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// UnloadAll
// ---------------------------------------------------------------------------
void AssetMapper::UnloadAll() {
    for (auto& e : m_entries) {
        if (e.texture.id > 0) {
            UnloadTexture(e.texture);
            e.texture = {0};
        }
    }
    m_cache.clear();
}
