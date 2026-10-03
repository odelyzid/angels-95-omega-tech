// ============================================================================
// Resources/AssetScan.hpp
//
// Enumerate the assets a browser can offer: everything under GameData/ on disk plus
// every entry inside a loaded .oz* package.
//
// WHY A SHARED PRIMITIVE (R7). Two browsers scanned for models and four call sites
// scanned for sounds, and the model scan and ScanFilesAndPackages were the SAME
// twenty lines written twice:
//
//   - walk GameData[/subdir] recursively, keep matching extensions, name = stem
//   - walk PackageAssetLoader::ListAllFiles(), keep matching extensions, name = key stem
//   - sort by name, dedupe by name, preferring a real file over a package copy
//
// ScanAssets is that once. The two adapters that remain (ScanFilesAndPackages in
// UI/UiCommon.hpp and ScanModelBrowserFiles in UI/Panels/ModelPanel.cpp) only map the
// result into their own entry type and do their own state handling.
//
// The dedup/sort policy is NOT here - it is DedupeAssetItemsByName in AssetScope.hpp,
// because it is pure and therefore testable headlessly. This file is the IO shell
// around it: filesystem and package loader only.
//
// A real bug fixed on the way: the package half of both old scans did
// `pkgPath.substr(pkgPath.rfind('.'))`, and std::string::npos + substr throws. A
// package entry with no extension in its key would have propagated an exception out of
// a WM_INITDIALOG. Guarded here.
// ============================================================================
#ifndef ANGEL_ED_RESOURCES_ASSETSCOPE_SCAN_HPP
#define ANGEL_ED_RESOURCES_ASSETSCOPE_SCAN_HPP

#include "AssetScope.hpp"

#include <string>
#include <vector>

// Enumerate loose files under GameData/ (optionally restricted to `subdir`) and every
// entry in every loaded package, keeping only those whose extension is in `exts`.
//
// `exts` must be lowercase and include the dot, e.g. { ".wav", ".ogg" } - the result is
// compared case-insensitively against the file's own extension, but the caller's list
// is used verbatim.
//
// Result fields:
//   name        display stem, and the dedup key
//   path        absolute path for a loose file, or the package KEY for a packaged one
//   fromPackage set by the scanner, never inferred from the path
//
// Sorted by name with same-named duplicates resolved in favour of the real file. See
// DedupeAssetItemsByName.
std::vector<AssetScopeItem> ScanAssets(const std::string& subdir,
                                       const std::vector<std::string>& exts);

#endif // ANGEL_ED_RESOURCES_ASSETSCOPE_SCAN_HPP