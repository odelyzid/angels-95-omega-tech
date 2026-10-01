# AGENTS.md - Angels95 / OmegaTech Engine

PS1-styled multiplayer game on raylib 5.5 with a custom OZONE world format and dedicated server. C++20, single `g++` link per target via plain Makefiles, no cmake. Full docs live in `Wiki/` (Engine-Overview, Building, Editor-Usage, LightningScript, World-Format-OZONE).

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
- **cmd:** `build.cmd [fast] [debug] [skipdata]`.
- Both PowerShell scripts build all 5 targets and assemble `System/`. Flags: `-SkipData`, `-SkipClean`, `-Fast` (incremental; implies `-SkipClean`), `-Debug` (`MODE=debug`).
- `build-data.ps1` drives `OzPack.exe` to create `.oz*` packages from `GameData/` subdirectories.

### Build speed / iteration
Plain `make` is already incremental (objects in `build/`), but the scripts run `make clean` unless you skip it. For fast iteration:
```bash
make -j$(nproc) MODE=debug OTENGINE   # -O0 -g, no clean
.\build-native-win.ps1 -Fast -Debug -SkipData   # or: .\build.ps1 / build.cmd fast debug
```
- `MODE=debug` swaps `-O3` for `-O0 -g` (much faster compiles + symbols). Default is `release`.
- **ccache** is auto-detected on PATH (prefixes `g++`/`gcc`); disable with `CCACHE=` / force with `CCACHE=<path>`. Install: `pacman -S mingw-w64-x86_64-ccache`.
- **LLD** is auto-enabled as linker (`-fuse-ld=lld`) when `ld.lld`/`lld` is on PATH; override with `LDEXTRA=-fuse-ld=lld` or `LDEXTRA=` to disable. Install: `pacman -S mingw-w64-x86_64-lld`.
- `make help` prints the resolved `MODE`/`OPTFLAGS`/ccache/linker.

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
- `Source/Renderer/raygui/raygui.c` needs `-fpermissive -DRAYGUI_IMPLEMENTATION` (not plain C++).
- `windres` embeds `.rc` icons; server/client Windows builds need it on PATH.
- `clean` also removes the test executables sitting in the repo root.

## Tests
```bash
make test   # builds + runs ALL suites (continues past failures)
```
Suites: `test_parser`, `test_context`, `test_registry`, `test_entity_manager`, `test_pawn_system`, `test_ozone_parser`, `test_join_uri`, `test_master`, `test_network`, `test_game_state`.

- No test framework - standalone `tests/*.test.cpp` compiled directly. **Test executables land in repo root** (`./test_parser`, not `./tests/`).
- Most suites use `SERVER_CXX` + `-DOMEGA_TEST_ENV` (no raylib). **Exceptions:** `test_entity_manager` and `test_pawn_system` link raylib (Vector3/BoundingBox types).
- Single test targets: `make test_parser` / `make test_context` / `make test_registry` / `make test_ozone_parser` / `make test_join_uri` / `make test_master` / `make test_network` / `make test_game_state` etc.

## Runtime config (verified)
- **Client:** reads/writes `System/Angels95.ini` for real (loaded via
  `LoadClientSettings()` before `InitWindow` so window size/VSync/MSAA apply at
  creation; saved on exit by a static dtor in `Main.cpp`). Keys live in the
  `[Settings]` section; parser is `Source/IniConfig.hpp`. The build scripts'
template ini is only a fallback. `[MasterServers]` (keys `Master`, `Master1`..)
lists internet master URLs (http:// or https://), read by
`Source/Server/Master/MasterList.hpp`. Default when unset:
`https://angels95.tribewarez.com/master`. TLS uses WinHTTP (Windows) / `curl`
(Linux) — see `Source/Server/Master/MasterHttp.hpp`; Windows links `-lwinhttp`.
- **Server:** `System/OzServer.ini` is a template written by build scripts -
  never read at runtime. Server behavior is controlled entirely by CLI flags
  (`--port`, `--http-port`, `--dir`, `--master`, `--master-http`, `--public-ip`,
  `--server-name`).
- `AngelEd` reads its INI (`g_config.Load("System/AngelEd.ini")`).
- **Player profiles:** `System/PlayerProfiles.ini`, written by
  `Source/PlayerProfile.{hpp,cpp}` (`PlayerProfileManager::Instance()`). Holds
  player slots (`displayName`, `modelPath`, `voiceSet`, `team`) edited in the
  title menu's Character pane (`Source/Menu/TitleMenu.hpp`). Loaded once in
  `OmegaTechInit()` and re-saved on every mutation (no exit hook), so reopening
  the pane never discards edits. The struct is **raylib-free** so AngelEd and the
  server can read the same file; `SetPath()` redirects it for tests. One
  `[Profiles]` section with `Profile<N>_*` keys — **not** one section per profile,
  because `IniConfig` stores sections in an `unordered_map` and per-profile
  sections would come back in arbitrary order. Writing bypasses `IniConfig::Save`
  (unordered) to keep the file hand-editable; loading still uses `IniConfig`.
- **Profile over the wire (protocol v2):** the active profile is attached to
  `CLIENT_AUTH` as `net::ClientAuthPayload` (packed, with a `structSize`
  forward-compat field so optional fields can be appended without another bump).
  `PROTOCOL_VERSION` went 1->2 and is now actually sent and checked - it was a
  dead constant before, so there had been **no** version negotiation on the wire.
  A payload shorter than `kAuthSizeV2` is a v1 peer and is still accepted with
  generated defaults; an unrecognised `protocolVersion` is **rejected** rather
  than guessed at, because a misparse here assigns the wrong identity to a player.
  **Trust model - every field in that payload is client-claimed.** The server
  sanitises with `net::SanitizeDisplayName` (strips C0/DEL, keeps UTF-8),
  `net::SanitizeAssetRef` (ASCII only, **rejects `..` path segments**) and
  `net::ClampRequestedTeam` (out of range -> 0, never clamps onto a real team).
  `modelPath`/`voiceSet` are **opaque strings the server never opens or
  resolves** - if a future server-side loader ever consumes `modelPath` it must
  treat it as untrusted input. Display names are de-duplicated via
  `net::MakeUniqueName` because GameState persists save data keyed by name, so
  two players sharing one would collide. The server replies `PROFILE_STATE` so the
  client learns what was actually accepted instead of assuming its own values
  were honoured.
  **Team is NOT trusted and NOT assigned yet - the server has no team concept at
  all.** `ServerPlayer::team` (server-owned, always 0 today) is the only field
  gameplay may read; `requested_team` is the untrusted client hint, recorded for
  a future team system. Nothing may score or match on `requested_team`.
  Covered by `tests/Network.test.cpp` (profile round-trip and de-duplication run
  over a real loopback socket).

## Entrypoints
- **Client:** `Source/Main.cpp` - `main()` after OmegaTechInit, splash, home screen, world loading, game loop. Flags: `--world <name>`, `--world-dir <path>`, `--join <ip[:port]>`; also accepts an `angels95://join/<ip>:<port>` positional arg (web-portal deep link) and auto-joins/skips the menu. Registers the `angels95://` OS handler on launch (`Source/ProtocolHandler.hpp`, HKCU on Windows / user .desktop on Linux); URI parsing is `Source/JoinUri.hpp`. **Screenshot mode** (see `Source/Screenshot.hpp`): `--shot <out.png>`, `--shot-delay <frames>` (default 90; needs >=2 so the skyzone `on_enter` uniforms land), `--shot-res <WxH>`, `--shot-cam "x,y,z,yaw[,pitch]"` (repeatable, **OZONE Z-up**, yaw/pitch in degrees; repeats produce `out_1.png`, `out_2.png`, ...), `--shot-hud` (keep HUD). Suppresses splash/menu/audio and forces vsync/MSAA/pixel/jitter/fog/head-bob/debug/FPS off, hides hotbar/HUD/crosshair/view-model, freezes the camera via `isNoClip`, and exits when every camera is captured. `GameData/Launch.conf` is skipped in shot mode so `--shot-res` is authoritative.
- **Server:** `Source/Server/Server.cpp` - `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080, HTTP map API), `--dir` (GameData), `--bind` (default all interfaces; literal IPv4 or hostname, use `0.0.0.0` on a VPS), `--auth-token` (HTTP Bearer gate; env `OZ_AUTH_TOKEN`), `--admin-token` (enables COMMAND list/say/kick; env `OZ_ADMIN_TOKEN`), `--server-name`, `--master host[:port]` (repeatable), `--master-http URL` (repeatable), `--public-ip`. Loads `System/OzServer.ini` (`[Server]`/`[Auth]`/`[MasterServers]`) first; CLI/env override it. LAN discovery UDP 27100; internet heartbeat uplink to masters (30s, background thread). Worlds seed NPCs/pickups from `World.ozone` entities (procedural ring only as fallback); server saves in `GameData/Saves/` (autosave 60s + shutdown). VPS: `System/angels95-serv.service`.
- **Master server:** `Source/Server/Master/Master.cpp` - `main(argc, argv)`. Flags: `--port` (27900 UDP heartbeats), `--http-port` (27950 JSON list), `--max-servers`, `--gamename`, `--bind` (default 127.0.0.1; use `0.0.0.0` on a VPS). Serves `GET /api/servers?gamename=angels95`, `GET /api/stats`, `POST /api/heartbeat`. Entries expire after 90s. A self-reported `publicip` is only trusted when the heartbeat arrives from a non-private source (anti-spoof). Docs: `Wiki/Master-Server.md`; VPS unit: `System/angels95-serv.service`.
- **Editor:** `AngelEd/Source/Main.cpp` - `main(argc, argv)`. Win32 panels + raylib viewport.
- **Core engine:** `Source/Core.hpp` (~2400 lines, single header) - init, splash, menu, world loading, render loop, shaders.

## Source layout (condensed)
Full tree: `Wiki/Engine-Overview.md`. Key modules:
Full tree: `Wiki/Engine-Overview.md`. Key modules:
- Rendering/loop: `Source/Main.cpp`, `Source/Core.hpp`, `Source/Renderer/` (LitLightning `LitLightning_UpdateFrame` owns the per-frame lighting pass, EngineBillboard, TextSystem), `Source/Renderer/raygui/`, `Source/Renderer/rlights/`, `System/Shaders/`.
- Networking: `Source/Server/Network/Network.cpp/.hpp` (UDP, `#pragma pack(push,1)`), `Source/Client/Client.cpp`, `Source/Server/Master/` (AngelMaster daemon, `MasterClient` uplink, `MasterProtocol`/`MasterHttp`/`MasterList` helpers), `Source/Menu/InternetBrowser.hpp` (client browser).
- Server: `Source/Server/` (Server.cpp, GameState, OzoneParser).
- Script/entities: `Source/Script/` (LightningScript parser/context; EntityRegistry scans `*.ozls` in GameData + packages; EntityManager; `GameUI` EntityType is a data+hook-only declarative HUD layer).
- Player: `Source/Pawn/AngelPlayer/` (GameUi bridge, **SlotBar** shared HUD bar renderer, InventoryBehaviour HUD/overlay + item/weapon collect hooks, WeaponBehaviour fire/recoil/ADS, PlayerController toggles/vertical/game-over) and `Source/Pawn/PickupPawns.*` (networked-pickup collect loop only).
- Pawn/world: `Source/Pawn/` (OzPawnSystem, Items/Entities/Objects/Player, PickupPawns), `Source/Physics/` (OzBsp, WorldChunk, PlayerPhysics + PhysicsInfo), `Source/World/` (OzoneParser, OzOzoneLoader, OzoneFrustum, OzoneHeightmap, **ZoneManager**, **ZoneTypes**, **LevelSettings**).
- Packaging: `Source/Package/` (OzPackage, PackageAssetLoader, OzAssetMapper), `Source/OzPack.cpp`.
- Audio: `Source/Audio/` (**SoundManager** = the single facade for every sound/music call, plus `DspReverb`); video: `Source/Renderer/plmpeg/`; particles: `Source/Particle/`.
- Platform UI: `Source/UI/UiHandler.*` (Win32 native menu bar behind `oz::ui::CreateNativeMenuBar(callbacks)`; no-op off Windows).

## Package system (OzPackage)
- Extensions + magic: `.ozpak`/OZPK (generic: models/scripts/shaders), `.oztex`/OZTX (textures), `.ozsnd`/OZSD (sounds), `.ozmux`/OZMX (music), `.ozone`/OZWN (worlds).
- Runtime: `PackageAssetLoader::Instance().Init()` scans `System/Data/*.oz*`. Read assets via `*WithFallback` wrappers (filesystem first, then packages).
- Models use temp-file cache in `System/Cache/` (raylib has no `LoadModelFromMemory`).
- Packaging: `OzPack pack <magic|auto> <dir> <out>`; also `unpack`, `list`, `dir`. `build-data.ps1` drives it per GameData subdirectory.

## World format (OZONE) - sole source of truth
- The legacy WDL format and its fallback loader have been **removed**. OZONE + the LightningScript/Pawn entity system are the only world source of truth. A missing `GameData/Worlds/<Name>/World.ozone` is a hard error.
- Primitive ops: `add`/`sub`/`intersect` + primitives `box cyl sph pyr pln`. Per-brush kwargs: `flags=N`, `texScaleU/V`, `texOffsetU/V`, `texPath=`.
- **Authoring traps** (each of these silently does nothing, or silently does the wrong thing):
  - `texSlot` is a **bare positional float** (arg 8 for `box`, arg 9 for `cyl`), *not* a `key=value` kwarg. `texSlot=3` is dropped and auto-selection applies instead (`h<1` -> tileset slot 1, else slot 2).
  - `scale=` (Mesh) and `radius=` (PathNode) are appended to the **positional** args vector, so they must come *after* all positional floats. `name=`, `tex=`, `anim=`, `speed=`, `next=`, `loop`, `gravity=`..`fly_mult=` are order-independent named kwargs.
  - `cyl` spans **z = [cz-h, cz]** — the authored `cz` is the *top*, not the centre. `box` is centre-based.
  - Unknown tokens on a brush line are ignored, but any **bare number** inside a trailing `#` comment is swallowed as an argument (and arg 8/9 is the texSlot).
  - There is **no quote parsing** in the parser. `texPath="my tex.png"` splits at the space. Only whole-token fields (`Mesh.*` path, `tex=`, `next=`, heightmap paths) tolerate quotes.
  - Every zone needs an explicit `name=`. Auto-generated names (`zone_sky_0`) are per-load counters and **collide across worlds**; `.ozls` defs live in one global last-writer-wins map. `LightningEntityRegistry::LoadWorldOverrides()` is called on every `LoadWorld()` to re-point that map at the world being played.
  - Only `playerstart` **#1** is ever used (`PawnSystem::GetFirstPlayerStart`) — it alone decides a level's first frame.
  - `light directional <x> <y> <z>`: the authored point is the light **SOURCE**, aimed at the world origin (see `OzOzoneLoader`). A large positive z = overhead sun.
  - `sub` carves **collision only**, never the render mesh. Build openings from separate pier/lintel boxes, never by subtracting from a solid.
- Entities: `playerstart`, `pickup`, `zone` (zones are scripted by sibling `<name>.ozls` skyzone files; support per-zone physics kwargs `gravity=`, `jump=`, `terminal=`, `water_gravity=`, `water_drag=`, `swim_up=`, `ladder_speed=`, `fly_mult=` overriding `oz::physics::PhysicsInfo`), `npc`, `light` (point/spot/directional). Z-up axis.
- Export from AngelEd: `ExportToOzone()` in `AngelEd/Source/Main.cpp`. Reference: `GameData/Worlds/*/World.ozone`.
- Shared I/O helpers: `Source/PPGIO.hpp` (`LoadFile`, `WSplitValue`, `WReadValue`, `ToFloat`; save/config parsing only).
- World subdirs: `Models/`, `Music/`.

## World module layout (`Source/World/`)
```
World/
├── LevelSettings.hpp          // Extracted level metadata, game rules, and skybox paths
├── ZoneTypes.hpp              // Extracted ZoneType enum and ZoneEnvOverrides runtime structs
├── ZoneManager.hpp/.cpp       // Zone volume + portal storage, runtime queries, PointRegion
├── OzoneParser.hpp/.cpp       // Pure text-to-primitive parser (no raylib/gameplay dependencies)
├── OzOzoneLoader.hpp/.cpp     // Geometry, heightmap, and spatial chunk provider
├── OzoneFrustum.hpp/.cpp      // Frustum culling utility math
└── OzoneHeightmap.cpp         // Terrain mesh generation and height sampling logic
```
- **`ZoneTypes.hpp`** — `ZoneType`, `ZoneEnvOverrides` (with `Merge()`), `GameplaySoundProfile`, plus `ZoneTypeFromString()` / `ZoneTypeName()`. No raylib; include it from tools/tests.
- **`LevelSettings.hpp`** — `LevelSettings` (game rules, skybox paths, weather particles) plus `ParseLevelInfo()` / `ParseLevelParticles()` so the OZONE primitive layout lives next to the struct.
- **`ZoneManager.hpp/.cpp`** — owns `ZoneVolumeNode`, `ZonePortal`, `PointRegion` and every zone query (`GetActiveZones`, `CheckZoneCollision`, `CheckPortalCollision`, `UpdatePlayerRegion`). These used to live in `PawnSystem`; **runtime zone entry checks, volume detection and env-override merging now go through `ZoneManager::Instance()`**, not `PawnSystem::Instance()`. Env overrides layer lowest-priority-first so the highest-priority zone wins. `PawnSystem::AssignLightZones()` binds lights to their containing zone.
- **`OzOzoneLoader`** is a **pure data provider**: `LoadFile`/`LoadString` fill an `OzoneEntitySet` (`GetEntities()` / `GetLevelSettings()` / `GetWorldDir()`) and never mutate `PawnSystem`/`ZoneManager`. The orchestrator applies it explicitly — `Core.hpp LoadWorld()` and AngelEd's open/undo-restore paths both call
  `InjectOzoneEntities(OzoneLoader::Instance().GetEntities(), PawnSystem::Instance());`

## Audio (SoundManager)
- `Source/Audio/SoundManager.hpp/.cpp` is the **single entry point for every sound/music call**. No gameplay file should touch raylib sound handles directly.
- API: `LoadCoreSounds()` (init), `AttachReverbProcessor()` (device hookup), `ResetWorldAudio()` / `Shutdown()`, one-shots (`PlayUIClick`, `PlayChasing`, `PlayDeath`, `PlayCollision`, `PlayTextNoise`, `PlayScream`, `LoadPawnScream`, `StartWalkLoop`/`StopWalkLoop`), music (`PlayWorldMusic(assetPrefix)`, `StopWorldMusic`, `Update()`, generic `Play/Stop/UpdateStream` for caller-owned handles), zone audio (`UpdateSoundZones(region)`, `UpdateReverb(region)`), script audio (`PlayScriptSound(path)`, `PruneScriptSoundCache`).
- Per-world audio state (world music, zone ambience handle, reverb hook, script sound cache) lives in SoundManager — the `g_prevSoundZone`/`g_defaultWorldMusic`/`g_ambienceHandle`/`g_wasInReverb` globals are gone from `Core.hpp`.
- `LightningScript play_sound` routes through `SoundManager::PlayScriptSound` and is **package-aware** (`LoadSoundWithFallback`), so `.ozsnd` assets resolve.
- `GameplaySoundProfile` is consumed **only** by `SoundManager::UpdateSoundZones`; `ZONE_GAMEPLAY_SOUND` volumes are excluded from the LightningScript `on_enter`/`on_exit` zone hooks in `Main.cpp` because they are handled here instead. The profile fields `music_on_exit`, `sfx_on_combat` and `volume_mult` are editor-authored but not yet consumed.
- `DspReverb` (Schroeder reverb) stays a standalone all-static DSP; only `SoundManager` drives `SetMix`/`SetDecay`.
## Conventions & quirks
- **`#pragma pack(push,1)`** for all network packet structs.
- **`Source/WindowsCompat.hpp`** must be included early in files touching both raylib and winsock2; it remaps `CloseWindow`/`ShowCursor`/`Rectangle`/`DrawText` to `__WIN32_*`/`GDI_*` before `#include <windows.h>`, then `#undef`s them.
- **`using namespace std;`** in `PPGIO.hpp`, `Data.hpp`, `TextSystem.hpp`.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav`, `POS.sav`, `Script.sav`. All `*.sav` gitignored.
- gitignored: `System/`, `build/`, `build-ed/`, `.docs/`, `*.exe`, `*.o`.
- Run scripts (`System/run.bat`, `run.ps1`) set cwd to repo root before launching - the game expects repo-relative paths.

## Script/entities: GameUI + SlotBar (HUD object bar)
- `GameData/Global/UI/GameUI.ozls` (`EntityType::GAMEUI`) is the **authored** source of the player's HUD object bar: one atlas `texture =` plus a `slot_rects` stat.
- `Source/Pawn/AngelPlayer/GameUi.{hpp,cpp}` = C++ bridge. Exposes the texture plus `SlotCount()`/`SlotRect(i)` and the layout stats (`hud_width_pct`, `hud_bottom_margin`, `icon_inset`, `show_slot_numbers`, `show_slot_name`). Still data-only — it never draws.
- `Source/Pawn/AngelPlayer/SlotBar.{hpp,cpp}` = the **one** renderer. `DrawSlotBar(SlotBarOptions&, int& hover)` maps the atlas to the screen, draws it once, then fits each occupied cell's entity icon (aspect preserved, centred), the slot number, the gold selection/hover outline, and the selected item's name. It also does click-to-select.
- Both call sites share it: `LightningEntityManager::DrawHotbar()` (in-world, bottom centre) and `InventoryBehaviour::DrawOverlay()` (overlay mini-bar via `forcedWidth`). Each keeps its plain-rectangle fallback for when `GameUi::HasBar()` is false, so a missing/!def'd `GameUI.ozls` degrades instead of blanking.
- **Authoring `slot_rects`:** comma-separated `(x,y,w,h)` quadruples in *source-texture* pixels, left to right. It **must contain no spaces** — the stats parser reads one whitespace-delimited token and does not honour quotes, so a space silently truncates the value. Quotes are tolerated and stripped by `GameUi::ParseSlots()`. Re-measure the rects when swapping the atlas; the C++ never guesses geometry.
- Gameplay slot count stays `LightningEntityManager::HOTBAR_SIZE` (8), mirrored by literal `slot < 8` bounds in `Server/Server.cpp` and `Server/GameState.cpp`. The bar art has 8 cells to match; raising it means touching both.
- `SlotBar.cpp` has an opt-in `#ifdef OZ_SLOTBAR_PROBE` hook that fills empty slots with known defs so `--shot --shot-hud` can prove the icon path headlessly. Never defined by the shipped build.

## Editor asset managers (Model Browser + Texture Manager)
- Both panels present assets as the **same two-root tree**, never one flat list, so it is always obvious whether an asset is an editable file or a package record:
  - `(GameData)` — loose files under `GameData/`, nested by their real folder
  - `(Packages)` — assets that only exist inside a `.oz*` package
- A **`.oz*` file is the only thing treated as a package**; anything reachable on disk is a real file even when a package holds a same-named copy. Implemented once in `BuildAssetScope()` (`Win32Dialogs.cpp`) over `AssetScopeItem`/`AssetScopeNode` (`Win32Dialogs.hpp`) and shared by both panels, so the two can't drift.
- Leaves carry their index into the caller's vector in the treeview `lParam`; folders carry `-1`. **Treeview selection arrives as `WM_NOTIFY`/`TVN_SELCHANGEDW`, not `WM_COMMAND`.**
- Each panel has a search box that re-filters the already-loaded in-memory entries (no filesystem hit per keystroke). While a search is active, matching folders auto-expand; empty folders are pruned.
- Texture Manager: the tree is a **scope selector** for the thumbnail grid, not a filter of it. `UpdateTextureScopeSelection()` republishes `g_textureVisible` (indices into `g_textureFiles`) from the selected subtree; empty means "all", which is the grid's original behaviour. It is split out from `FillTextureScopeTree()` on purpose — rebuilding the tree from inside its own selection notification would destroy that selection. The grid stores a *grid position*, so always map through `TexEntryAt()`.
- **Imports pack immediately** (`PackIntoPackage`/`HotLoadPackage` in `Win32Dialogs.cpp`): model import → `System/Data/imported_models.ozpak` (OZPK), texture import → `System/Data/imported_textures.oztex` (OZTX). The package is *appended* (existing entries read back with `OzPackageReader::Read` and rewritten), then hot-loaded via `LoadPackageFile`, so entries resolve this session and land under `(Packages)`. Requires miniz, which AngelEd already links.
- Imported textures are **free-placement assets**, not tileset entries: usable via `tex=` on `Mesh.Static`/`Mesh.Skeletal` and as a model texture, but **not** as a brush `texSlot`. The import dialog says so loudly (`kTexSlotWarning`) because `texSlot` is a bare positional float indexing `<world>/oztex/tileset/` — silently getting auto-selection back is a confusing authoring bug.

## Pawn system

- Data-driven NPC defs from `GameData/Global/PawnDefs/*.cfg` (name, speed, aggroRange, attackRange, damage, maxHealth, sprite_path, scream_path). Fallback hardcoded defs: Walker, Skaarj, Brute, Floater.
- **FSM is `PawnState`: IDLE, PATROL, CHASE, RETURN, DEAD** (no ATTACK state - check `Source/Pawn/OzPawnSystem.hpp:25`). State transitions fire `.ozls` script actions `on_patrol`/`on_chase`/`on_return`/`on_death` — **only if a pawn-named `.ozls` def exists in the registry** (e.g. `Walker.ozls`); without one, `scriptInstanceIndex` stays -1 and hooks never fire. Projectile kills call `TransitionState(DEAD)`.
- NPCs attack only via melee range check (+ projectiles) - no ranged NPC fire. Scream plays on IDLE→CHASE (4s per-pawn cooldown).
- Multiplayer: server-owned NPCs carry `npc_type` in `NpcStateUpdateData` and spawn with `networkControlled=true` (local FSM + contact damage skipped; server applies damage via `PLAYER_HURT`).
- Animated pawns: `.cfg` keys `mesh_type` (`static`/`skeletal`) + `anim_idle/patrol/chase/return/death` + `anim_speed`. A skeletal pawn uses `oz::SkeletalMesh` (GLB/GLTF/IQM only — not OBJ) and maps `PawnState` to a clip. Yaw tracks movement for skeletal pawns; static/billboard pawns still face the camera.

## Mesh / entity taxonomy
- `Source/Renderer/Mesh/` — `oz::Mesh` base, `oz::StaticMesh`, `oz::SkeletalMesh` (clips + stateless `ApplyPose`), and the internal `oz::MeshCache` (owned by `PawnSystem`; intentionally leaked so GL unloads never run after `CloseWindow`). Names are namespaced because raylib also defines `::Mesh`/`::Model`/`::Transform`.
- Model+clips are shared per asset; **per-instance playback state** (`animClip`/`animTime`) lives on the `Pawn`/`MeshObjectNode` — apply pose then draw immediately.
- `.ozls` `EntityType` tokens: `Mesh.Static`, `Mesh.Skeletal`, `ParticleEmitter`, `WindZone`. `LightningScriptParser` has first-class body keys `mesh_type`, `anim_idle/patrol/chase/return/death`, `anim_speed` (top-level; unknown keys are silently dropped).
- OZONE entities: `Mesh.Static <path> x y z yaw [scale=] [tex=]` and `Mesh.Skeletal <path> x y z yaw [scale=] [tex=] [anim=Clip] [speed=]`. Parsed in `OzoneParser`, loaded into `PawnSystem::m_meshObjects` by `InjectOzoneEntities`, drawn by `DrawEntities`, exported by `AngelEd` `ExportToOzone`. Client-only (not networked). AngelEd also has a `GameEngine.Mesh` tree branch (places the Model Browser selection) plus `SelType::MESH` selection, properties panel (path/texture/clip/scale/pos/yaw + Reload), delete/duplicate.
- PS1 look: model textures get `TEXTURE_FILTER_POINT` in `oz::Mesh::Load`.

## Particles (GameEngine.ParticleEmitter)
- Emitter node defs are boxed in `PawnSystem` (`m_particleEmitters`, `AddParticleEmitter`/`GetParticleEmitters`/`ClearParticleEmitters`). OZONE line: `ParticleEmitter <type> x y z [rate life speed spread sizeStart sizeEnd r g b rEnd gEnd bEnd gravity radius dirX dirY dirZ yaw] [tex=path]`.
- Simulation + rendering live in `Source/Particle/OzParticleSimulationManager.*` (pool of 4096, generated soft default texture, optional per-emitter `tex=`). **Isolation contract:** it is ticked once per frame in `Core.hpp` *after* the weapon/NPC/projectile sim loop and only reads emitter defs — it never calls gameplay code, so particles can't interrupt weapon mechanics or NPC ticks. Client-only/cosmetic (not networked).
- AngelEd: `GameEngine.ParticleEmitter` tree leaf places a default fire emitter; `SelType::PARTICLE` select/delete/duplicate + properties (type/tex/rate/life/speed/size/spread/color) + `ExportToOzone`.

## Path nodes (GameEngine.PathNode)
- OZONE line: `PathNode <name> x y z [radius=R] [next=a,b,c] [loop]`. Links are **by node name** (not index), so the graph survives editor reordering.
- Client: nodes boxed in `PawnSystem` (`m_pathNodes`, `AddPathNode`/`GetPathNodes`/`FindPathNodeByName`/`ClearPathNodes`), loaded by `InjectOzoneEntities`. Client-side nodes are for editor placement/visualization + round-trip only (the client never runs path AI).
- Server: `ServerPathNode` + `WorldState::path_nodes`, seeded in `seed_world_entities`; `GameState::tick_npcs` PATROL follows the graph (server-authoritative — NPC positions are already networked, so no protocol change). `ServerNPC::path_target` holds the current waypoint index; when a world has no path nodes it falls back to the legacy circular patrol. `RemovePathNode` also strips links referencing the removed name.
- AngelEd: `GameEngine.PathNode` tree leaf places one at the camera; `SelType::PATHNODE` select/delete/duplicate + properties (name/radius/next-list/loop); links and selected-node radius drawn as 3D overlays; `ExportToOzone`.

## Wind zones (WindZone)
- OZONE line: `WindZone minX minY minZ maxX maxY maxZ dirX dirY dirZ strength [freq]`. Client-only/cosmetic; not networked.
- `WindZoneNode` boxed in `PawnSystem` (`m_windZones`); loaded by `InjectOzoneEntities`; `PawnSystem::SampleWind(worldPos)` returns the combined wind at a point as `(dirX, dirZ, strength, frequency)`.
- Foliage opt-in: `wind=1` on a `Mesh.Static`/`Mesh.Skeletal` OZONE line sets `MeshObjectNode::windAffected`; the editor MESH properties panel exposes a `Wind Affected` checkbox.
- Rendering: `GameData/Shaders/Lights/Wind.vs` (a copy of `Lighting.vs` plus height-weighted sine sway, pairs with `LitFog.fs`) loaded into `OmegaTechData.WindShader`; `oz::SetWindUniforms` uploads `windParams/windTime/windBaseY/windHeight` per draw. `PawnSystem::DrawEntities(camera, litShader, windShader)` uses the wind shader **only** for wind-affected meshes — everything else keeps `Lighting.vs` untouched. Missing `Wind.vs` falls back to `Lighting.vs` (no sway).
- AngelEd: `WindZone` tree leaf places one at the camera; `SelType::WINDZONE` select/delete/duplicate + properties (size/direction/strength/frequency); boxes + direction arrows drawn as 3D overlays; `ExportToOzone`.

## Vertex-keyframe animation (Mesh.Skeletal `animfile=`)
- Authored **vertex-keyframe/morph** animation (not bone skeletal). Text `.ozanim` format (`Source/Package/Anim/OzAnimFormat.*`, raylib-free): `ozanim 1` / `clip "Name" fps 30 loop 1` / `key <t>` / sparse `v <index> dx dy dz` offsets added to the model's base vertices. Vertex indices are GLOBAL across the model.
- Runtime: `oz::AnimatedMesh` (`Source/Renderer/Mesh/AnimatedMesh.*`) samples the clip and CPU-uploads positions via `UpdateMeshBuffer(mesh, 0, ...)` (apply-pose-then-draw; never `UploadMesh` on a loaded mesh). `oz::MeshCache::GetAnimated(mesh, tex, animFile, ...)` (cache key includes the anim file); falls back to StaticMesh.
- `MeshObjectNode` fields `animFile`, `animSpeed`, `animPaused`, `editPose` (editor-only live pose). OZONE: `Mesh.Skeletal <path> x y z yaw [scale=] [tex=] [animfile=Global/Anims/x.ozanim] [speed=] [wind=1]`. `speed=` / `animfile=` parsed in `OzoneParser`, applied in `OzOzoneLoader`, exported in `AngelEd` `ExportToOzone`.
- Editor: toolbar **Anim** opens the Animation panel — clip list, New/Delete, FPS/loop, Play/Pause/Stop, timeline scrub; **Convert to Animated** (Mesh properties / Entity Properties) writes a default clip under `GameData/Global/Anims/`; **Edit Verts** enables vertex picking (click / Shift-add / Sel All), keyboard deform (U/J/H/K/Y/I move, O/L rotate; Ctrl+Z/Y undo/redo, tool-scoped) and **Add Key/Del Key** capture sparse offsets. `SelType::MESH` selection highlight + selection raycast are wired.
- Packaging: `build-data.ps1` packs `GameData/Global/Anims` → `System/Data/anims.ozpak` (`OZPK`). Playback is client-cosmetic (not networked).

## Weapons (b54+)
- Data-driven `.ozls` entities of type `weapon`: ranged (ProjectileNode; speed/spread/damage/lifetime; `magazine`/`reload_time` stats) or melee (`reach` stat, `on_swing`/`on_hit` actions).
- Key code: `Source/Script/LightningEntityManager.cpp` `FireSelectedWeapon()` (ammo/reload/cooldown dispatch); projectile sim in `Source/Pawn/OzPawnSystem.cpp` `SpawnProjectile`/`UpdateProjectiles` (client radius 1.5) and `Source/Server/GameState.cpp` `spawn_projectile`/`tick_projectiles` (server radius 2.0 - radii intentionally differ).
- Shipped weapons: `GameData/Global/gun/{Pistol_01,Rifle_01,etheral_waver,automag,flux_carbine,selenite_blade}`. `pistol_01`/`rifle_01` are ranged; `etheral_waver` is a melee sword. Map pickups use these names (`pickup pistol_01` / `pickup rifle_01` / `pickup etheral_waver`).
- **FBX art pipeline:** raylib can't load `.fbx`, so `tools/convert_fbx.ps1` runs FBX2glTF (auto-downloaded to `tools/FBX2glTF.exe`, gitignored) to make `.glb` beside each FBX, then `tools/merge_glb_anims.py` folds the separate `<Name>_Fire/_Reload/_BasePose` animation GLBs into `<Name>.glb` as named clips (Fire/Reload) and synthesizes an `Idle` hold clip from the bind pose. Both are idempotent; commit the `.glb` outputs.
- **Per-weapon projectiles:** weapon `stats` may set `projectile_mesh` (path to a model, e.g. `Projectiles.glb`), `projectile_submesh` (mesh index for the caliber), `projectile_scale`, `projectile_color=(r,g,b)`. `FireSelectedWeapon` stamps these onto the `ProjectileNode`; `PawnSystem::DrawProjectiles(camera, shader)` draws the submesh (`oz::Mesh::DrawSubmesh`) with a tracer. Client-side visual only.
- **First-person view-model:** `Source/Renderer/ViewModel.*` draws the selected weapon attached to the camera (client-only, in `Core.hpp DrawWorld`). Weapon `stats` keys: `viewmodel_mesh`, `viewmodel_texture`, `viewmodel_offset=(x,y,z)` (camera space, z forward), `viewmodel_rot=(x,y,z)`, `viewmodel_scale`, `recoil`. Clips `Idle`/`Fire`/`Reload` come from the merged GLB (`oz::SkeletalMesh`); one-shots return to Idle, plus a procedural recoil kick / reload dip. Firing is `Pawn/AngelPlayer/WeaponBehaviour.cpp FireWeapon()` and the R key trigger the clips.

## Editor state (verified as of b58)
- Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. Reads `System/AngelEd.ini`.
- Lit/Unlit/Wire toggle swaps material shaders; right-click context menu exists; collision CSG runs via `OzoneLoader::RebuildCollisionVolumes` per brush `csgOp` (render meshes are not carved). Export preserves `add`/`sub`/`intersect`.
- Tool modes (Cam/Move/Scale/Rotate) are wired in placement mode; Pawn-tree Weapons branch places weapon pickups; sound preview has volume + loop.
- Undo/redo: full-document History (Edit menu + Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z). Snapshots serialize the whole world via `ExportToOzone`; restore uses `OzoneLoader::LoadString(text, worldDir)` (tileset-aware) followed by `InjectOzoneEntities(OzoneLoader::Instance().GetEntities(), PawnSystem::Instance())`. Cleared on new/open. The animation tool keeps its own vertex-edit undo stack. Docs: `Wiki/Editor-Usage.md`.

## CI (.github/workflows/ci.yml)
- Runs on every push/PR; tags matching `b*` also create a GitHub Release. Release assets: `System-<tag>.zip` (full Windows bundle) plus per-binary downloads `AngelServ`/`AngelMaster`/`OzPack`/`Angels95` (`*-<tag>-{linux-x86_64,windows-x86_64.exe}`) and `AngelEd-<tag>-windows-x86_64.exe` (editor is Windows-only). `AngelServ`/`AngelMaster`/`OzPack` are self-contained; `Angels95`/`AngelEd` need the zip's DLLs+GameData.
- **Linux:** build raylib from source (cached) -> `make AngelServ` -> `make OTENGINE` -> `make ozpack` -> `make AngelMaster` -> smoke test with `timeout 3 ./AngelServ`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` -> build all 5 targets -> assemble System/ -> run `build-data.ps1` -> upload artifact. Note: CI compiles AngelEd with inline raw `g++` commands, NOT the `AngelEd/Makefile` - the two can drift.