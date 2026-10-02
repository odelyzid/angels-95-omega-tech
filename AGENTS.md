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
Suites: `test_parser`, `test_context`, `test_registry`, `test_entity_manager`, `test_pawn_system`, `test_ozone_parser`, `test_join_uri`, `test_master`, `test_network`, `test_game_state`, `test_ozanim`, `test_ozls_writer`, `test_autoconvex`.

- No test framework - standalone `tests/*.test.cpp` compiled directly. **Test executables land in repo root** (`./test_parser`, not `./tests/`).
- Most suites use `SERVER_CXX` + `-DOMEGA_TEST_ENV` (no raylib). **Exceptions:** `test_entity_manager` and `test_pawn_system` link raylib (Vector3/BoundingBox types).
- Single test targets: `make test_parser` / `make test_context` / `make test_registry` / `make test_ozone_parser` / `make test_join_uri` / `make test_master` / `make test_network` / `make test_game_state` / `make test_ozls_writer` / `make test_autoconvex` etc.
- **A new source file must be added in THREE places** or CI breaks silently: the root `Makefile` (compile rule + `OBJS` + link), `AngelEd/Makefile` if the editor needs it, and `.github/workflows/ci.yml`'s **inline `g++` list** — CI builds AngelEd by hand rather than via `AngelEd/Makefile`, so the two can drift. `OBJS` is now the OTENGINE link's single source of truth; it previously had a duplicate hand-maintained list in the `OTENGINE` prerequisite line and the two drifted, breaking both CI jobs (see `fcbf049`).
- CI runs `make worldcheck` but **not** `make test`, so a new test suite is not exercised by CI unless a step is added.

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

## Networked pickups (`PickupPawns` + `PawnSystem::ApplyPickupNetState`)
Three lists exist and confusing any two of them is what made pickups look broken:
1. the **server's** `ServerPickup` (authoritative, `GameState::m_worlds`),
2. the **client's shadow** `ClientPickup` (`OmegaClient::m_pickups`) — the client's view of the server,
3. the **drawn** `PickupNode` (`PawnSystem::m_pickups`) — what you actually see.
- **(2) drives (3), via `netId`.** `OmegaClient::set_on_pickups_changed` fires from a *copy* taken under `m_msg_mutex`; `Main.cpp` converts it and calls `PawnSystem::ApplyPickupNetState(net, world_index)`. Previously the collect flipped `active` on **(2)** only, so the mesh never vanished and every walk back re-requested an already-consumed pickup.
- **`PickupNode::netId` is the file-order index of the `pickup` line in `World.ozone`,** matching `seed_world_entities`' `nextPickupId++`. Both sides must keep enumerating the parsed primitives in file order or a collect targets the wrong physical pickup. `tests/GameState.test.cpp` pins the server side by re-reading the file independently.
- Matching is on `netId`, **not** `id` — `PickupNode::id` is a local handle from a counter shared with every other entity type.
- **`netId == -1` means "not the server's"** (e.g. a script `spawn_pickup`); `ApplyPickupNetState` leaves those alone rather than hiding a scripted pickup MP never owned. A drawn node with a `netId` but *no* server entry is left alone and **reported** — hiding on a partial snapshot is how a whole level disappears.
- **Nothing about a refused collect is silent.** `GameState::collect_pickup` returns a `PickupReject` (`UNKNOWN_PLAYER`/`WRONG_WORLD`/`UNKNOWN_WORLD`/`NOT_FOUND`/`OUT_OF_RANGE`), logged by the server, which then answers with a re-sync snapshot of the player's own world. Previously all four paths were a bare `return false` with no log and no reply, which turned the bug into a four-way guess.
- **Three ways state re-syncs:** the join snapshot, the server's 15 s per-player/own-world resync (UDP gives no delivery guarantee, so one lost `PICKUP_RESPAWN` used to strand a pickup permanently), and `PICKUP_RESYNC` (client-initiated, `PickupPawns` fires it when it has local pickups but no server state for the world).
- **`PickupRespawnData::typeName` is an OPTIONAL TRAILING FIELD.** Gate on `net::kPickupRespawnSizeBase`, never `sizeof()`. Requiring `sizeof()` made a client drop every message from a server predating the field — silently, a whole world's pickups at once. Same forward-compatible tail pattern as `ClientAuthPayload::structSize`; **no `PROTOCOL_VERSION` bump**, and `PICKUP_RESYNC = 26` is a new `MessageType` which is compatible by definition.
- **`g_network_world_index` must never be `-1`.** `-1` makes `PickupPawns`' `p.world_index != local_world` filter reject every pickup *and* the server's world check reject every collect, with nothing sent and nothing logged. A parse miss now keeps the previous value and warns.
- `OmegaClient::on_disconnected` clears `m_pickups`, `m_pending_collects` and `m_scores`; they are per-session server state.

## Entrypoints
- **Client:** `Source/Main.cpp` - `main()` after OmegaTechInit, splash, home screen, world loading, game loop. Flags: `--world <name>`, `--world-dir <path>`, `--join <ip[:port]>`; also accepts an `angels95://join/<ip>:<port>` positional arg (web-portal deep link) and auto-joins/skips the menu. Registers the `angels95://` OS handler on launch (`Source/ProtocolHandler.hpp`, HKCU on Windows / user .desktop on Linux); URI parsing is `Source/JoinUri.hpp`. **Screenshot mode** (see `Source/Screenshot.hpp`): `--shot <out.png>`, `--shot-delay <frames>` (default 90; needs >=2 so the skyzone `on_enter` uniforms land), `--shot-res <WxH>`, `--shot-cam "x,y,z,yaw[,pitch]"` (repeatable, **OZONE Z-up**, yaw/pitch in degrees; repeats produce `out_1.png`, `out_2.png`, ...), `--shot-hud` (keep HUD). Suppresses splash/menu/audio and forces vsync/MSAA/pixel/jitter/fog/head-bob/debug/FPS off, hides hotbar/HUD/crosshair/view-model, freezes the camera via `isNoClip`, and exits when every camera is captured. `GameData/Launch.conf` is skipped in shot mode so `--shot-res` is authoritative.
- **Server:** `Source/Server/Server.cpp` - `main(argc, argv)`. Flags: `--port` (27015), `--http-port` (8080, HTTP map API), `--dir` (GameData), `--bind` (default all interfaces; literal IPv4 or hostname, use `0.0.0.0` on a VPS), `--auth-token` (HTTP Bearer gate; env `OZ_AUTH_TOKEN`), `--admin-token` (enables COMMAND list/say/kick; env `OZ_ADMIN_TOKEN`), `--server-name`, `--master host[:port]` (repeatable), `--master-http URL` (repeatable), `--public-ip`. Loads `System/OzServer.ini` (`[Server]`/`[Auth]`/`[MasterServers]`) first; CLI/env override it. LAN discovery UDP 27100; internet heartbeat uplink to masters (30s, background thread). Worlds seed NPCs/pickups from `World.ozone` entities (procedural ring only as fallback); server saves in `GameData/Saves/` (autosave 60s + shutdown). VPS: `System/angels95-serv.service`.
- **Master server:** `Source/Server/Master/Master.cpp` - `main(argc, argv)`. Flags: `--port` (27900 UDP heartbeats), `--http-port` (27950 JSON list), `--max-servers`, `--gamename`, `--bind` (default 127.0.0.1; use `0.0.0.0` on a VPS). Serves `GET /api/servers?gamename=angels95`, `GET /api/stats`, `POST /api/heartbeat`. Entries expire after 90s. A self-reported `publicip` is only trusted when the heartbeat arrives from a non-private source (anti-spoof). Docs: `Wiki/Master-Server.md`; VPS unit: `System/angels95-serv.service`.
- **Editor:** `AngelEd/Source/Main.cpp` - `main(argc, argv)`. Win32 panels + raylib viewport. **Property-panel consolidation is implemented — see `Wiki/Editor-PropertyPanel-Refactor.md` for the plan, the progress log, the invariant register and the ruled-out dead ends.** The duplicate View-menu Zone/Light windows are gone (per-zone rows are the Entity Properties panel's Environment section, level-state rows are the WorldGraph `Map (level)` row), portal delete moved into the PORTAL section, and per-zone `envOverrides` now has both authoring and live preview. The manual checks listed at the bottom of that file are still outstanding.
- **Core engine:** `Source/Core.hpp` (~2400 lines, single header) - init, splash, menu, world loading, render loop, shaders.

## Source layout (condensed)
Full tree: `Wiki/Engine-Overview.md`. Key modules:
Full tree: `Wiki/Engine-Overview.md`. Key modules:
- Rendering/loop: `Source/Main.cpp`, `Source/Core.hpp`, `Source/Renderer/` (LitLightning `LitLightning_UpdateFrame` owns the per-frame lighting pass, EngineBillboard, TextSystem), `Source/Renderer/raygui/`, `Source/Renderer/rlights/`, `System/Shaders/`.
- Networking: `Source/Server/Network/Network.cpp/.hpp` (UDP, `#pragma pack(push,1)`), `Source/Client/Client.cpp`, `Source/Server/Master/` (AngelMaster daemon, `MasterClient` uplink, `MasterProtocol`/`MasterHttp`/`MasterList` helpers), `Source/Menu/InternetBrowser.hpp` (client browser).
- Server: `Source/Server/` (Server.cpp, GameState, OzoneParser).
- Script/entities: `Source/Script/` (LightningScript parser/context; EntityRegistry scans `*.ozls` in GameData + packages; EntityManager; `GameUI` EntityType is a data+hook-only declarative HUD layer).
- Player: `Source/Pawn/AngelPlayer/` (GameUi bridge, **SlotBar** shared HUD bar renderer, InventoryBehaviour HUD/overlay + item/weapon collect hooks, WeaponBehaviour fire/recoil/ADS, PlayerController toggles/vertical/game-over) and `Source/Pawn/PickupPawns.*` (networked-pickup collect loop only).
- Pawn/world: `Source/Pawn/` (OzPawnSystem, Items/Entities/Objects/Player, PickupPawns), `Source/Physics/` (OzBsp, **AutoConvex**, WorldChunk, PlayerPhysics + PhysicsInfo), `Source/World/` (OzoneParser, OzOzoneLoader, OzoneFrustum, OzoneHeightmap, **ZoneManager**, **ZoneTypes**, **LevelSettings**).
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
  - **`sub`/`intersect` are modifiers, not shapes.** `CsgProcessor` (`Source/Physics/OzBsp.hpp`) subtracts from / intersects with solids that **already exist**. A world whose only brush is `sub box ...` has **zero** collision volumes while its render mesh still draws - it looks like a floor and the player falls straight through it. `GameData/Worlds/TestMap/World.ozone` shipped exactly this bug. Author floors as `add`.
- Surface flags: `SURF_FAKEBACKDROP` (`1<<3`, zone backdrop) and `SURF_COLLISION_PROXY` (`1<<4`, generated AutoConvex box - collision only, **never drawn by `DrawWorldGeometry`**). Both round-trip through the ordinary `flags=` kwarg.
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
- **Data-driven sound stats** (`.ozls`): `PlayStatSound(path, volume, pitch)` is the ONE entry point for any stat-driven one-shot — do **not** add per-stat `Play*` methods; it shares the script-sound cache so it is package-aware and a `.wav` used by both a `play_sound` opcode and a `fire_sound` stat loads once. `StartLoopSound(key, ...)`/`StopLoopSound(key)`/`IsLoopPlaying(key)` are the **keyed** loop API; the old single `WalkingSound` handle could not express two interchangeable loops (walk/run). Loops are re-triggered from `Update()` because raylib drains a `PlaySound`'d stream, and `ResetWorldAudio()` releases them or they outlive a world load. The preloaded `GameSounds` handles stay the **fallback layer** for unauthored keys.
- Weapon keys: `fire_sound`, `swing_sound`, `hit_sound`, `reload_sound`, `equip_sound` + `_volume` (default 1.0) / `_pitch`. Player keys on `Player.ozls`: `jump_sound`, `land_sound`, `walk_sound`, `run_sound`, `hurt_sound`, `death_sound` + `_volume`. **Author them quote-free and space-free** — the stats parser stores a string verbatim, does not strip quotes, and stops at the first space, so a quoted or spaced path silently never resolves.
- **Fire/swing audio must stay behind the `result < 0` gate** in `WeaponBehaviour::FireWeapon`. `FireSelectedWeapon` returns `-1` when it refuses to shoot — on cooldown, out of stamina, or when it silently auto-reloads — so playing earlier fires a gunshot on every click of an empty magazine.
- `hit_sound` fires inside the `!blocked` melee branch only: a swing that stops at a wall takes the else-branch, which deliberately runs no `on_hit`, so it must not make a connect sound either.
- **Fallbacks are asymmetric on purpose.** `fire_sound`/`equip_sound` fall back to a global handle so an unauthored weapon still shoots; `swing_sound`/`hit_sound`/`reload_sound`/`land_sound` do **not**, because there is no honest generic clip for "a blade connected" and borrowing one makes every swing in every level identical.
- **Hurt is split from death.** Both hurt paths (network `PLAYER_HURT` in `Main.cpp`, script `damage` opcode in `Core.hpp`) route through `PlayerController::PlayHurt(fatal)`. `PLAYER_HURT` fires on *every* point of damage, so routing both to `PlayDeath()` made a firefight sound like a montage of deaths.
- `WeaponBehaviour::SelectedWeaponStat` reads `runtimeStats` **before** `def->stats.floats`, matching `FireSelectedWeapon`'s `readStat` lambda. It used to read the def only, so a script-set override was invisible to recoil/damage while the projectile path saw the overridden value.

## `.ozls` writer (Script/OzlsWriter)
- `Source/Script/OzlsWriter.{hpp,cpp}` is the write counterpart to `LightningScriptParser`. **Raylib-free** on purpose: AngelEd links it and it runs in the headless test harness.
- **Patch, do not regenerate.** `PatchOzlsStats(path, edits)` rewrites individual `key = value` lines inside the existing `stats { ... }` span. The tempting alternative (parse → mutate → re-serialise) is simpler but destroys every comment, the key order, and any key the parser only warned about — `reverb_mix`/`reverb_decay` in the shipped zone defs are exactly that. An edit from the Property Window must be a one-line diff, so `tests/OzlsWriter.test.cpp` asserts the *untouched* content byte-for-byte.
- `SerializeEntityDef` is only for a file with **no** `stats` block (a new def) and for the round-trip test. It cannot preserve comments because it never sees them. Output is sorted (from `unordered_map`) so a save does not reshuffle itself.
- Empty `StatEdit::value` means **erase**. An erase against a file with no `stats` block is rejected rather than fabricating an empty block to hold the deletion.
- The block scan must ignore a `}` that appears **after an `=`** on a line — `stats` lines are flat assignments, so that brace belongs to the value. Without the check the block ends early and an edit appends a duplicate key instead of updating in place. Braces inside quotes are also skipped.
- **Build wiring is three places:** root `Makefile` (`OBJS` + `test_ozls_writer`), `AngelEd/Makefile` (`CORE_OBJS`), and `.github/workflows/ci.yml`'s inline `g++` list — CI compiles the editor by hand rather than via the Makefile, so missing it fails the link there only.
- Known pre-existing bug (not in scope here): **`LightningScriptParser` segfaults on an unquoted `}` inside a stats value** — it walks past the end of the content. Reachable from any `.ozls` an author writes. The OzlsWriter brace tests assert on written text and deliberately do not re-parse.
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

## Backface culling — one tracked flag (`Source/Renderer/CullState.hpp`)
- **`Source/Renderer/CullState.hpp` is the ONLY place allowed to touch culling.** `oz::SetBackfaceCulling(bool)` is the setter; `oz::ScopedCullOff` is the RAII guard. Nobody calls `rlEnable/rlDisableBackfaceCulling()` directly. Header-only + inline on purpose: a `.cpp` would mean editing the root `Makefile`, `AngelEd/Makefile` AND the inline g++ list in CI.
- **Why it exists:** three modules each toggled culling with raw rlgl calls and each remembered the result in its own private bool. `Core.hpp` turned it ON after the skybox and never turned it off; `SurfaceMaterial` tracked `m_cullOff`; AngelEd set it from inside `if (s_skyTex.id > 0)`, so a world with no skybox never set it at all and inherited the previous frame. Restores then restored the wrong thing.
- **The visible symptom was "some models are missing".** `tools/convert_fbx.ps1` bakes a Z-up -> Y-up rotation into the character/pawn/weapon GLBs, which flips triangle winding on a lot of them; drawn culled, such a model is *completely invisible*. Generated OZONE brushes keep correct winding, so they were fine — which is exactly why it read as a content bug and not a renderer bug. It silently affected the player character, the view-model, `Mesh.*` props and pickup models.
- **Rule:** imported assets opt OUT (`oz::Mesh::Draw`/`DrawSubmesh`/`DrawMatrix` each wrap in `ScopedCullOff`); generated world geometry stays culled.
- **The editor passes its intent in:** `OzoneLoader::Draw(camera, cullBackfaces = true)` APPLIES the value through the tracked setter rather than inheriting it. AngelEd passes `ViewMode != LightingMode::WIREFRAME`. It is a parameter, not an include of `LightingMode`, because `OzOzoneLoader` also builds into `worldcheck` and the headless tests.

## Player character model (`Source/Renderer/PlayerModel.*`, `GameData/Global/Objects/Player.ozls`)
- One shared asset for every remote player, resolved through an **ordered fallback chain**, first that loads wins, and the winner is logged: (1) the `Player` def's `mesh`/`texture` — the authoring hook; (2) `GameData/Global/Player/Plague_Arcanist.glb` — the guaranteed default (rigged, embedded textures, nothing external to lose); (3) `Character_Killer_01.glb` + `.png`; (4) a primitive capsule placeholder.
- **A failed resolve must NOT latch.** `m_tried` gates only a *success*; failure backs off 2 s and retries. The old code latched on the first call regardless of outcome, so one unlucky early call (before the `.ozls` defs registered, or before a package hot-loaded) pinned the whole session to the capsule. `Invalidate()` resets it and is called from `LoadWorld()`, because `LightningEntityRegistry::LoadWorldOverrides` re-points the def map on every world load. (`MeshCache` caches negative results as `nullptr`, so a retry can heal a late *def* — a different cache key — but not a file that was merely missing from disk.)
- **Pass the real lit shader.** `DrawRemotePlayers3D(Shader)` takes `OmegaTechData.Lights`. It used to pass `{0}`, and `oz::Mesh::Draw` only falls back to the model's own materials when `litShader.id == 0` — so remote players were drawn outside the game's lighting pass and read as unlit/invisible in a dim room while everything else was lit.
- **`model_height` stat** (in `kPlayerStats` for the editor panel) sets the rendered height. The character GLBs are authored in metres while `PlayerMovement::Height` is 3.0, so unscaled they draw at ~half the collision silhouette. Positive re-normalises; **negative means "authored scale, do not normalise"**.
- `mesh_type = "static"` on purpose — the Arcanist GLB's 10 clips are unnamed mixamo takes. Rename them (editor Anim panel) and set `anim_idle`/`anim_chase` to switch to skeletal.

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
- **Weapon sounds are `.ozls` stats**, not hardcoded: `fire_sound`/`swing_sound` in `WeaponBehaviour::FireWeapon`, `hit_sound` + `reload_sound` + `equip_sound` in `LightningEntityManager` via `PlayDefStatSound`. See the SoundManager section for the full key list and the authoring traps.

## Editor state
- Win32 native panels + raylib viewport. Dynamic file scanning of `GameData/` + packages. Reads `System/AngelEd.ini`.
- Lit/Unlit/Wire toggle swaps material shaders; right-click context menu exists; collision CSG runs via `OzoneLoader::RebuildCollisionVolumes` per brush `csgOp` (render meshes are not carved). Export preserves `add`/`sub`/`intersect`. See the CSG/AutoConvex section below.
- Tool modes (Cam/Move/Scale/Rotate) are wired in placement mode; Pawn-tree Weapons branch places weapon pickups; sound preview has volume + loop.
- Undo/redo: full-document History (Edit menu + Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z). Snapshots serialize the whole world via `ExportToOzone`; restore uses `OzoneLoader::LoadString(text, worldDir)` (tileset-aware) followed by `InjectOzoneEntities(OzoneLoader::Instance().GetEntities(), PawnSystem::Instance())`. Cleared on new/open. The animation tool keeps its own vertex-edit undo stack. Docs: `Wiki/Editor-Usage.md`.
- **The Entity Properties panel edits `.ozls` stats.** An `Edit stats` section is generated from a per-entity-type schema (`kWeaponStats`/`kPlayerStats`/`kPawnStats`/`kLightStats` in `Win32Dialogs.cpp`) — from the SCHEMA, not the authored keys, so a weapon shows stats it has never set. Apply writes only *changed* rows via `ozls::PatchOzlsStats`; a blank unauthored row is not an erase, so Apply can never strip a def. Rows have **per-row** control IDs (`ID_PP_STAT_FIELD_0 + i`) rather than shared Preview/Browse plus a cursor hit-test, which would drift from the layout.
- **The read-only dump shows only keys the schema does NOT know about.** `FillDefBlock` filters `propDefFields` against the keys it just pushed into `propDefEditable`. Repeating schema keys in both sections doubled the panel height and pushed Apply off the bottom of the screen. Nothing is hidden: a schema key is shown, just in the editable row.
- **The Properties panel scrolls** (`PropsCaptureRows`/`PropsApplyScroll`/`PropsUpdateScroll`/`PropsFitWindow` in `Win32Dialogs.cpp`). A weapon def is ~30 rows taller than any screen, so the window is capped to the monitor work area and each child is moved by `-scrollPos`; the texture grid scrolls by painting instead because it owns no children. `PropsUpdateScroll` MUST run *after* the resize — `nPage` comes from the client height, so choosing `WS_VSCROLL` first and resizing afterwards leaves a panel that fits before the resize, does not fit after, and has no bar to scroll with. `propsPanelPos.w` went 470 -> 540 so the scrollbar does not steal value-column width.
- `FillDefBlock()` is file scope in `Win32Dialogs.cpp`, not a lambda in `ShowPropertiesPanel` — the Script Manager's **Properties** button needs it too, and that is the only route to a def with no world instance (`Player.ozls`). `ShowDefPropertiesFor(defName)` opens the panel on a def by name and leaves `propsTargetType = -1` so the per-instance position/rotation rows are skipped.
- A def resolved from a `.oz*` **package** has no writable source, so its stat rows render read-only with an explanation rather than accepting edits that cannot be saved.
- **Which `propsTargetType` reaches a def** (`ShowPropertiesPanel`). The enum is `SelType` in `Main.cpp`, and a **missing branch means no def section at all** — that is how `SPAWN` (7) and `LIGHT` (5) lost access to their `.ozls` stats. Current routing: `3` NPC → `defName`; `4` PICKUP → `typeName`; `6` ZONE → `zone.name`; `7` SPAWN → the fixed name `"Player"` (`playerstart` carries no name, so it is resolved by constant); `5` LIGHT → `light.name`. Everything else is instance fields only.

## CSG + AutoConvex collision
- **The collision world is AABB-only.** `CsgProcessor` (`Source/Physics/OzBsp.hpp`) consumes nothing else, which is why a placed `Mesh.*` prop has no collision and a concave brush over-blocks. `RebuildCollisionVolumes` only ever turns a renderable's mesh AABB into a `CsgBrush`.
- `Source/Physics/AutoConvex.{hpp,cpp}` is the raylib-free voxeliser: world-space float vertex soup → AABB → grid → keep cells a triangle passes through → `ConvexBox` list. Covered by `tests/AutoConvex.test.cpp` (`make test_autoconvex`).
  - **SAT with all 12 axes** (3 box faces + 9 `cross(triEdge, boxAxis)`). Do *not* "optimise" this to Akenine-Möller's 3+3 form: that variant depends on a vertex reordering into the positive octant and is easy to get subtly wrong in a way that silently rejects *everything*.
  - **`vertFloatCount` counts FLOATS, not vertices.** An earlier `vertexCount` that was really a float count made both the bounds loop and the triangle loop index 3x past the end of the buffer, reading uninitialised memory — it showed up as proxies *inside* solid geometry and as counts that changed between runs. The parameter is named for its unit on purpose.
  - Voxelising a **surface** yields a closed shell, not a filled volume (the 2x2x2 box at 0.5 cells gives 56 cells, not 64). That is correct for collision.
  - A **zero-thickness** axis gets exactly one cell *centred* on the geometry. `floor`/`ceil` of an equal min and max is zero cells, which silently returned nothing and left planes/card meshes/flat brushes with no collision.
  - `maxBoxes` **aborts the whole build** rather than truncating. A partial hull is the exact failure the feature exists to prevent.
- `OzoneLoader::AppendAutoConvexCollision(verts, vertFloatCount, cellSize, maxBoxes)` appends each box via `AddBrushRenderable(..., CsgOp::SOLID, SURF_COLLISION_PROXY)` then calls `RebuildCollisionVolumes()`. `AddBrushRenderable` gained an optional trailing `surfaceFlags = 0`.
- **Visibility:** `DrawWorldGeometry` skips `SURF_COLLISION_PROXY` unconditionally. `Draw` (the editor's all-renderables path) skips it unless `OzoneLoader::SetDrawCollisionProxies(true)`, which the stats sidebar's **View → Collision** button drives. The dead `CollisionToggle` global in `Editor.hpp` was removed in favour of `EditorPanelState::showCollisionBounds` — two flags for one toggle is how they drift.
- AngelEd entry point: `AppendAutoConvexForSelection()` in `Main.cpp`, from the right-click menu (`IDM_APPEND_AUTOCONVEX`, gated to `SelType::MESH`/`BRUSH`). Cell size is `max(0.25, extent/16)`; budget `kAutoConvexMaxBoxes = 2048`. Skeletal meshes voxelise in their **bind pose** (poses are uploaded per frame at draw time, so no posed geometry exists to sample).
- **Both brush-commit paths go through `CommitBrushRenderable()`** (Enter-key ghost and the sidebar op buttons). It snapshots the collision-volume count, rebuilds, and if a `SUB`/`DE_RESC`/`INTERSECT` op produced no new volume it logs loudly, raises a `MessageBoxA`, and sets `EditorPanelState::collisionOpWarning` (the sidebar counter then reads `Collision: N vols << NO SOLID` and the wireframes turn red). No behaviour change — a lone `sub` is *not* auto-promoted to `SOLID`.

## Light `.ozls` defs
- `EntityType::LIGHT` (`: light`) is a **defaults layer**, resolved by the light's existing `name=` kwarg — the same lookup a zone's `name=` uses for its skyzone def, so no new OZONE kwarg and no format change. Reference def: `GameData/Global/Lights/Light.ozls`.
- `ApplyLightDefDefaults(node, prim)` in `OzOzoneLoader.cpp` runs in the `ENTITY_LIGHT` branch of `ParseOzoneEntity` after the positional/kwarg decode.
- **A value the light line authored always wins.** Only `effect`/`flare`/`corona` (and only when `prim.light* < 0`, i.e. the line omitted the kwarg) plus `period`, `cast_shadow`, `is_static`, `inner_cone`, `outer_cone` take effect.
- **`intensity` / `radius` / `color` / `position` / `target` are deliberately line-owned** and are not in `kLightStats`. They are positional on every light line, so a def able to override them would silently retune every already-saved level the next time the def was touched — the exact hazard the light positional/kwarg split was added to avoid.
- `inner_cone`/`outer_cone` are **cosines**, matching `LightNode` (not degrees).
- Clamp `effect` into `[NONE, LAMP]` rather than casting the float straight to the enum — a def is data, and a bad value in it must not reach memory as bits.
- `ApplyLightDefDefaultsToNode(node)` is the **code-built** entry point (the editor's `GameEngine.Light` placement). The OZONE path uses the file-static version, which needs the primitive to know which optional fields the line authored; a node built in code authored none, so every def-owned field applies. Editing `Light.Spot.ozls` must change both newly placed and loaded lights.

## Per-face surface properties (UT99 style)
- **Data model:** `Source/World/SurfaceFlags.{hpp,cpp}` — `oz::surface::{SurfaceProps, BrushSurface, SurfaceFace}`. **Raylib-free on purpose** (plain floats, no Color/Vector3) so `worldcheck`, `OzoneParser` and `tests/` link it. Same discipline as `ZoneTypes.hpp` / `GameType.hpp`.
- **A brush's surface is PER FACE**, not per brush: a brush-wide `def` plus up to six `face[]` overrides. Precedence is **face override → brush default → engine default** (the same "a value the author set always wins" rule the light defs use).
- **Faces are named in engine Y-up** (`px nx py ny pz nz`) and are **not** swapped for OZONE's Z-up — they are a renderer concept, not world coordinates.
- **`FaceFromNormal()` buckets by DOMINANT axis, deterministically** (ties resolve X→Y→Z). Both the editor's picker and the renderer's mesh split use it, so the face the user clicks is provably the face that gets edited. Do not depend on a primitive generator's vertex ordering.
- **Flags are `inline constexpr uint32_t` in `oz::surface`, re-exported unqualified via `using` at the bottom of the header — NOT macros.** They were `#define SURF_FAKEBACKDROP` / `SURF_COLLISION_PROXY` in `OzOzoneLoader.hpp`; a macro of the same name blocks any scoped declaration that mentions it, which is what stopped the flag set existing. `tests/Surface.test.cpp` asserts the two legacy bits never move.
- **Bit values are duplicated as `#define SF_*` in `GameData/Shaders/Surface.fs`.** Renumber a bit in the header and you MUST change the shader too, or the flag silently does nothing.
- **The legacy `OzonePrimitive::surfaceFlags` / `texScaleU/V` / `texOffsetU/V` / `texPath` fields are now DERIVED views** onto `BrushSurface::def` by `DeriveLegacySurfaceFields()`. `DrawWorldGeometry`, `DrawZoneGeometry`, `ApplyRenderableUV` and `ExportToOzone` still read them, and the shipped worlds rely on `flags=8` backdrops — do not write them from the tokenizer again, or a kwarg can be half-applied.
- **`surface.def.flags` OWNS the flags; `OzoneRenderable::surfaceFlags` is a derived mirror** of `oz::surface::kLegacyPipelineFlags`. Every writer must call `oz::surface::DeriveLegacyFlags()` afterwards. This is not bookkeeping — **`ExportToOzone` used to emit `flags=` only when `def.flags != surfaceFlags`, i.e. when the owner disagreed with its own mirror, which after a load is never.** So `flags=` was never written, and re-exporting a level silently deleted every painted backdrop and every AutoConvex collision proxy in it (TestMap lost all 230). `oz::surface::NeedsFlagsKwarg(defFlags)` is the honest predicate and is what the exporter now calls. `AddBrushRenderable` also used to assign `surfaceFlags` directly — the opposite direction from every other writer — so a freshly appended proxy exported a literal `flags=0`; it now sets `surface.def.flags` and derives. Pinned by `tests/Surface.test.cpp` (derivation + kwarg decision) and `tests/OzoneParser.test.cpp` (`flags=16` sets both fields, a plain brush sets neither).
- **`SurfaceMaterial` (`Source/Renderer/`) owns a SECOND shader program** (`GameData/Shaders/Surface.{vs,fs}`) rather than adding uniforms to `LitFog`. `Lighting.vs`/`LitFog.fs` drives every mesh, particle, pickup, projectile and the view-model; branching inside it would change the whole game's output. `Surface.vs` is a deliberate copy of `Lighting.vs` (same as `Wind.vs`).
- **Surface properties are UNIFORMS, i.e. per draw call**, so a decorated brush is drawn as up to six `DrawMesh` calls. `OzoneRenderable::faceMesh[6]` holds lazily built sub-meshes (`BuildFaceMeshes`); they own their own index buffers, so **`UnloadModel` does not free them** — `RemoveRenderable`, `Unload` and `UpdateBrushRenderable` must all `UnloadMesh` them.
- **A brush with no surface flags keeps the original single `DrawModel` path** (`NeedsPerFaceDraw()`), so the common case costs nothing. `DrawRenderable()` is the shared router; the fast path and the surface path must not drift.
- **Per-frame:** `SurfaceMaterial::UpdateFrame()` reuses `LitLightning_Update` rather than duplicating the 32-light loop — two copies would drift and a surface brush would disagree with the room around it. It is called from `Core.hpp UpdateLightSources` (after `LitLightning_UpdateFrame`) and from the editor's equivalent, because the program carries its OWN copy of `lights[]`/`viewPos`/`ambient`.
- **Glow** is two things: the emissive term is in `Surface.fs` (added *after* lighting, so a sign stays bright in an unlit room), and `OzoneLoader::DrawGlowGeometry` re-draws `SURF_GLOW` faces additively after the world pass for the halo.
- **`SURF_FAKE_LIT`/`UNLIT` replace the old `DrawZoneGeometry` ambient hack.** That function mutated the shared `LitFog` `colDiffuse`/`ambient` and restored a hardcoded `1.0`, but it runs at `Core.hpp` *before* `DrawWorldGeometry` in the same frame, so every world surface in a sky zone rendered at `ambient/10 == 0.1`. Per-draw uniforms end the leak. `SetWorldAmbient()` exists because raylib 6.0 has no `GetShaderValue`.
- **`SurfaceMaterial` carries its OWN `ambient` AND `fogStart/fogEnd/fogDensity/fogColor/fogIntensity`.** `CacheLocations()` used to cache no fog locations at all, so surface-flagged brushes always fogged at the GLSL defaults (10/100, density 1) regardless of the level's fog; and `UpdateFrame` hardcoded `amb = {0.1,0.1,0.1,1}` while its comment claimed the value lived on `OzoneLoader` — there was no accessor and it read nothing. `UpdateFrame(lights, camera, dt, ambient)` now takes ambient as a parameter and `SetFog()` mirrors the five fog uniforms (applied lazily, one upload per change). **`OzoneLoader` owns both** (`SetWorldAmbient`/`GetWorldAmbient`, `SetWorldFog`/`GetWorldFog`) because `Core.hpp` sets fog on `LitFog` from **three** sites (startup, zone entry, zone exit); `UpdateLightSources()` is the single mirror point instead of three that can drift. Both callers (`Source/Core.hpp`, `AngelEd/Source/Main.cpp`) read back from `OzoneLoader`.
- **Editor UX:** right-click a face → **Surface Properties (N Selected)** opens a `Flags` / `Alignment` / `Stats` dialog (`SurfacePropsProc`). **Shift**-right-click adds faces to the selection; `Apply` writes to all of them, `Reset Surface` drops their overrides. Picking is triangle-level (Möller–Trumbore in `Main.cpp`), not AABB-level, and the local→world transform must match `DrawSurface` or a rotated brush reports the wrong face.
- **`ApplyToSelection` is a no-op on an empty mask** — never let it mean "apply to all six faces".
- **Authoring:** brush-wide `flags=` / `surfTexSlot=` / `surfTex=` / `uvScale*` / `pan=` / `surfAlpha=` / `surfCutoff=` / `surfGlow=`; per-face `face<name>_<field>=`. Only overridden faces are exported, so a save is a small diff. `texSlot` is now BOTH a keyword (wins) and a legacy positional arg 8/9/7.
- **`skybox <tex> cx cy cz size`** is a render-only primitive: six inward-facing quads with direction-projected UVs, forced `UNLIT + TWO_SIDED + NO_FOG + NO_BSP_CUTS`, and **filtered out of the CSG collision pass** — feeding it to CSG would put an invisible wall around the player and let a `sub` erase the sky.
- `ApplyRenderableUV` recomputes from `OzoneRenderable::uvBase` (the pristine generated UVs). The old scheme divided by `texScaleU` to "undo" the last edit, but that field was seeded with the baked tiling for `BOX` only, so cyl/sph/pyr compounded on every edit and a box round-tripped to 256x.
- **A read-only properties-panel row MUST have no control ID.** `Apply`'s stat-edit scan reads each editable row by `controlId` via `readStatRowText()`, which returns `""` for a missing control - so an ID-less row would compare unequal to its authored value and be queued as an **erase**. This is why `FillDefBlock` filling `propDefEditable` for a *packaged* def also required gating the Apply scan on `propDefWritable`; otherwise the first Apply strips the def's own stats. The same trap applies to any new row type added to the panel.

## CI (.github/workflows/ci.yml)
- Runs on every push/PR; tags matching `b*` also create a GitHub Release. Release assets: `System-<tag>.zip` (full Windows bundle) plus per-binary downloads `AngelServ`/`AngelMaster`/`OzPack`/`Angels95` (`*-<tag>-{linux-x86_64,windows-x86_64.exe}`) and `AngelEd-<tag>-windows-x86_64.exe` (editor is Windows-only). `AngelServ`/`AngelMaster`/`OzPack` are self-contained; `Angels95`/`AngelEd` need the zip's DLLs+GameData.
- **Linux:** build raylib from source (cached) -> `make AngelServ` -> `make OTENGINE` -> `make ozpack` -> `make AngelMaster` -> smoke test with `timeout 3 ./AngelServ`.
- **Windows (MSYS2):** `pacman -S mingw-w64-x86_64-{gcc,make,raylib}` -> build all 5 targets -> assemble System/ -> run `build-data.ps1` -> upload artifact. Note: CI compiles AngelEd with inline raw `g++` commands, NOT the `AngelEd/Makefile` - the two can drift.