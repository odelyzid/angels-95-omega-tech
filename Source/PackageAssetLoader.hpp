#pragma once
#include "raylib.h"
#include "OzPackage.hpp"
#include "Log.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

class PackageAssetLoader {
public:
    static PackageAssetLoader& Instance() {
        static PackageAssetLoader instance;
        return instance;
    }

    void Init() {
        m_readers.clear();
        m_modelCache.clear();
        fs::path dataDir = fs::current_path() / "System" / "Data";
        if (!fs::exists(dataDir)) return;

        for (auto& entry : fs::directory_iterator(dataDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext.size() >= 3 && ext.substr(0, 3) == ".oz") {
                    OzPackageReader reader;
                    if (reader.Open(entry.path().string().c_str())) {
                        m_readers.push_back(std::move(reader));
                        OZ_INFO("PackageLoader: loaded %s (%u entries)",
                                entry.path().filename().string().c_str(),
                                m_readers.back().EntryCount());
                    }
                }
            }
        }
    }

    const uint8_t* Find(const char* filename, size_t& outSize) {
        outSize = 0;
        for (auto it = m_readers.rbegin(); it != m_readers.rend(); ++it) {
            const uint8_t* data = it->GetData(filename, outSize);
            if (data) return data;
        }
        return nullptr;
    }

    const uint8_t* FindBasename(const char* basename, size_t& outSize) {
        outSize = 0;
        for (auto it = m_readers.rbegin(); it != m_readers.rend(); ++it) {
            const OzPackageEntry* e = it->FindBasename(basename);
            if (e) {
                outSize = (size_t)e->sizeRaw;
                return (const uint8_t*)(it->GetData(e->filename, outSize));
            }
        }
        return nullptr;
    }

    const uint8_t* ResolvePackageKey(const char* path, size_t& outSize) {
        outSize = 0;
        std::string normalized;
        const char* pkgPath = path;
        if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
        normalized = pkgPath;
        for (auto& c : normalized) if (c == '\\') c = '/';

        const uint8_t* data = Find(normalized.c_str(), outSize);
        if (data) return data;

        const char* slash = normalized.rfind('/') != std::string::npos
            ? &normalized[normalized.rfind('/') + 1] : normalized.c_str();
        data = FindBasename(slash, outSize);
        if (data) return data;

        size_t firstSlash = normalized.find('/');
        if (firstSlash != std::string::npos) {
            std::string stripped = normalized.substr(firstSlash + 1);
            data = Find(stripped.c_str(), outSize);
            if (data) return data;
        }

        return nullptr;
    }

    bool ReadInto(const char* filename, std::vector<uint8_t>& out) {
        size_t sz;
        const uint8_t* data = ResolvePackageKey(filename, sz);
        if (!data) return false;
        out.assign(data, data + sz);
        return true;
    }

    std::string CacheModelFile(const char* path) {
        std::string normalized;
        const char* pkgPath = path;
        if (strncmp(path, "GameData/", 9) == 0) pkgPath = path + 9;
        normalized = pkgPath;
        for (auto& c : normalized) if (c == '\\') c = '/';

        auto it = m_modelCache.find(normalized);
        if (it != m_modelCache.end()) return it->second;

        size_t sz;
        const uint8_t* data = ResolvePackageKey(path, sz);
        if (!data) return "";

        fs::path cacheDir = fs::current_path() / "System" / "Cache";
        fs::create_directories(cacheDir);

        std::string cacheName = normalized;
        for (auto& c : cacheName) if (c == '/') c = '_';
        fs::path cachePath = cacheDir / cacheName;

        std::ofstream ofs(cachePath, std::ios::binary);
        if (!ofs) return "";
        ofs.write((const char*)data, sz);
        ofs.close();

        m_modelCache[normalized] = cachePath.string();
        return cachePath.string();
    }

    int PackageCount() const { return (int)m_readers.size(); }

    // List all filenames across all loaded packages
    void ListAllFiles(std::vector<std::string>& names) const {
        names.clear();
        for (const auto& reader : m_readers) {
            for (const auto& e : reader.Entries()) {
                names.push_back(e.filename);
            }
        }
    }

private:
    std::vector<OzPackageReader> m_readers;
    std::unordered_map<std::string, std::string> m_modelCache;
};

inline Texture2D LoadTextureWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadTexture(path);

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        std::string normalized = path;
        for (auto& c : normalized) if (c == '\\') c = '/';
        std::string ext = normalized.substr(normalized.rfind('.'));
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

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        std::string normalized = path;
        for (auto& c : normalized) if (c == '\\') c = '/';
        std::string ext = normalized.substr(normalized.rfind('.') + 1);
        Wave w = LoadWaveFromMemory(ext.c_str(), data, (int)sz);
        Sound s = LoadSoundFromWave(w);
        UnloadWave(w);
        return s;
    }
    return Sound{0};
}

inline Music LoadMusicWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadMusicStream(path);

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        std::string normalized = path;
        for (auto& c : normalized) if (c == '\\') c = '/';
        std::string ext = normalized.substr(normalized.rfind('.') + 1);
        return LoadMusicStreamFromMemory(ext.c_str(), data, (int)sz);
    }
    return Music{0};
}

inline Model LoadModelWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadModel(path);

    std::string cachePath = PackageAssetLoader::Instance().CacheModelFile(path);
    if (!cachePath.empty() && IsPathFile(cachePath.c_str()))
        return LoadModel(cachePath.c_str());

    return Model{0};
}

inline Font LoadFontWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadFont(path);

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        std::string normalized = path;
        for (auto& c : normalized) if (c == '\\') c = '/';
        std::string ext = normalized.substr(normalized.rfind('.') + 1);
        return LoadFontFromMemory(ext.c_str(), data, (int)sz, 32, 0, 0);
    }
    return Font{0};
}

inline std::string LoadFileTextWithFallback(const char* path) {
    if (IsPathFile(path)) {
        char* text = LoadFileText(path);
        if (text) {
            std::string result(text);
            UnloadFileText(text);
            return result;
        }
    }
    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        return std::string((const char*)data, sz);
    }
    return "";
}

inline Image LoadImageWithFallback(const char* path) {
    if (IsPathFile(path))
        return LoadImage(path);

    size_t sz;
    const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(path, sz);
    if (data) {
        std::string normalized = path;
        for (auto& c : normalized) if (c == '\\') c = '/';
        std::string ext = normalized.substr(normalized.rfind('.') + 1);
        return LoadImageFromMemory(ext.c_str(), data, (int)sz);
    }
    return Image{0};
}

inline Shader LoadShaderWithFallback(const char* vsPath, const char* fsPath) {
    std::string vsCode, fsCode;
    bool vsFromPkg = false, fsFromPkg = false;

    if (vsPath && IsPathFile(vsPath)) {
        char* text = LoadFileText(vsPath);
        if (text) { vsCode = text; UnloadFileText(text); }
    } else if (vsPath) {
        size_t sz;
        const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(vsPath, sz);
        if (data) { vsCode.assign((const char*)data, sz); vsFromPkg = true; }
    }

    if (fsPath && IsPathFile(fsPath)) {
        char* text = LoadFileText(fsPath);
        if (text) { fsCode = text; UnloadFileText(text); }
    } else if (fsPath) {
        size_t sz;
        const uint8_t* data = PackageAssetLoader::Instance().ResolvePackageKey(fsPath, sz);
        if (data) { fsCode.assign((const char*)data, sz); fsFromPkg = true; }
    }

    if (!vsCode.empty() || !fsCode.empty()) {
        const char* vs = vsCode.empty() ? nullptr : vsCode.c_str();
        const char* fs = fsCode.empty() ? nullptr : fsCode.c_str();
        return LoadShaderFromMemory(vs, fs);
    }

    return LoadShader(vsPath, fsPath);
}
