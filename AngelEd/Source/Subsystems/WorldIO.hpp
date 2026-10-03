// ============================================================================
// Subsystems/WorldIO.hpp
//
// Reading a world document into the editor.
//
// A REAL translation unit as of Phase F.
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_WORLDIO_HPP
#define ANGEL_ED_SUBSYSTEMS_WORLDIO_HPP

#include <filesystem>

// ClearScene() - drop everything currently loaded - lives in Core/EditorShell.cpp, not
// here, and is called by WorldIO, History and Main.

// Load `path` as a world document. Returns false if it could not be read.
bool LoadWorldDocument(const std::filesystem::path& path);

#endif // ANGEL_ED_SUBSYSTEMS_WORLDIO_HPP