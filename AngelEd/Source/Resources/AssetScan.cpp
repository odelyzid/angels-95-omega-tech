// ============================================================================
// Resources/AssetScan.cpp
//
// See AssetScan.hpp. The only IO in the Resources/ layer besides PackageIO, and the
// only part that cannot be unit-tested headlessly - it reads the filesystem and the
// package loader. The policy it applies (sort + dedupe) lives in AssetScope.cpp
// precisely because that part CAN be tested.
// ============================================================================
#include "AssetScan.hpp"

#include "../../../Source/Package/PackageAssetLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string LowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    return s;
}

bool HasExt(const std::string& loweredExt, const std::vector<std::string>& exts) {
    for (const auto& e : exts) if (loweredExt == e) return true;
    return false;
}

} // namespace

std::vector<AssetScopeItem> ScanAssets(const std::string& subdir,
                                       const std::vector<std::string>& exts) {
    std::vector<AssetScopeItem> out;

    // --- loose files under GameData/ ---
    fs::path base = fs::current_path() / "GameData";
    if (!subdir.empty()) base /= subdir;
    try {
        if (fs::exists(base)) {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (!entry.is_regular_file()) continue;
                if (!HasExt(LowerAscii(entry.path().extension().string()), exts)) continue;
                out.push_back({ entry.path().stem().string(),
                                entry.path().string(),
                                false });
            }
        }
    } catch (const std::exception& e) {
        // A browser listing an empty or unreadable GameData/ is not a reason to take
        // the editor down, and neither original scanner let it.
        fprintf(stderr, "WARN: Exception during file scan: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "WARN: Unknown exception during file scan\n");
    }

    // --- entries inside loaded packages ---
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        // Guard the dot: std::string::npos + substr throws std::out_of_range, and the
        // original code did exactly this on a package key with no extension.
        const size_t keyDot = pkgPath.rfind('.');
        if (keyDot == std::string::npos) continue;
        if (!HasExt(LowerAscii(pkgPath.substr(keyDot)), exts)) continue;

        // Display name is the key's basename minus its extension. PackageAssetLoader
        // stores keys with '/' separators, but tolerate '\' so a hand-built key from a
        // tool does not become one enormous display name.
        std::string name = pkgPath;
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name = name.substr(0, dot);

        out.push_back({ name, pkgPath, true });
    }

    DedupeAssetItemsByName(out);
    return out;
}