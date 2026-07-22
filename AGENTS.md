# AGENTS.md — Angels95 / OmegaTech Engine

## Build

### Linux / macOS
```bash
make OTENGINE          # game client → Angels95
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

### Targets
| Target | Build cmd | Dependencies |
|---|---|---|
| `Angels95` | `make OTENGINE` | raylib 5.5 |
| `AngelServ` | `make AngelServ` | None (standalone, raw sockets) |
| `AngelEd` (Win32 only) | See `build-native-win.ps1` / CI for exact g++ flags | raylib 5.5 + Win32 |
| `OzPack` | `make ozpack` | None (standalone) |

## Tests
```bash
make test              # runs test_parser only
make test_parser       # LightningScriptParser tests
make test_context      # LightningScriptContext tests
make test_registry     # LightningEntityRegistry tests
```
- No test framework — standalone `.test.cpp` files compiled directly, run as executables.
- No raylib dependency (use `SERVER_CXX` compiler).

## Entrypoints
- **Client:** `Source/Main.cpp` — `main()` after OmegaTechInit, splash, home screen, world loading, game loop.
- **Server:** `Source/Server/Server.cpp` — `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080), `--dir` (GameData).
- **Editor:** `AngelEd/Source/Main.cpp` — `main(argc, argv)`. Win32 panels + raylib viewport.
- **Core engine:** `Source/Core.hpp` (~1200 lines, single header) — init, menu, world loading, render loop, shaders.

## Source tree essentials
- `Source/Main.cpp` — client entrypoint, game loop, rendering
- `Source/Core.hpp` — engine init, splash, menu, world loading
- `Source/Server/Server.cpp` — dedicated server, HTTP API on :8080
- `Source/Server/WDLParser.hpp` — WDL world format parser (standalone, no raylib)
- `Source/Network/Network.hpp` — custom UDP protocol, packed structs. LAN discovery on UDP 27100, game port 27015
- `Source/Package/PackageAssetLoader.hpp` — runtime asset loading from `.oz*` packages
- `Source/Pawn/OzPawnSystem.hpp` — dynamic NPC system (FSM: IDLE→PATROL→CHASE→RETURN→DEAD)
- `Source/Script/LightningScript*.hpp` — LightningScript scripting system
- `Source/Items.hpp` — item definitions (20 backpack + 8 equipment + 5 weapon slots)
- `Source/WindowsCompat.hpp` — must be included early in files touching both raylib and winsock2. Defines `CloseWindow`/`ShowCursor`/`Rectangle`/`DrawText` → `__WIN32_*`/`GDI_*` before `#include <windows.h>`, then `#undef`s them.

## Package system (OzPackage)
- Extensions: `.ozpak` (generic), `.oztex` (textures), `.ozsnd` (sounds), `.ozmux` (music), `.ozone` (worlds)
- Runtime: `PackageAssetLoader::Instance().Init()` scans `System/Data/*.oz*`. Use `*WithFallback` wrappers (filesystem → package).
- Models use temp-file cache in `System/Cache/` (raylib has no `LoadModelFromMemory`).
- Packaging: `.\build-data.ps1` uses `OzPack.exe` to create packages from `GameData/` subdirectories.

## World format (WDL)
- Colon-delimited plain text. Instructions: `HeightMap`, `Model1`–`Model20`, `Object1`–`Object5`, `Walker` (NPC spawn), `Light`, `ClipBox`, `Collision`, `Script`, `NE1`–`NE3` (noise emitters).
- Worlds live in `GameData/Worlds/<WorldName>/World.wdl` alongside optional subdirectories: `Models/`, `Scripts/`, `Music/`, `NoiseEmitter/`.
- Server scans `GameData/Worlds/` for subdirectories containing `World.wdl` at startup.

## System/ release layout
- `System/` contains Angels95.exe, AngelServ.exe, AngelEd.exe, OzPack.exe, INIs, run scripts, `Data/*.oz*`, `Cache/`.
- Game requires `GameData/` as a sibling directory for worlds, saves, and loose assets.
- Run scripts (`run.bat`, `run.ps1`) set cwd to repo root then launch the client.
- Server config: `OzServer.ini` (Port, HttpPort, MaxPlayers, WorldDir, ServerName).

## Conventions & quirks
- **Single g++ invocation per target, no cmake.** Flags: `-O3 --std=c++20`.
- **`#pragma pack(push,1)`** for all network packet structs.
- **Asset paths:** `*WithFallback` wrappers check filesystem first, then packages. Server `--dir` flag overrides world directory.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav` (flags), `POS.sav` (position), `Script.sav` (WDL scripts). All `.sav` files are gitignored.
- **`using namespace std;`** used in `PPGIO.hpp`, `Data.hpp`, `Encoder.hpp`, `TextSystem.hpp`, `ParasiteScriptData.hpp`.
- **Editor:** Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. `Win32Dialogs.cpp` defines `UNICODE`/`_UNICODE` for wide-string Win32 API.
- **Known editor gaps:** Lighting toggle (Lit/Unlit) does not actually unset shader from model materials. No right-click context menu or entity properties. CSG Add/Subtract UI is wired but backend CsgProcessor never called. Texture browser uses plain listbox (no tile thumbnails). No test-play functionality.
- **Particle system:** `Core.hpp` includes `ParticleDemon.hpp` — actual 50-particle array implementation with explosion/trail/rain effects. `RainParticles` instance in `EngineData`.

## CI
- **Linux:** Build raylib from source (cached) → `make AngelServ` → `make OTENGINE` → `make ozpack` → smoke test server with `timeout 3`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` → build all 4 targets → assemble System/ → run `build-data.ps1` → upload artifact.
- Tags matching `b*` trigger GitHub Release with zipped System/.
