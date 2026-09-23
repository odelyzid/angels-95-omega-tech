# Engine Roadmap — Gap Analysis & Priorities

Status as of **b55/b56**. This page is the living gap analysis for the Angels95 /
OmegaTech engine: what exists, what is missing compared with a conventional game
engine, and the prioritized plan to close the gaps. Each entry lists the
`file:line` evidence so work can start without re-auditing.

Legend: **[0]** security/correctness, **[1]** engine fundamentals,
**[2]** multiplayer completeness, **[3]** presentation, **[4]** tooling/UX.

---

## What the engine already has

- Server-authoritative world simulation on a dedicated server (`AngelServ`, no
  raylib dep) with raw-socket UDP networking (`Source/Network/Network.hpp`).
- CSG brush worlds in the OZONE format with a Win32 editor (`AngelEd`) and
  per-zone scripting (`.ozls`).
- Data-driven pawn/weapon/item systems via LightningScript entities
  (`Source/Script/`, `Source/Pawn/`).
- Package streaming for models/textures/sounds/music/worlds
  (`Source/Package/`).
- A working 3D forward renderer: CSG brushes, billboards, rlights
  directional/point/spot, particle effects, post-processing blits
  (`Source/Renderer/`).
- Build + test matrix (`make test`, CI on Linux + Windows).

---

## Gap matrix

### Rendering & content pipeline

| Gap | Detail | Evidence |
|---|---|---|
| **[3]** Shadows | `castShadow` flag parsed but never used by the light pass | `Source/Renderer/LitLightning.hpp:11-24` |
| **[3]** Materials | Diffuse-only; no normal/specular/PBR maps | `Source/Renderer/LitLightning.cpp:9-37` |
| **[3]** LOD / instancing | Not present; every renderable drawn every frame | `Source/OzOzoneLoader.cpp:972-983` |
| **[3]** Culling | No frustum/occlusion culling | `Source/OzOzoneLoader.cpp:972-983` |
| **[3]** Animations | Zero skeletal/skinned/keyframe/flipbook; NPCs are billboards; remote players are capsules | `Source/Main.cpp:217-240` |
| **[3]** View-model | No first-person weapon view-model | `Source/Main.cpp` (render loop) |
| **[3]** Post shaders | Toon/Sobel/Line loaded but never applied | `Source/Core.hpp:865,1826` |
| **[3]** Particles | 2D only, hard cap 50 | `Source/ParticleDemon/ParticleDemon.hpp:13-29` |
| **[1]** VSync/MSAA | Flags set *after* `InitWindow` — ineffective | `Source/Settings.hpp:80-87,200-212` |
| **[3]** Title video | MPEG1 opened but never drawn/freed (leak) | `Source/Core.hpp:850` |

### Physics & gameplay

| Gap | Detail | Evidence |
|---|---|---|
| **[1]** Collision is AABB-only | CSG brushes as AABBs; `MAX_SPLITS=64` | `Source/Physics/OzBsp.hpp:6-33` |
| **[1]** No collision APIs | No raycast, sphere/capsule cast, or query — gameplay can't reuse the BSP | `Source/Physics/` |
| **[1]** Position restore | Player slide solved by restoring pre-frame snapshot; no swept/continuous collision | `Source/Core.hpp:966-968` |
| **[0/1]** Parallel collision paths | WDL re-parsed every frame on the client; OZONE pre-baked — two CSG paths never unified | `Source/Core.hpp:1175-1503` |
| **[3]** Rigid bodies | No physics engine at all (no joints, no ragdolls) | — |
| **[3]** Navmesh/AI perception | NPCs use distance checks only; no pathfinding | `Source/Server/GameState.cpp:433-503` |
| **[3]** Input | No rebinding, no gamepad tuning, no mouse-look sensitivity setting | `Source/Input*` (none) |

### Audio & video

| Gap | Detail | Evidence |
|---|---|---|
| **[3]** Positional audio | Zero pan/attenuation calls; all sounds global | `Source/Audio/` |
| **[3]** Zone audio | `ambience_loop` / `sfx_on_enter` slots never wired; `ZONE_REVERB` only logs — no DSP | `Source/Core.hpp:1887-1891,2052-2058` |
| **[3]** Video | plmpeg present, title video unused | `Source/plmpeg/`, `Source/Core.hpp:850` |

### Networking & multiplayer

| Gap | Detail | Evidence |
|---|---|---|
| **[0/2]** Prediction/interpolation | NPCs snap at 2.5 Hz broadcast; no client interpolation; no remote-player prediction | `Source/Server/Server.cpp:1293-1343` |
| **[0/2]** Reliability | `sequence` is never set/checked (raw UDP, no acks) | valid everywhere; `Source/Network/Network.cpp` |
| **[2]** Entity replication | NPC/pickup identity is index-triples, no stable net IDs | `Source/Network/Network.hpp:127-136` |
| **[2]** Game modes | `levelinfo` parsed but unused; menu Deathmatch/CTF cosmetic; no scoreboards/timers | `Source/Server/Server.cpp:276-291`; `Source/Menu/TitleMenu.hpp` |
| **[2]** Respawn | No server-side death/respawn flow | `Source/Server/GameState.cpp:690-694` (damage only) |
| **[2]** Admin | No kick/ban/RCON; `COMMAND` unhandled | `Source/Server/Server.cpp:1129-1133` |
| **[1]** Ping | Measures time-since-connect, not RTT | `Source/Network/Network.cpp:623-630` |
| **[2]** Join UX | Join port hardcoded 27015; no LAN browser UI; discovery is server-only | `Source/Main.cpp:797`; `Source/Network/Network.cpp:706-740` |
| **[2]** Reconnect | None — dropped client must restart | `Source/Network/Network.cpp:608-611` |
| **[2]** Game stats | `save_player_data()`/`load_player_data()` declared but unimplemented | `Source/Server/GameState.hpp:332-333` |

### Engine core & persistence

| Gap | Detail | Evidence |
|---|---|---|
| **[1]** Fixed timestep | Variable `GetFrameTime()` everywhere; server tick `0.1f` also uncapped | `Source/Main.cpp`; `Source/Server/Server.cpp:1259-1355` |
| **[0]** Config persistence | INIs are templates never read at runtime; resolution/volume/FOV all in-memory | `Source/Settings.hpp`, `System/*.ini` |
| **[2]** Save UX | Client save drops stats (hotbar names only) | `Source/Script/LightningEntityManager.cpp:859-914` |
| **[4]** Profiling/telemetry | None | — |
| **[4]** Crash reporting | None (no crashpad/minidumps) | — |
| **[4]** Mod loading | None; GameData is baked at pack time | `Source/Package/` |
| **[4]** Streaming | No async asset loading; home screen scans disk at boot | `Source/Main.cpp` |
| **[3]** Cutscenes/sequences | None | — |
| **[4]** Localization | String literals throughout | — |

### Scripting (LightningScript)

| Gap | Detail | Evidence |
|---|---|---|
| **[2]** Timers/delays | No `wait`/timed actions; all script is synchronous | `Source/Script/LightningScriptContext.*` |
| **[2]** Data types | No arrays/strings; only flat fields | `Source/Script/LightningScriptParser.*` |
| **[2]** Functions/coroutines | No user functions or coroutines | `Source/Script/` |
| **[2]** Debugger | No breakpoints/step/trace | — |
| **[2]** Event bus | Actions fire ad-hoc; no publish/subscribe | `Source/Script/` |
| **[1]** `on_fire` stub | Weapon fire script action is a placeholder | `Source/Main.cpp:256` |

### Editor (AngelEd)

| Gap | Detail | Evidence |
|---|---|---|
| **[4]** Undo/redo | Not implemented (documented gap) | `Wiki/Editor-Usage.md` |
| **[4]** Multi-select / prefabs | Single-object ops only | `AngelEd/Source/Editor.hpp` |
| **[4]** Lightmap baking | none | — |
| **[4]** CI drift | CI builds editor with inline g++ commands, not the `AngelEd/Makefile` | `.github/workflows/ci.yml` |
| **[2]** Hardcoded pickups | `EditorPickupType` should map to LightningScript/pawn defs | `AngelEd/Source/Editor.hpp` |

### Engineering / CI

| Gap | Detail | Evidence |
|---|---|---|
| **[4]** `make test` in CI | CI only smoke-tests the server binary | `.github/workflows/ci.yml` |
| **[4]** Formatting/lint | No clang-format / clang-tidy / `.editorconfig` | — |
| **[4]** Sanitizers | No ASan/UBSan build variant | `Makefile` |

---

## Roadmap

### Tier 0 — security & correctness (RCE/OOB/trust bugs) — **in progress (b56)**

- [x] Roadmap doc (this page)
- [x] S6 — clamp world-list JSON to `MAX_MESSAGE_SIZE` on join
- [x] S1 — clamp client `SCENE_UPDATE`/`CHAT` payload reads
- [x] S2 — ownership + clamps on `PLAYER_HURT`/`PLAYER_KILL`; drop spoofed
  client `PICKUP_COLLECTED`; clamp `WEAPON_AMMO`
- [x] S3 — finite/teleport validation on `PLAYER_UPDATE`; server-authoritative
  relayed health
- [x] S4 — auth race: client confirms on first non-challenge message; retry
  sends `CLIENT_AUTH`; server re-acks re-auth; idempotent `add_player`
- [x] S5 — explicit `COMMAND`/`GAME_STATE` handling (warn + drop)
- [x] Test coverage for the above

### Tier 1 — engine fundamentals

- [ ] Real RTT ping (PING/PONG timestamp round-trip instead of time-since-connect)
- [ ] Enforced 10 Hz server tick + client fixed-timestep (accumulator)
- [ ] NPC interpolation on the client (smooth 2.5 Hz broadcasts)
- [ ] Position/state validation moved into `GameState` (single choke point)
- [ ] Client settings persistence (read a real config file; move VSync/MSAA to
  window-creation time)
- [ ] Frustum culling for renderables
- [ ] Unify WDL client parse-on-load vs OZONE bake; stop per-frame WDL reparsing

### Tier 2 — multiplayer completeness

- [ ] Server-authoritative death/respawn + timers; scoreboard UI
- [ ] `levelinfo` → real game modes (DM timers, score limits, friendly fire)
- [ ] Kick/ban + server console (implement `COMMAND`)
- [ ] Player roster + LAN browser UI; honor the join port field
- [ ] Stable entity replication IDs (replace index-triples)
- [ ] ACK/retry for critical messages (weapon fire, pickup collect)
- [ ] Client prediction + reconciliation for own player

### Tier 3 — presentation

- [ ] Shadow pass (directional + point)
- [ ] First-person weapon view-model
- [ ] Wire the title video (draw + free)
- [ ] Positional audio (Attenuation/pan) + zone ambience/`sfx_on_enter`
- [ ] 3D particles (raise/dimension the particle cap)
- [ ] Skeletal/flipbook animation for NPCs + remote players

### Tier 4 — tooling & UX

- [ ] Editor undo/redo; multi-select; prefab library
- [ ] CI runs `make test` and a game-state suite; build editor via its Makefile
- [ ] clang-format/clang-tidy + `.editorconfig`
- [ ] ASan/UBSan CI build variant
- [ ] Save-slot UX + full player-data persistence
- [ ] FOV/ADS sensitivity settings

---

## Known-truth corrections (claims that are only half-true)

- "LAN discovery works": the server announces on UDP 27100 via
  `NetworkDiscovery::update()` but there is **no client-side browser UI** and
  `NetworkDiscovery::parse_response()` is unused.
- "Dedicated server standalone works": it does for world/NPC simulation, but
  player damage is client-authoritative for world hazards (`PLAYER_HURT`) — now
  closed by Tier 0 ownership checks.
- "INI config is supported": `System/Angels95.ini` and `System/OzServer.ini`
  are build-time templates, **never read at runtime**. Client/server accept CLI
  flags only (`--world`, `--world-dir`, `--port`, `--http-port`, `--dir`).

---

*To add an item: file it under a tier, with `file:line` evidence, then tick it
off when landed. Keep this page the single source of truth for engine gaps.*