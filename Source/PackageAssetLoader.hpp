#pragma once
#include "OzPackage.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// PackageAssetLoader — global package reader for System/Data/*.oz*
//
// Initialized at startup. Provides fallback asset loading from packages
// before falling back to raw filesystem paths.
// ---------------------------------------------------------------------------

class PackageAssetLoader {
public:
    static PackageAssetLoader& Instance() {
        static PackageAssetLoader instance;
        return instance;
    }

    // Scan and open all .oz* files in System/Data/
    void Init() {
        m_readers.clear();
        fs::path dataDir = fs::current_path() / "System" / "Data";
        if (!fs::exists(dataDir)) return;

        for (auto& entry : fs::directory_iterator(dataDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                // Match .oz* extensions
                if (ext.size() >= 3 && ext.substr(0, 3) == ".oz") {
                    OzPackageReader reader;
                    if (reader.Open(entry.path().string().c_str())) {
                        m_readers.push_back(std::move(reader));
                        fprintf(stderr, "PackageLoader: loaded %s (%u entries)\n",
                                entry.path().filename().string().c_str(),
                                m_readers.back().EntryCount());
                    }
                }
            }
        }
    }

    // Find a file by name in any open package. Returns data pointer + size, or nullptr.
    const uint8_t* Find(const char* filename, size_t& outSize) {
        outSize = 0;
        // Search in reverse order so last-opened packages take priority
        for (auto it = m_readers.rbegin(); it != m_readers.rend(); ++it) {
            const uint8_t* data = it->GetData(filename, outSize);
            if (data) return data;
        }
        return nullptr;
    }

    // Convenience: read into vector
    bool ReadInto(const char* filename, std::vector<uint8_t>& out) {
        size_t sz;
        const uint8_t* data = Find(filename, sz);
        if (!data) return false;
        out.assign(data, data + sz);
        return true;
    }

    int PackageCount() const { return (int)m_readers.size(); }

private:
    std::vector<OzPackageReader> m_readers;
};

// ---------------------------------------------------------------------------
// Inline wrappers for raylib asset loading with package fallback
// ---------------------------------------------------------------------------
// Usage: replace LoadTexture(path) with LoadTextureWithFallback(path).
// If the file exists on disk, it's loaded directly.
// Otherwise, packages are searched.

inline Texture2D LoadTextureWithFallback(const char* path) {
    // Try filesystem first
    if (IsPathFile(path))
        return LoadTexture(path);
    // Try package
    std::string relPath;
    const char* pkgPath = path;
    // Strip "GameData/" prefix if present to get relative path
    if (strncmp(path, "GameData/", 9) == 0)
        pkgPath = path + 9;
    // Normalize backslashes to forward slashes
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        // Determine format from extension
        std::string ext = relPath.substr(relPath.rfind('.'));
        const char* fmt = nullptr;
        if (ext == ".png") fmt = "png";
        else if (ext == ".jpg" || ext == ".jpeg") fmt = "jpg";
        else if (ext == ".bmp") fmt = "bmp";
        else if (ext == ".tga") fmt = "tga";
        if (fmt) {
            Image img = LoadImageFromMemory(fmt, data, (int)sz);
            Texture2D tex = LoadTextureFromImage(img);
            UnloadImage(img);
            return tex;
        }
    }
    return Texture2D{0};
}

inline Sound LoadSoundWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadSound(path);
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        Wave w = LoadWaveFromMemory(relPath.substr(relPath.rfind('.') + 1).c_str(), data, (int)sz);
        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        return s;
    }
    return Sound{0};
}

inline Music LoadMusicWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadMusicStream(path);
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        return LoadMusicStreamFromMemory(relPath.substr(relPath.rfind('.') + 1).c_str(), data, (int)sz);
    }
    return Music{0};
}

inline Model LoadModelWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadModel(path);
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        return LoadModelFromMemory(relPath.substr(relPath.rfind('.') + 1).c_str(), data, (int)sz);
    }
    return Model{0};
}

inline Font LoadFontWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadFont(path);
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        return LoadFontFromMemory(relPath.substr(relPath.rfind('.') + 1).c_str(), data, (int)sz, 32, 0, 0);
    }
    return Font{0};
}

// Wrapper for loading raw file content (e.g., WDL scripts)
inline std::string LoadFileTextWithFallback(const char* path) {
    if (IsPathFile(path)) {
        char* text = LoadFileText(path);
        if (text) {
            std::string result(text);
            UnloadFileText(text);
            return result;
        }
    }
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        return std::string((const char*)data, sz);
    }
    return "";
}

// Wrapper for image loading
inline Image LoadImageWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadImage(path);
    std::string relPath;
    const char* pkgPath = path;
    if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
    relPath = pkgPath;
    for (auto& c : relPath) if (c == '\\') c = '/';
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().Find(relPath.c_str(), sz);
    if (data) {
        std::string ext = relPath.substr(relPath.rfind('.') + 1);
        return LoadImageFromMemory(ext.c_str(), data, (int)sz);
    }
    return Image{0};
}
