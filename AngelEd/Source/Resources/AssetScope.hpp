// ============================================================================
// Resources/AssetScope.hpp
//
// The two-root asset tree shared by the Model Browser and the Texture Manager.
//
// WHY THIS LAYER EXISTS (R6). BuildAssetScope was declared in UI/UiPanels.hpp and
// IMPLEMENTED inside UI/Panels/TexturePanel.cpp - yet ModelPanel.cpp calls it. It
// worked only because every panel is #included into one translation unit; the moment
// the UI layer became real objects, ModelPanel would have stopped compiling. Same
// shape as LevelMetadata living in UI/Panels/LevelState.cpp. AGENTS.md already
// claimed these "cannot drift" because they are implemented once - true in effect,
// structurally fragile in fact.
//
// This header is deliberately dependency-free: <string> and <vector> only. No Win32,
// no raylib, no filesystem, no engine headers. That is what lets
// tests/AssetScope.test.cpp exercise it headlessly - the first automated check the
// asset browser has ever had, covering the invariant AGENTS.md states in prose:
//   "A .oz* file is the ONLY thing treated as a package; anything reachable on disk
//    is a real file even when a package holds a same-named copy."
// ============================================================================
#ifndef ANGEL_ED_RESOURCES_ASSETSCOPE_HPP
#define ANGEL_ED_RESOURCES_ASSETSCOPE_HPP

#include <string>
#include <vector>

// One asset, as presented by a browser: either a loose file under GameData/ or an
// entry that exists only inside a .oz* package.
//
// `path` is the on-disk path for a loose file, or the package key for a packaged
// one. The two are told apart by `fromPackage`, never by sniffing the extension -
// a package file that exists ON DISK is still a real file.
struct AssetScopeItem {
    std::string name;        // display/file stem
    std::string path;        // on-disk path, or package key
    bool        fromPackage = false;
};

// A node in the two-root tree.
//
// entryIndex >= 0 marks a LEAF and is the index into the caller's items vector;
// folders carry -1. Panels store this in the treeview lParam, which is why it must
// be a plain int and why leaves and folders cannot share a value space.
struct AssetScopeNode {
    std::string label;       // display text for this node
    int         entryIndex = -1;              // >= 0 => leaf into the caller's vector
    std::vector<AssetScopeNode> children;
};

// Group `items` into the (GameData)/(Packages) tree.
//
//   root
//     (GameData)   loose files, nested by their real folder under GameData/
//     (Packages)   assets that exist only inside a .oz* package
//
// `search` is a case-insensitive substring filter on name AND path; when non-empty
// only matching leaves survive and empty folders are pruned. Roots auto-expand when
// filtering so matches are visible without clicking.
//
// Children are sorted folders-first, then alphabetically, case-insensitively.
AssetScopeNode BuildAssetScope(const std::vector<AssetScopeItem>& items,
                               const std::string& search);

// Order by display name and drop same-named duplicates, KEEPING THE REAL FILE.
//
// This is the enumeration-side half of the invariant AGENTS.md states: a package
// holding a same-named copy does not make the on-disk file disappear. Both asset
// browsers did this independently, each with the same three lines, and each deciding
// "is this a package?" by calling IsPathFile() on the path - re-deriving something the
// scanner already knew, via a filesystem hit test that a package key could in
// principle satisfy. `fromPackage` is set by the scanner and needs no guesswork.
//
// Sorted by name, so the result is stable and two browsers listing the same asset
// agree on its position.
void DedupeAssetItemsByName(std::vector<AssetScopeItem>& items);

#endif // ANGEL_ED_RESOURCES_ASSETSCOPE_HPP