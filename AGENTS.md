# AGENTS.md - Angels95 / OmegaTech Engine

PS1-styled multiplayer game on raylib 5.5 with a custom WDL/OZONE world format and dedicated server. C++20, single `g++` link per target via plain Makefiles, no cmake. Full docs live in `Wiki/` (Engine-Overview, Building, Editor-Usage, LightningScript, World-Format-WDL).

## Build

### Linux / macOS
```bash
make OTENGINE          # client -> Angels95
make AngelServ         # server, no raylib dep
make AngelMaster       # master server, no raylib dep
make ozpack            # asset packer
make -j$(nproc)        # all four
```
- raylib 5.5 must be installed system-wide (`/usr/local/lib/libraylib.a`), not vendored. `build.sh` installs it from source.

### Windows
- **w64devkit:** `.\build-native-win.ps1` (requires `C:\raylib\w64devkit`, GCC 15.2.0). Do NOT use WinGet GCC 16.1.0 - broken POSIX/UCRT headers.
- **MSYS2/MINGW64:** `.\build.ps1` (uses `mingw-w64-x86_64-raylib`; auto-builds raylib 5.5 to `~/raylib-5.5` if missing).
- Both build all 5 targets and assemble `System/`. Flags: `-SkipData` (skip asset packaging), `-SkipClean`.
- `build-data.ps1` drives `OzPack.exe` to create `.oz*` packages from `GameData/` subdirectories.

### Targets
| Target | Build cmd | Dependencies |
|---|---|---|
| `Angels95` (client) | `make OTENGINE` | raylib 5.5 |
| `AngelServ` (server) | `make AngelServ` | None (standalone, raw sockets) |
| `AngelEd` (editor) | `make -C AngelEd` | raylib 5.5 + Win32 (Makefile errors out on Linux) |
| `AngelMaster` (master server) | `make AngelMaster` | None (standalone, raw sockets + pthreads) |
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
Suites: `test_parser`, `test_context`, `test_registry`, `test_entity_manager`, `test_pawn_system`, `test_wdl_parser`, `test_ozone_parser`, `test_join_uri`, `test_master`, `test_network`, `test_game_state`.

- No test framework - standalone `tests/*.test.cpp` compiled directly. **Test executables land in repo root** (`./test_parser`, not `./tests/`).
- Most suites use `SERVER_CXX` + `-DOMEGA_TEST_ENV` (no raylib). **Exceptions:** `test_entity_manager` and `test_pawn_system` link raylib (Vector3/BoundingBox types).
- Single test targets: `make test_parser` / `make test_context` / `make test_registry` / `make test_wdl_parser` / `make test_ozone_parser` / `make test_join_uri` / `make test_master` / `make test_network` / `make test_game_state` etc.

## Runtime config (verified)
- **Client:** reads/writes `System/Angels95.ini` for real (loaded via
  `LoadClientSettings()` before `InitWindow` so window size/VSync/MSAA apply at
  creation; saved on exit by a static dtor in `Main.cpp`). Keys live in the
  `[Settings]` section; parser is `Source/IniConfig.hpp`. The build scripts'
  template ini is only a fallback. `[MasterServers]` (keys `Master`, `Master1`..)
  lists internet master URLs, read by `Source/Master/MasterList.hpp`.
- **Server:** `System/OzServer.ini` is a template written by build scripts -
  never read at runtime. Server behavior is controlled entirely by CLI flags
  (`--port`, `--http-port`, `--dir`, `--master`, `--master-http`, `--public-ip`,
  `--server-name`).
- `AngelEd` reads its INI (`g_config.Load("System/AngelEd.ini")`).

## Entrypoints
- **Client:** `Source/Main.cpp` - `main()` after OmegaTechInit, splash, home screen, world loading, game loop. Flags: `--world <name>`, `--world-dir <path>`, `--join <ip[:port]>`; also accepts an `angels95://join/<ip>:<port>` positional arg (web-portal deep link) and auto-joins/skips the menu. Registers the `angels95://` OS handler on launch (`Source/ProtocolHandler.hpp`, HKCU on Windows / user .desktop on Linux); URI parsing is `Source/JoinUri.hpp`.
- **Server:** `Source/Server/Server.cpp` - `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080, HTTP map API), `--dir` (GameData), `--auth-token` (HTTP Bearer gate; env `OZ_AUTH_TOKEN`), `--admin-token` (enables COMMAND list/say/kick; env `OZ_ADMIN_TOKEN`), `--server-name`, `--master host[:port]` (repeatable), `--master-http URL` (repeatable), `--public-ip`. LAN discovery UDP 27100; internet heartbeat uplink to masters (30s, background thread). Worlds seed NPCs/pickups from `World.ozone` entities (procedural ring only as fallback); server saves in `GameData/Saves/` (autosave 60s + shutdown).
- **Master server:** `Source/Master/Master.cpp` - `main(argc, argv)`. Flags: `--port` (27900 UDP heartbeats), `--http-port` (27950 JSON list), `--max-servers`, `--gamename`. Serves `GET /api/servers?gamename=angels95`, `GET /api/stats`, `POST /api/heartbeat`. Entries expire after 90s. Docs: `Wiki/Master-Server.md`.
- **Editor:** `AngelEd/Source/Main.cpp` - `main(argc, argv)`. Win32 panels + raylib viewport.
- **Core engine:** `Source/Core.hpp` (~2400 lines, single header) - init, splash, menu, world loading, render loop, shaders.

## Source layout (condensed)
Full tree: `Wiki/Engine-Overview.md`. Key modules:
- Rendering/loop: `Source/Main.cpp`, `Source/Core.hpp`, `Source/Renderer/` (LitLightning, EngineBillboard, TextSystem), `Source/raygui/`, `Source/rlights/`, `System/Shaders/`.
- Networking: `Source/Network/Network.cpp/.hpp` (UDP, `#pragma pack(push,1)`), `Source/Client/Client.cpp`, `Source/Master/` (AngelMaster daemon, `MasterClient` uplink, `MasterProtocol`/`MasterHttp`/`MasterList` helpers), `Source/Menu/InternetBrowser.hpp` (client browser).
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
- **FSM is `PawnState`: IDLE, PATROL, CHASE, RETURN, DEAD** (no ATTACK state - check `Source/Pawn/OzPawnSystem.hpp:25`). State transitions fire `.ozls` script actions `on_patrol`/`on_chase`/`on_return`/`on_death` — **only if a pawn-named `.ozls` def exists in the registry** (e.g. `Walker.ozls`); without one, `scriptInstanceIndex` stays -1 and hooks never fire. Projectile kills call `TransitionState(DEAD)`.
- NPCs attack only via melee range check (+ projectiles) - no ranged NPC fire. Scream plays on IDLE→CHASE (4s per-pawn cooldown).
- Multiplayer: server-owned NPCs carry `npc_type` in `NpcStateUpdateData` and spawn with `networkControlled=true` (local FSM + contact damage skipped; server applies damage via `PLAYER_HURT`).

## Weapons (b54+)
- Data-driven `.ozls` entities of type `weapon`: ranged (ProjectileNode; speed/spread/damage/lifetime; `magazine`/`reload_time` stats) or melee (`reach` stat, `on_swing`/`on_hit` actions).
- Key code: `Source/Script/LightningEntityManager.cpp` `FireSelectedWeapon()` (ammo/reload/cooldown dispatch); projectile sim in `Source/Pawn/OzPawnSystem.cpp` `SpawnProjectile`/`UpdateProjectiles` (client radius 1.5) and `Source/Server/GameState.cpp` `spawn_projectile`/`tick_projectiles` (server radius 2.0 - radii intentionally differ).

## Editor state (verified as of b58)
- Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. Reads `System/AngelEd.ini`.
- Lit/Unlit/Wire toggle swaps material shaders; right-click context menu exists; collision CSG runs via `OzoneLoader::RebuildCollisionVolumes` per brush `csgOp` (render meshes are not carved). Export preserves `add`/`sub`/`intersect`.
- Tool modes (Cam/Move/Scale/Rotate) are wired in placement mode; Pawn-tree Weapons branch places weapon pickups; sound preview has volume + loop.
- Still missing: undo/redo. Docs: `Wiki/Editor-Usage.md`.

## CI (.github/workflows/ci.yml)
- Runs on every push/PR; tags matching `b*` also create a GitHub Release with zipped `System/`.
- **Linux:** build raylib from source (cached) -> `make AngelServ` -> `make OTENGINE` -> `make ozpack` -> `make AngelMaster` -> smoke test with `timeout 3 ./AngelServ`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` -> build all 5 targets -> assemble System/ -> run `build-data.ps1` -> upload artifact. Note: CI compiles AngelEd with inline raw `g++` commands, NOT the `AngelEd/Makefile` - the two can drift.