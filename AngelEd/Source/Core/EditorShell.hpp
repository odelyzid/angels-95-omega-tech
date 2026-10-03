// =============================================================================
// Core/EditorShell.hpp
//
// Preamble: includes, shared editor globals, local types, forward declarations.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

#include "../../../Source/WindowsCompat.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../Editor.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include "../UI/UiPanels.hpp"
#include "../EditorIcons.hpp"
#include "../../../Source/IniConfig.hpp"
#include "../../../Source/World/OzOzoneLoader.hpp"
#include "../../../Source/Pawn/OzPawnSystem.hpp"
#include "../../../Source/Package/Anim/OzAnimFormat.hpp"
#include "../../../Source/Package/PackageAssetLoader.hpp"
#include "../../../Source/Script/LightningEntityRegistry.hpp"
#include "../../../Source/Script/OzlsWriter.hpp"
#include "../../../Source/Audio/SoundManager.hpp"
#include "../../../Source/Physics/OzBsp.hpp"
#include "../../../Source/Renderer/LitLightning.hpp"
#ifdef _WIN32
#include <GL/gl.h>
#endif
#include <algorithm>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

// The state that used to live here is now in Core/EditorState.hpp. It is included
// from Main.cpp rather than from here, so this file stays a pure include list - the
// same relationship UI/UiCommon.hpp has with its own preamble.
