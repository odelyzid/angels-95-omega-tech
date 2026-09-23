# AGENTS.md - Angels95 / OmegaTech Engine

PS1-styled multiplayer game on raylib 5.5 with a custom WDL/OZONE world format and dedicated server. C++20, single `g++` link per target via plain Makefiles, no cmake. Full docs live in `Wiki/` (Engine-Overview, Building, Editor-Usage, LightningScript, World-Format-WDL).

## Build

### Linux / macOS
```bash
make OTENGINE          # client -> Angels95
make AngelServ         # server, no raylib dep
make ozpack            # asset packer
make -j$(nproc)        # all three
```
- raylib 5.5 must be installed system-wide (`/usr/local/lib/libraylib.a`), not vendored. `build.sh` installs it from source.

### Windows
- **w64devkit:** `.\build-native-win.ps1` (requires `C:\raylib\w64devkit`, GCC 15.2.0). Do NOT use WinGet GCC 16.1.0 - broken POSIX/UCRT headers.
- **MSYS2/MINGW64:** `.\build.ps1` (uses `mingw-w64-x86_64-raylib`; auto-builds raylib 5.5 to `~/raylib-5.5` if missing).
- Both build all 4 targets and assemble `System/`. Flags: `-SkipData` (skip asset packaging), `-SkipClean`.
- `build-data.ps1` drives `OzPack.exe` to create `.oz*` packages from `GameData/` subdirectories.

### Targets
| Target | Build cmd | Dependencies |
|---|---|---|
| `Angels95` (client) | `make OTENGINE` | raylib 5.5 |
| `AngelServ` (server) | `make AngelServ` | None (standalone, raw sockets) |
| `AngelEd` (editor) | `make -C AngelEd` | raylib 5.5 + Win32 (Makefile errors out on Linux) |
| `OzPack` | `make ozpack` | None |

## Makefile notes (root)
- Flags: `-O3 --std=c++20`, one g++ invocation per target. Objects go to `build/`.
- `Source/raygui/raygui.c` needs `-fpermissive -DRAYGUI_IMPLEMENTATION` (not plain C++).
- `windres` embeds `.rc` icons; server/client Windows builds need it on PATH.
- `clean` also removes the test executables sitting in the repo root.

## Tests
```bash
make test   # builds + runs ALL suites (continues past failures)
```
Suites: `test_parser`, `test_context`, `test_registry`, `test_entity_manager`, `test_pawn_system`, `test_wdl_parser`, `test_ozone_parser`, `test_network`, `test_game_state`.

- No test framework - standalone `tests/*.test.cpp` compiled directly. **Test executables land in repo root** (`./test_parser`, not `./tests/`).
- Most suites use `SERVER_CXX` + `-DOMEGA_TEST_ENV` (no raylib). **Exceptions:** `test_entity_manager` and `test_pawn_system` link raylib (Vector3/BoundingBox types).
- Single test targets: `make test_parser` / `make test_context` / `make test_registry` / `make test_wdl_parser` / `make test_ozone_parser` / `make test_network` / `make test_game_state` etc.

## Runtime config quirk (verified)
- `System/Angels95.ini` and `System/OzServer.ini` are **templates written by build scripts - never read at runtime**. The client and server accept no INI config. Server behavior is controlled entirely by CLI flags (`--port`, `--http-port`, `--dir`). Only `AngelEd` reads its INI (`g_config.Load("System/AngelEd.ini")`). To change defaults, edit the code, not the INI.

## Entrypoints
- **Client:** `Source/Main.cpp` - `main()` after OmegaTechInit, splash, home screen, world loading, game loop. Flags: `--world <name>`, `--world-dir <path>`.
- **Server:** `Source/Server/Server.cpp` - `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080, HTTP map API), `--dir` (GameData). LAN discovery UDP 27100.
- **Editor:** `AngelEd/Source/Main.cpp` - `main(argc, argv)`. Win32 panels + raylib viewport.
- **Core engine:** `Source/Core.hpp` (~2400 lines, single header) - init, splash, menu, world loading, render loop, shaders.

## Source layout (condensed)
Full tree: `Wiki/Engine-Overview.md`. Key modules:
- Rendering/loop: `Source/Main.cpp`, `Source/Core.hpp`, `Source/Renderer/` (LitLightning, EngineBillboard, TextSystem), `Source/raygui/`, `Source/rlights/`, `System/Shaders/`.
- Networking: `Source/Network/Network.cpp/.hpp` (UDP, `#pragma pack(push,1)`), `Source/Client/Client.cpp`.
- Server: `Source/Server/` (Server.cpp, GameState, WDLParser, OzoneParser).
- Script/entities: `Source/Script/` (LightningScript parser/context; EntityRegistry scans `*.ozls` in GameData + packages; EntityManager).
- Pawn/world: `Source/Pawn/` (OzPawnSystem, Items/Entities/Objects/Player), `Source/Physics/` (OzBsp, WorldChunk), `Source/OzOzoneLoader.*`.
- Packaging: `Source/Package/` (OzPackage, PackageAssetLoader, OzAssetMapper), `Source/OzPack.cpp`.
- Audio: `Source/Audio/`; video: `Source/plmpeg/`; particles: `Source/ParticleDemon/`.

## Package system (OzPackage)
- Extensions + magic: `.ozpak`/OZPK (generic: models/scripts/shaders), `.oztex`/OZTX (textures), `.ozsnd`/OZSD (sounds), `.ozmux`/OZMX (music), `.ozone`/OZWN (worlds).
- Runtime: `PackageAssetLoader::Instance().Init()` scans `System/Data/*.oz*`. Read assets via `*WithFallback` wrappers (filesystem first, then packages).
- Models use temp-file cache in `System/Cache/` (raylib has no `LoadModelFromMemory`).
- Packaging: `OzPack pack <magic|auto> <dir> <out>`; also `unpack`, `list`, `dir`. `build-data.ps1` drives it per GameData subdirectory.

## World format (WDL) - legacy
- Colon-delimited text in `GameData/Worlds/<Name>/World.wdl` (all bundled worlds now use `.ozone` instead).
- Tokens recognized by `WDLParser::classify()`: `HeightMap`, `Collision`, `AdvCollision`, `ClipBox`, `Light`, `C`, `Spawn`, `Pickup`, `ZoneInfo`, `Portal`, `LevelInfo`, `Particles`, `Fog`, `Ambient`, `LightType`, `NPC`/`Walker`/`Skaarj`/`Brute`/`Floater`, and suffix-counted `Script*`, `Object*`, `Model<digits>`, `NE*` (noise emitters -> mp3s in `NoiseEmitter/`).
- I/O helpers: `Source/PPGIO.hpp` (`LoadFile`, `GetWDLSize`, `WSplitValue`, `WReadValue`, `ToFloat`).
- World subdirs: `Models/`, `Scripts/`, `Music/`, `NoiseEmitter/`.

## World format (OZONE) - current
- Primitive ops: `add`/`sub`/`intersect` + primitives `box cyl sph pyr pln`. Per-brush kwargs: `flags=N`, `texScaleU/V`, `texOffsetU/V`, `texPath=`.
- Entities: `playerstart`, `pickup`, `zone` (zones are scripted by sibling `<name>.ozls` skyzone files), `npc`, `light` (point/spot/directional). Z-up axis.
- Export from AngelEd: `ExportToOzone()` in `AngelEd/Source/Main.cpp`. Reference: `GameData/Worlds/*/World.ozone`.

## Conventions & quirks
- **`#pragma pack(push,1)`** for all network packet structs.
- **`Source/WindowsCompat.hpp`** must be included early in files touching both raylib and winsock2; it remaps `CloseWindow`/`ShowCursor`/`Rectangle`/`DrawText` to `__WIN32_*`/`GDI_*` before `#include <windows.h>`, then `#undef`s them.
- **`using namespace std;`** in `PPGIO.hpp`, `Data.hpp`, `Encoder.hpp`, `TextSystem.hpp`, `ParasiteScriptData.hpp`.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav`, `POS.sav`, `Script.sav`. All `*.sav` gitignored.
- gitignored: `System/`, `build/`, `build-ed/`, `.docs/`, `*.exe`, `*.o`.
- Run scripts (`System/run.bat`, `run.ps1`) set cwd to repo root before launching - the game expects repo-relative paths.

## Pawn system
- Data-driven NPC defs from `GameData/Global/PawnDefs/*.cfg` (name, speed, aggroRange, attackRange, damage, maxHealth, sprite_path, scream_path). Fallback hardcoded defs: Walker, Skaarj, Brute, Floater.
- **FSM is `PawnState`: IDLE, PATROL, CHASE, RETURN, DEAD** (no ATTACK state - check `Source/Pawn/OzPawnSystem.hpp:25`). State transitions fire `.ozls` script actions `on_patrol`/`on_chase`/`on_return`/`on_death`.
- NPCs attack only via melee range check (+ projectiles) - no ranged NPC fire.

## Weapons (b54+)
- Data-driven `.ozls` entities of type `weapon`: ranged (ProjectileNode; speed/spread/damage/lifetime; `magazine`/`reload_time` stats) or melee (`reach` stat, `on_swing`/`on_hit` actions).
- Key code: `Source/Script/LightningEntityManager.cpp` `FireSelectedWeapon()` (ammo/reload/cooldown dispatch); projectile sim in `Source/Pawn/OzPawnSystem.cpp` `SpawnProjectile`/`UpdateProjectiles` (client radius 1.5) and `Source/Server/GameState.cpp` `spawn_projectile`/`tick_projectiles` (server radius 2.0 - radii intentionally differ).

## Editor state (verified as of b54)
- Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. Reads `System/AngelEd.ini`.
- Lit/Unlit/Wire toggle now swaps material shaders; right-click context menu exists; CSG `Apply`/`MergePass` is called for OZONE primitives (EMID >= 200).
- Still missing: undo/redo, test-play mode. Docs: `Wiki/Editor-Usage.md`.

## CI (.github/workflows/ci.yml)
- Runs on every push/PR; tags matching `b*` also create a GitHub Release with zipped `System/`.
- **Linux:** build raylib from source (cached) -> `make AngelServ` -> `make OTENGINE` -> `make ozpack` -> smoke test with `timeout 3 ./AngelServ`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` -> build all 4 targets -> assemble System/ -> run `build-data.ps1` -> upload artifact. Note: CI compiles AngelEd with inline raw `g++` commands, NOT the `AngelEd/Makefile` - the two can drift.