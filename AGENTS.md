# AGENTS.md - Angels95 / OmegaTech Engine

## Build

### Linux / macOS
```bash
make OTENGINE          # game client -> Angels95
make AngelServ         # dedicated server (no raylib dep)
make ozpack            # asset packer tool
make -j$(nproc)        # all targets
```
- `build.sh` handles raylib 5.5 install + make on Linux.
- raylib must be installed system-wide (`/usr/local/lib/libraylib.a`). Not vendored.

### Windows
- **w64devkit:** `.\build-native-win.ps1` (requires `C:\raylib\w64devkit` GCC 15.2.0). Do NOT use WinGet GCC 16.1.0.
- **MSYS2/MINGW64:** `.\build.ps1` (uses `mingw-w64-x86_64-raylib`).
- Flags: `-SkipData` skips asset packaging, `-SkipClean` skips `make clean`.
- Both assemble `System/` with all EXEs, INIs, run scripts, and packaged assets.
- `build-data.ps1` uses `OzPack.exe` to create `.oz*` packages from `GameData/`.

### Targets
| Target | Build cmd | Dependencies |
|---|---|---|
| `Angels95` (client) | `make OTENGINE` | raylib 5.5 |
| `AngelServ` (server) | `make AngelServ` | None (standalone, raw sockets) |
| `AngelEd` (editor) | `make -C AngelEd` | raylib 5.5 + Win32 |
| `OzPack` | `make ozpack` | None (standalone) |

## Makefile structure (root)
- **Game objects:** raygui.o, OTCustom.o, Encoder.o, Main.o, Network.o, Log.o, Client.o, OzAssetMapper.o, OzSoundLoader.o, OzPawnSystem.o, OzOzoneLoader.o, OzoneParser.o, OzBsp.o, WorldChunk.o, LightningScriptContext.o, LightningScriptParser.o, LightningEntityRegistry.o, LightningEntityManager.o, LitLightning.o, rlights.o
- **AngelServ objects:** Network.o, GameState.o, Log.o, OzBsp.o, WorldChunk.o (+ Server.cpp, WDLParser.cpp, OzoneParser.cpp)
- Flags: `-O3 --std=c++20`, single g++ invocation per target, no cmake.

## AngelEd Makefile (`AngelEd/Makefile`)
- Separate Makefile with its own `AngelEd.o`, `Win32Dialogs.o`, `EditorIcons.o`, `PPGIO.o` objects.
- Links raylib, links against Angels95 build objects for shared modules.
- Win32-only (native panels + raylib viewport).

## Tests
```bash
make test              # runs test_parser + test_entity_manager
make test_parser       # LightningScriptParser tests
make test_context      # LightningScriptContext tests
make test_registry     # LightningEntityRegistry tests
make test_entity_manager # LightningEntityManager tests
```
- No test framework - standalone `.test.cpp` files in `tests/` compiled directly, run as executables.
- No raylib dependency (use `SERVER_CXX` compiler, `-DOMEGA_TEST_ENV` flag).

## Entrypoints
- **Client:** `Source/Main.cpp` - `main()` after OmegaTechInit, splash, home screen, world loading, game loop.
- **Server:** `Source/Server/Server.cpp` - `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080), `--dir` (GameData).
- **Editor:** `AngelEd/Source/Main.cpp` - `main(argc, argv)`. Win32 panels + raylib viewport.
- **Core engine:** `Source/Core.hpp` (~1200 lines, single header) - init, menu, world loading, render loop, shaders.

## Source tree (Source/)
- `Source/Main.cpp` - client entrypoint, game loop, rendering
- `Source/Core.hpp` - engine init, splash, menu, world loading
- `Source/Server/Server.cpp` - dedicated server, HTTP API on :8080
- `Source/Server/WDLParser.cpp`/`.hpp` - WDL world format parser (standalone, no raylib)
- `Source/Server/OzoneParser.cpp`/`.hpp` - OZONE world format parser
- `Source/Server/GameState.cpp`/`.hpp` - server game state management
- `Source/Network/Network.cpp`/`.hpp` - custom UDP protocol, packed structs. LAN discovery on UDP 27100, game port 27015
- `Source/Package/PackageAssetLoader.hpp` - runtime asset loading from `.oz*` packages
- `Source/Package/OzPackage.hpp` - OzPackage format reader/writer
- `Source/Package/OzAssetMapper.cpp`/`.hpp` - engine/item texture mapper
- `Source/Pawn/OzPawnSystem.cpp`/`.hpp` - dynamic NPC system (FSM: IDLE -> PATROL -> CHASE -> RETURN -> DEAD)
- `Source/Pawn/Items.hpp` - item definitions (20 backpack + 8 equipment + 5 weapon slots)
- `Source/Pawn/Entities.hpp` - entity definitions
- `Source/Pawn/Objects.hpp` - world object definitions
- `Source/Pawn/Player.hpp` - player state/capabilities
- `Source/Script/LightningScriptParser.cpp`/`.hpp` - LightningScript parser
- `Source/Script/LightningScriptContext.cpp`/`.hpp` - script execution context
- `Source/Script/LightningEntityRegistry.cpp`/`.hpp` - entity type registry (pickup defs from .ozls)
- `Source/Script/LightningEntityManager.cpp`/`.hpp` - runtime entity management
- `Source/Script/LightningEntityDef.hpp` - entity definition structs
- `Source/Renderer/LitLightning.cpp`/`.hpp` - lighting renderer
- `Source/Renderer/EngineBillboard.hpp` - billboard sprite rendering (pickups, icons)
- `Source/Renderer/TextSystem.hpp` - text rendering system
- `Source/Renderer/Video.hpp` - video playback support
- `Source/Physics/OzBsp.cpp`/`.hpp` - CSG/BSP collision processor
- `Source/Physics/WorldChunk.cpp`/`.hpp` - spatial partition (chunk-based world)
- `Source/Audio/OzSoundLoader.cpp`/`.hpp` - sound loading with fallback
- `Source/Audio/DspReverb.hpp` - audio reverb DSP
- `Source/Client/Client.cpp`/`.hpp` - client networking layer
- `Source/Custom/OTCustom.cpp`/`.hpp` - custom engine code (statically linked)
- `Source/Encoder/Encoder.cpp`/`.hpp` - asset encoder
- `Source/Menu/TitleMenu.hpp` - title/home screen menu
- `Source/Parasite/ParasiteScript.hpp` - Parasite script definitions
- `Source/Parasite/ParasiteScriptData.hpp` - Parasite script data
- `Source/ParticleDemon/ParticleDemon.hpp` - 50-particle array system (explosion/trail/rain)
- `Source/plmpeg/pl_mpeg.h` - MPEG1 video decoder
- `Source/rlights/rlights.cpp`/`.h` - raylib lights helper
- `Source/raygui/` - raygui UI library (raygui.c/.h, dark.h theme)
- `Source/OzOzoneLoader.cpp`/`.hpp` - OZONE world format loader
- `Source/OzPack.cpp` - standalone packer/unpacker CLI tool
- `Source/Data.hpp` - game data structures
- `Source/Settings.hpp` - settings management
- `Source/IniConfig.hpp` - INI config file reader
- `Source/Log.cpp`/`.hpp` - logging system
- `Source/Editor.hpp` - editor integration header (used by AngelEd)
- `Source/PPGIO.hpp` - WDL format I/O helpers (uses `using namespace std`)
- `Source/WindowsCompat.hpp` - must be included early in files touching both raylib and winsock2. Defines `CloseWindow`/`ShowCursor`/`Rectangle`/`DrawText` -> `__WIN32_*`/`GDI_*` before `#include <windows.h>`, then `#undef`s them.

## AngleEd/Source (Editor)
- `AngelEd/Source/Editor.hpp` - main editor state (Camera, ViewMode, Fog, WorldData, cached models)
- `AngelEd/Source/Main.cpp` - full editor loop: raycast selection, gizmo, entity actions, menu bar
- `AngelEd/Source/Win32Dialogs.cpp`/`.hpp` - native Win32 panel windows (SoundMgr, TextureMgr, PawnMgr, ScriptMgr, ModelBrowser, EnvPanel, PickupPanel, NodePanel, HeightmapEditor, LightProps, WorldGraph, PropertiesPanel)
- `AngelEd/Source/EditorIcons.cpp`/`.hpp` - toolbar icon loader (AngelEd/UI/*.bmp)
- `AngelEd/Source/PPGIO.hpp` - WDL I/O (shared with Source/)
- `AngelEd/Source/raygui/` - bundled raygui (dark.h, raygui.c/.h)
- `AngelEd/UI/` - 45 toolbar icon .bmp files
- Entity selection: raycast hit-testing for brushes, models, NPCs, pickups, lights, zones, spawns
- Supports WDL (.wdl) and OZONE (.ozone) world formats
- Model preview render-to-texture for browser dialogs
- CSG processor (`CsgProcessor g_csgProc`) exists but backend never called
- INI config at `System/AngelEd.ini`

## Package system (OzPackage)
- Extensions: `.ozpak` (generic), `.oztex` (textures), `.ozsnd` (sounds), `.ozmux` (music), `.ozone` (worlds)
- Runtime: `PackageAssetLoader::Instance().Init()` scans `System/Data/*.oz*`. Use `*WithFallback` wrappers (filesystem -> package).
- Models use temp-file cache in `System/Cache/` (raylib has no `LoadModelFromMemory`).
- Packaging: `.\build-data.ps1` uses `OzPack.exe` to create packages from `GameData/` subdirectories.

## World format (WDL)
- Colon-delimited plain text. Instructions: `HeightMap`, `Model1`-`Model20`, `Object1`-`Object5`, `Walker` (NPC spawn), `Light`, `ClipBox`, `Collision`, `Script`, `NE1`-`NE3` (noise emitters).
- Worlds live in `GameData/Worlds/<WorldName>/World.wdl` alongside optional subdirectories: `Models/`, `Scripts/`, `Music/`, `NoiseEmitter/`.
- Server scans `GameData/Worlds/` for subdirectories containing `World.wdl` at startup.
- WDL helpers in `Source/PPGIO.hpp`: `LoadFile()`, `GetWDLSize()`, `WSplitValue()`, `WReadValue()`, `ToFloat()`, etc.

## World format (OZONE)
- Newer format supported by OzoneParser/OzoneLoader.
- Primitive-based: box, cylinder, sphere, pyramid, plane with CSG ops (add/sub/intersect).
- Entities: playerstart, pickup, npc, zone, emitter.
- Export from AngelEd: `ExportToOzone()` in `AngelEd/Source/Main.cpp`.

## System/ release layout
- `System/` contains Angels95.exe, AngelServ.exe, AngelEd.exe, OzPack.exe, INIs, run scripts, `Data/*.oz*`, `Cache/`.
- Game requires `GameData/` as a sibling directory for worlds, saves, and loose assets.
- Run scripts (`run.bat`, `run.ps1`) set cwd to repo root then launch the client.
- Server config: `OzServer.ini` (Port, HttpPort, MaxPlayers, WorldDir, ServerName).
- Editor config: `AngelEd.ini`

## Conventions & quirks
- **`#pragma pack(push,1)`** for all network packet structs.
- **Asset paths:** `*WithFallback` wrappers check filesystem first, then packages. Server `--dir` flag overrides world directory.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav` (flags), `POS.sav` (position), `Script.sav` (WDL scripts). All `.sav` files are gitignored.
- **`using namespace std;`** used in `PPGIO.hpp`, `Data.hpp`, `Encoder.hpp`, `TextSystem.hpp`, `ParasiteScriptData.hpp`.
- **Editor:** Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. `Win32Dialogs.cpp` defines `UNICODE`/`_UNICODE` for wide-string Win32 API.
- **Known editor gaps:** Lighting toggle (Lit/Unlit) does not actually unset shader from model materials. No right-click context menu or entity properties. CSG Add/Subtract UI is wired but backend `CsgProcessor` never called. Texture browser grid is implemented (thumbnails in custom control). No test-play functionality.
- **Particle system:** `Core.hpp` includes `ParticleDemon.hpp` - actual 50-particle array implementation with explosion/trail/rain effects. `RainParticles` instance in `EngineData`.
- **Editor panels:** Sound Manager (category tabs: SFX/Music/Ambience, volume slider, loop), Texture Manager (grid browser with thumbnails, package loading, apply to model), Pawn Manager (tree hierarchy, data-driven from config/registry), Script Manager, Model Browser, Zone Properties, Pickup Panel, Node Panel, Heightmap Editor, Light Properties, World Graph Explorer, Properties Panel.

## Pawn system
- Data-driven NPC definitions from `GameData/Global/PawnDefs/*.cfg` (name, speed, aggroRange, attackRange, damage, maxHealth, sprite_path, scream_path).
- Fallback hardcoded defs: Walker, Skaarj, Brute, Floater.
- FSM states: IDLE, PATROL, CHASE, ATTACK, RETURN, DEAD.
- Pickup types from LightningScript entity registry (`.ozls` definitions).

## CI
- **Linux:** Build raylib from source (cached) -> `make AngelServ` -> `make OTENGINE` -> `make ozpack` -> smoke test server with `timeout 3`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` -> build all 4 targets -> assemble System/ -> run `build-data.ps1` -> upload artifact.
- Tags matching `b*` trigger GitHub Release with zipped System/.
