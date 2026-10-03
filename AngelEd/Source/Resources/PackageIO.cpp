// ============================================================================
// Resources/PackageIO.cpp
//
// See PackageIO.hpp. Verified as a pure move from UI/Panels/TexturePanel.cpp, with
// `static` dropped from the two entry points so ModelPanel can reach them.
// ============================================================================
#include "PackageIO.hpp"

#include "../../../Source/Package/PackageAssetLoader.hpp"
#include "../../../Source/Package/OzPackage.hpp"
// Pack `add` (entry name -> bytes) into `pkgPath`, preserving whatever the
// package already held. Returns false and fills `err` on failure.
bool PackIntoPackage(const fs::path& pkgPath, uint32_t magic,
                            const std::vector<std::pair<std::string, std::vector<uint8_t>>>& add,
                            std::string& err) {
    std::error_code ec;
    fs::create_directories(pkgPath.parent_path(), ec);

    OzPackageWriter writer(magic);

    // Carry forward the existing entries.
    if (fs::exists(pkgPath)) {
        OzPackageReader reader;
        if (!reader.Open(pkgPath.string().c_str())) {
            err = "existing package could not be read (corrupt?): " + pkgPath.string();
            return false;
        }
        std::vector<std::string> names;
        reader.List(names);
        for (const auto& nm : names) {
            std::vector<uint8_t> data;
            if (reader.Read(nm.c_str(), data) > 0 && !data.empty())
                writer.AddFile(nm.c_str(), data.data(), data.size());
        }
    }

    for (const auto& kv : add) {
        if (kv.second.empty()) continue;
        writer.AddFile(kv.first.c_str(), kv.second.data(), kv.second.size());
    }

    if (!writer.WriteToFile(pkgPath.string().c_str())) {
        err = "failed to write " + pkgPath.string();
        return false;
    }
    return true;
}

// Hot-load a freshly written package so its entries resolve this session.
bool HotLoadPackage(const fs::path& pkgPath, std::string& err) {
    if (!PackageAssetLoader::Instance().LoadPackageFile(pkgPath.string().c_str())) {
        err = "package written but failed to load: " + pkgPath.string();
        return false;
    }
    return true;
}
