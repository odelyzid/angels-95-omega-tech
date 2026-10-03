// ============================================================================
// Resources/PackageIO.hpp
//
// Append-to-package and hot-load, shared by both import paths.
//
// WHY THIS LAYER EXISTS (R6). PackIntoPackage and HotLoadPackage were `static` in
// UI/Panels/TexturePanel.cpp, yet UI/Panels/ModelPanel.cpp calls both - the texture
// import and the model import share one implementation, which is the intent stated in
// AGENTS.md. They were only reachable from ModelPanel because the UI layer is one
// translation unit. Same defect as BuildAssetScope (see AssetScope.hpp).
//
// Both entry points are Win32-free: they need the OzPackage reader/writer and
// PackageAssetLoader, both of which are engine code. No HWND, no GDI.
// ============================================================================
#ifndef ANGEL_ED_RESOURCES_PACKAGEIO_HPP
#define ANGEL_ED_RESOURCES_PACKAGEIO_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// One entry to be appended: name -> bytes.
using PackageEntryList = std::vector<std::pair<std::string, std::vector<uint8_t>>>;

// Append every entry in `add` to the package at `pkgPath`, PRESERVING whatever the
// package already held (existing entries are read back with OzPackageReader::Read and
// rewritten). Returns false and fills `err` on failure.
//
// This is deliberately an append and not a create: model imports target
// System/Data/imported_models.ozpak and texture imports
// System/Data/imported_textures.oztex, both of which accumulate across sessions.
bool PackIntoPackage(const fs::path& pkgPath, uint32_t magic,
                     const PackageEntryList& add,
                     std::string& err);

// Load `pkgPath` into the running PackageAssetLoader so its entries resolve THIS
// session (and appear under the (Packages) root in the browsers). Returns false and
// fills `err` if the file was written but then failed to load - a corrupt package
// should not look like a successful import.
bool HotLoadPackage(const fs::path& pkgPath, std::string& err);

#endif // ANGEL_ED_RESOURCES_PACKAGEIO_HPP