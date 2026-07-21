# AGENTS.md — Angels95 / OmegaTech Engine

## Build

### Linux / macOS

```bash
make OTENGINE          # game client → Angels95
make oz_server         # dedicated server (no raylib dep)
make ozpack            # asset packer tool
make -j$(nproc)        # all targets
```

- Single g++ invocation per target, no cmake. Flags: `-O3 --std=c++20`.
- **raylib 5.5** must be installed system-wide (`/usr/local/lib/libraylib.a`). Not vendored.
- `build.sh` handles raylib install + make on Linux.

### Windows (Native w64devkit)

```powershell
.\build-native-win.ps1              # full System/ release
.\build-native-win.ps1 -SkipData    # skip asset packaging
.\build-native-win.ps1 -SkipClean   # skip clean step
```

- Requires `C:\raylib\w64devkit` (GCC 15.2.0) with raylib 5.5 statically linked.
- **Do NOT use WinGet GCC 16.1.0** — its C++ headers are broken with POSIX UCRT.
- Builds all targets: Angels95.exe, oz_server.exe, oz_editor.exe, OzPack.exe
- Assembles `System/` release with INI files, run scripts, and packaged assets.

### Windows (MSYS2/MINGW64)

```powershell
.\build.ps1                         # full System/ release
.\build.ps1 -SkipData               # skip asset packaging
```

- Uses `pacman -S mingw-w64-x86_64-raylib` for raylib.
- Same output structure as native build.

## System/ Release Layout

```
System/
  Angels95.exe          # Game client
  oz_server.exe         # Dedicated server
  oz_editor.exe         # Level editor (Win32)
  OzPack.exe            # Asset packer CLI
  Angels95.ini          # Client config
  oz_editor.ini         # Editor config
  run.bat / run.ps1     # Launch scripts (set cwd to repo root)
  Data/
    *.ozpak             # Generic asset packages (guns, objects, shaders)
    *.oztex             # Texture packages
    *.ozsnd             # Sound packages
    *.ozmux             # Music packages
    *.ozone             # World packages (OZWN format)
```

The game requires `GameData/` as a sibling directory to `System/` for worlds, saves, and loose assets not yet packaged.

## Package System (OzPackage)

### Format

Universal container with 32-byte header + sorted file index:
- **OZPK** (`.ozpak`) — generic assets (models, scripts, shaders)
- **OZTX** (`.oztex`) — textures (PNG)
- **OZSD** (`.ozsnd`) — sounds (WAV/MP3/OGG)
- **OZMX** (`.ozmux`) — music (WAV/MP3)
- **OZWN** (`.ozone`) — world files

### Runtime Loading

`PackageAssetLoader` (`Source/PackageAssetLoader.hpp`):
- Call `PackageAssetLoader::Instance().Init()` at startup to scan `System/Data/*.oz*`
- Provides `*WithFallback` wrappers: `LoadTextureWithFallback`, `LoadSoundWithFallback`, `LoadModelWithFallback`, `LoadShaderWithFallback`, etc.
- Path resolution: tries exact path → basename → stripped prefix
- Models use temp-file cache in `System/Cache/` (raylib has no `LoadModelFromMemory`)
- `ListAllFiles()` enumerates entries across all loaded packages (used by editor)

### Asset Packaging

```powershell
.\build-data.ps1        # packages all GameData assets into System/Data/
```

Uses `OzPack.exe` to create packages from `GameData/` subdirectories.

## Architecture

- **Client entrypoint:** `Source/Main.cpp:694` `main()`
- **Server entrypoint:** `Source/Server/Server.cpp:794` `main()` — no raylib dep, uses raw sockets
- **Editor entrypoint:** `OTEditor/Source/Main.cpp:363` `main()` — Win32 panels + raylib viewport
- **Engine core:** `Source/Core.hpp` (~2200 lines, single header) — init, menu, world loading, render loop, shaders
- **World format:** WDL (colon-delimited plain text) + OZONE (editor format). Parser in `Source/Server/WDLParser.hpp` (standalone, no raylib)
- **Networking:** Custom UDP protocol (`Source/Network/Network.hpp`). Packed structs (`#pragma pack(push,1)`). Discovery on UDP 27100, game on 27015.
- **Server HTTP API:** raw socket server in `Server.cpp` — routes `GET /map?list` and `GET /map?name=X` on port 8080.
- **Inventory:** 20 backpack slots + 8 equipment slots + 5 weapon hotbar slots. Items defined in `Source/Items.hpp`. Server maps pickup types to item_id.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav` (flags), `POS.sav` (position), `Script.sav` (WDL scripts).

## PawnSystem (NPC/AI)

Replaces the legacy `OmegaEnemy[10]` array with a dynamic, unlimited NPC system.

- **Header:** `Source/oz_pawn_system.h`
- **Implementation:** `Source/oz_pawn_system.cpp`
- **FSM states:** IDLE → PATROL → CHASE → RETURN → DEAD
- **Usage:**
  ```cpp
  PawnSystem::Instance().RegisterDef({"Walker", 1.5f, 6.0f, 1.5f, 10.0f, 100});
  PawnSystem::Instance().Spawn({x, y, z}, "Walker");
  PawnSystem::Instance().Update(playerPos, dt);
  PawnSystem::Instance().DrawAll(camera);
  ```
- **GetDefs()** returns `const std::vector<PawnDef>&` for enumerating registered definitions (used by editor Pawn Manager).
- **Integration:** WDL "Walker" entries spawn via `PawnSystem::Spawn`. `UpdateEntities()` calls `PawnSystem::Update` + `DrawAll`.
- **Editor:** Registers same defs (Walker, Skaarj, Brute, Floater). Win32 Pawn Manager panel for spawning.

## Editor (oz_editor)

Win32 native panels + raylib 3D viewport.

- **Source:** `OTEditor/Source/`
- **Panels:** Model Browser, Environment Settings, Pickups, Nodes, Pawn Manager, Texture Manager, Sound Manager, Script Manager
- **Top menu bar** replaces old in-viewport raygui overlay. Controls placed in File, Models, Pickups, Nodes, View, Pawn menus.
- **Win32 API:** `OTEditor/Source/Win32Dialogs.cpp` — uses `HWND` handles stored as `void*` in `EditorPanelState` for cross-platform compat. Requires `(HWND)` casts at call sites.
- **Win32 message loop:** The editor main loop calls `PeekMessage`/`TranslateMessage`/`DispatchMessage` before each raylib frame to process Win32 panel events.
- **Dynamic file scanning:** All panels now scan `GameData/` recursively + package entries via `PackageAssetLoader`. No hardcoded file stubs.
- **Unicode:** `Win32Dialogs.cpp` defines `UNICODE` / `_UNICODE` for wide-string Win32 API.
- **Linked engine objects:** `oz_pawn_system.o`, `oz_ozone_loader.o`, `OzoneParser.o`
- **Editor build (manual):** See `build-native-win.ps1` or CI workflow for exact g++ flags.

## Conventions & quirks

- **Windows compat hack:** `Source/WindowsCompat.hpp` `#define CloseWindow __WIN32_CloseWindow` to avoid raylib/winsock2 name collision. Must be included early in any file that touches both.
- **OTCustom statically linked** to avoid DLL issues cross-platform.
- **Asset paths:** Most loads use `*WithFallback` wrappers that check filesystem first, then packages. Server `--dir` flag overrides for world scanning.
- **Game data encryption** (`GameDataEncoded` flag) uses a substitution cipher in `Encoder.hpp`. Currently disabled (`false`).
- **`using namespace std;`** used in multiple headers (`PPGIO.hpp`, `Data.hpp`, `Encoder.hpp`, `TextSystem.hpp`, `ParasiteScriptData.hpp`).
- **`Entities.hpp`** retains `EnemyTexture` class for legacy texture/sound handles; the dynamic entity system is `PawnSystem`.

## CI (.github/workflows/ci.yml)

Two-job matrix (Linux + Windows MSYS2):

### Linux
1. Build raylib from source (cached by version)
2. `make oz_server`, `make OTENGINE`, `make ozpack`
3. Smoke test: `timeout 3 ./oz_server --dir GameData --port 27015 --http-port 8080`
4. Upload binaries: `oz_server-linux`, `Angels95-linux`, `OzPack-linux`

### Windows (MSYS2)
1. Install `mingw-w64-x86_64-{gcc,make,raylib}`
2. Build all targets: `oz_server`, `OTENGINE`, `ozpack`, `oz_editor`
3. Assemble `System/` release with EXEs, DLLs, INI files, run scripts
4. Run `build-data.ps1` to package assets
5. Upload `System-windows` artifact (full release tree)
6. Legacy: Upload `Angels95-windows` (EXE + DLLs only)

## Branch workflow

Active development on `feat/items-pickups`. Master is fast-forwarded from that branch. No merge commits.
