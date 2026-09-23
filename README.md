# Angels95 — OmegaTech Engine

![Angels95 Title Splash](GameData/Global/Title/menu_heading.png)

Angels95 reimagines the OmegaTech Engine as a **multiplayer game world** — a
persistent, server-authoritative realm where players explore partitioned
worlds, collect power-ups, level up, and fight NPCs alongside other connected
players.

Built on [raylib](https://www.raylib.com/) 5.5 with PS1-inspired retro
aesthetics, a custom **OZONE** world format (with legacy **WDL** support), a
**LightningScript** entity-scripting language, a Win32-native level editor
(`AngelEd`), and a dedicated standalone server (`AngelServ`) with no raylib
dependency.

Current release: **b55**.

---

## Features

- **Dedicated server** — UDP multiplayer (`135`), HTTP map API, LAN discovery
  (UDP `27100`); runs headless on Linux or Windows.
- **CSG worlds** — `OZONE` primitive brush format (`add`/`sub`/`intersect`,
  box/cyl/sph/pyr/pln), per-brush `texScale*`/`texOffset*`/`texPath` and
  `name=` zone labels; legacy text `WDL` worlds also load.
- **LightningScript entities** — data-driven `.ozls` definitions for items,
  pickups, weapons, NPCs, and scripted sky/zone triggers (`on_enter`,
  `on_tick`, `on_collect`, …) with actions like `heal`, `damage`, `msg`,
  `consume`, `playerstat`, `spawn_pickup`, `spawn_pawn`.
- **Data-driven pawn system** — NPC defs in `GameData/Global/PawnDefs/*.cfg`
  with an IDLE/PATROL/CHASE/RETURN/DEAD state machine.
- **Weapons & combat** — ranged (projectiles, ammo/reload/magazine) and melee
  (`reach`, `on_swing`/`on_hit`) defined as `.ozls` entities.
- **AngelEd level editor** — native Win32 panels + raylib viewport, CSG brush
  ops, OZONE export, Lit/Unlit/Wire view modes.
- **Packaged assets** — `OzPack.exe` bundles models/scripts/textures/sounds/
  worlds into `.ozpak/.oztex/.ozsnd/.ozmux/.ozone` packages in `System/Data/`.

---

## Quick Links

| Topic | Page |
|---|---|
| Quick developer reference | [AGENTS.md](AGENTS.md) |
| Gameplay, controls, HUD, mechanics | [Core Game](Wiki/Core-Game.md) |
| Architecture, source tree, key classes | [Engine Overview](Wiki/Engine-Overview.md) |
| AngelEd editor panels and workflow | [Editor Usage](Wiki/Editor-Usage.md) |
| LightningScript scripting reference | [LightningScript](Wiki/LightningScript.md) |
| WDL / OZONE world format | [World Format](Wiki/World-Format-WDL.md) |
| Build instructions, prerequisites, CI | [Building](Wiki/Building.md) |

---

## Client Controls

| Key | Action |
|---|---|
| WASD | Movement (first-person) |
| Mouse | Look |
| Left Click | Fire weapon (slot 1) |
| 1–8 | Select hotbar slot |
| Mouse Wheel / Arrow Keys | Cycle slots |
| E | Collect nearby pickup |
| Tab | Toggle inventory overlay |
| Escape | Pause menu (Resume / Settings / Main Menu / Quit) |
| F11 | Toggle fullscreen |

---

## Running

### Server

The dedicated server has **no raylib dependency**:

```
./AngelServ --port 27015 --http-port 8080 --dir GameData
```

| Flag | Default | Description |
|---|---|---|
| `--port` | `27015` | UDP game server port |
| `--http-port` | `8080` | HTTP map API (`GET /map?list`, `GET /map?name=X`) |
| `--dir` | `GameData` | Path to game data directory |

LAN discovery on UDP `27100`. Worlds are scanned from `GameData/Worlds/`.

### Client

Launch the client from the repo root (it expects repo-relative asset paths) or
from a `System/` release folder:

```
./Angels95                # show home screen / load default world
./Angels95 --world Dust_Ravine
./Angels95 --world-dir GameData/Worlds
```

> Note: `System/Angels95.ini` and `System/OzServer.ini` are **templates written
> by the build scripts and never read at runtime** — the client and server
> accept no INI config. Server behavior is controlled entirely by CLI flags.

---

## Building

### Linux / macOS

raylib 5.5 must be installed system-wide (`/usr/local/lib/libraylib.a` —
`build.sh` installs it from source). Then:

```bash
make OTENGINE       # client -> Angels95
make AngelServ      # server, no raylib
make ozpack         # asset packer
make -j$(nproc)     # all three
```

### Windows

- **w64devkit:** `.\build-native-win.ps1` (requires `C:\raylib\w64devkit`).
- **MSYS2 / MINGW64:** `.\build.ps1` (auto-builds raylib 5.5 if missing).

Both assemble a playable `System/` folder. `build-data.ps1` drives `OzPack`
to create `.oz*` packages from `GameData/`.

### Targets

| Target | Build cmd | Depends on |
|---|---|---|
| `Angels95` client | `make OTENGINE` | raylib 5.5 |
| `AngelServ` server | `make AngelServ` | none (standalone sockets) |
| `AngelEd` editor | `make -C AngelEd` | raylib + Win32 (Windows only) |
| `OzPack` packer | `make ozpack` | none |

---

## Tests

```bash
make test   # builds + runs all suites, continues past failures
```

Standalone tests (no framework) land in the repo root as executables.
Suites: parser, context, registry, entity_manager, pawn_system, wdl_parser,
ozone_parser, network, game_state. Single suites: `make test_parser`, etc.

---

## Documentation & Repository Structure

- `Source/` — client core, renderer, network, server, script, pawn, physics,
  package, audio, video (`plmpeg`).
- `AngelEd/` — Win32 level editor.
- `GameData/` — worlds, pawn defs, items, guns, textures, sounds.
- `tests/` — standalone test executables.
- `Wiki/` — full engine documentation (see Quick Links above).

---

## Changelog

### b55 — 2026-09-23
- **LightningScript entity actions** — new opcode/actions for scripts:
  `msg`, `heal`, `damage`, `playerstat <name> <op> <value>`, `consume`,
  `spawn_pickup`; stat-resolver fallback with compound float ops.
- **Scripted content for Dust_Ravine** — consumables (Medkit, ManaTonic),
  a scripted quest pickup (PistonPart), a new weapon (flux_carbine, reusing
  automag assets), plus `acidpool` (DoT tick zone) and `arena` (one-shot
  NPC-spawn zone) skyzone defs and matching `zone name=` geometry.
- **AngelEd** — brush property apply/delete, native menu bar, environment
  settings slider fix; server network message coverage completed.
- **CombatFX** module added for effect handling.
- **CI overhaul** — raylib 5.5 built from source on the Windows runner
  (pacman package removed), Editor build synced with `AngelEd/Makefile`,
  Linux client fixed (Win32 chrome in `TitleMenu` guarded), `.res`/`windres`
  made Windows-only. Both Linux and Windows CI jobs now pass.
- Tests extended (`LightningScriptContext`, `OzoneParser`): all 9 suites green.

### b54 — 2026-08-21
- OZONE terrain render pipeline, sky zone isolation, script action boundaries.

### b53 — 2026-08-11
- **New map: Dust_Ravine** — desert fortress complex with connected hallways,
  rooms, corridors, underground tunnels, and three outdoor areas. 24 pickups,
  22 lights across multiple interior/exterior zones.
- **2x texture tiling on wall brushes**, `texScaleU=N texScaleV=N` OZONE
  tokens, fixed overlapping floor geometry z-fighting.

### b52 — 2026-08-11
- **Fix: extreme low FPS** — `EngineBillboard` was recreating its mesh model
  every frame; now cached and reused across all draws.

### b51 — 2026-07-30
- Crash fixes, server security, multiplayer UI, editor fixes, refactoring,
  tests; `inline` global refactor; `ScanWorlds` multi-CWD fallback.

---

## License

[MIT](LICENSE) — Copyright (c) 2026 TribeWarez.

See also [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md),
[CONTRIBUTING.md](CONTRIBUTING.md), and [SECURITY.md](SECURITY.md).

## Acknowledgments

- [raylib](https://www.raylib.com/) — Ramon Santamaria
- [raygui](https://github.com/raysan5/raygui) — Immediate-mode GUI
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) — MPEG1 video playback
- [c99-raylib-video-player](https://github.com/WEREMSOFT/c99-raylib-vide-player) — Video integration