# OmegaTech Engine — Angels95 Edition

![Angels95 Title Splash](GameData/Global/Title/menu_heading.png)

Angels95 reimagines the OmegaTech Engine as a **multiplayer game world** — a persistent, server-authoritative realm where players explore partitioned worlds, collect power-ups, level up, and fight NPCs alongside other connected players.

Built on [raylib](https://www.raylib.com/) with PS1-inspired retro aesthetics and a custom WDL world format.

---

## Quick Links

| Topic | Wiki Page |
|---|---|
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
| 1–8 | Select slot |
| Mouse Wheel / Arrow Keys | Cycle slots |
| E | Collect nearby pickup |
| Tab | Toggle inventory overlay |
| Escape | Pause menu (Resume / Settings / Main Menu / Quit) |
| F11 | Toggle fullscreen |

---

## Server Hosting

The dedicated server (`AngelServ`) has **no raylib dependency** and runs on any Linux or Windows machine.

```
./AngelServ --port 27015 --http-port 8080 --dir GameData
```

| Flag | Default | Description |
|---|---|---|
| `--port` | `27015` | UDP game server port |
| `--http-port` | `8080` | HTTP map API (`GET /map?list`, `GET /map?name=X`) |
| `--dir` | `GameData` | Path to game data directory |

The server scans `GameData/Worlds/` for subdirectories containing `World.wdl`. LAN discovery on UDP `27100`.

---

## Changelog

### b53 — 2026-08-11
- **New map: Dust_Ravine** — Desert fortress complex with connected hallways, rooms, corridors, underground tunnels, and three distinct outdoor areas (West Approach, South Canyon, East Ruins Garden). Uses the Dessert_Dreams texture set. 24 pickups, 22 lights across multiple interior and exterior zones.
- **2x texture tiling on all wall brushes** — `BuildBox()` now applies 2x UV tiling to boxes with height >= 1.0, so the 32x32px wall stone texture doesn't look stretched across large faces. Applied globally (all OZONE maps benefit).
- **OZONE format extension: `texScaleU=N texScaleV=N` tokens** — Parser now supports per-brush texture UV scale/offset tokens (like `flags=N`), stored in `OzoneRenderable` and applied via `ApplyRenderableUV()`.
- **Fixed overlapping floor geometry in Dust_Ravine** — Removed redundant interior floor boxes at z=-0.25 that overlapped with the global ground plane (z=-0.5), preventing z-fighting.
- Files: `GameData/Worlds/Dust_Ravine/World.ozone`, `GameData/Worlds/Dust_Ravine/skyzone_dust.ozls`

### b52 — 2026-08-11
- **Fix: extreme low FPS / log spam** — `EngineBillboard` was recreating a mesh model (`GenMeshPlane` → `LoadModelFromMesh` → `DrawModelEx` → `UnloadModel`) every frame for every billboard entity (pawns, pickups, zones, emitters). Added a single cached model created at init and reused across all draws. Eliminates the per-frame VAO upload/download cycle logged as "VAO: [ID 36] Mesh uploaded successfully to VRAM (GPU)" / "Unloaded vertex array data from VRAM (GPU)".
- Files: `Source/Renderer/EngineBillboard.hpp`, `Source/Pawn/OzPawnSystem.cpp`

### b51 — 2026-07-30
- Phase 7: complete——fix: actually revert ALL inline globals (was failing silently), remove UpdateBounds from DrawWorld
- Fix: ScanWorlds fallback for multi-CWD + CWD diagnostics + compiled zones scan
- Fix: revert all inline globals back to regular (single-TU safe), fix WorldData path for Ozone loading
- Fix: remove EngineSaveLoad.cpp module, restore inline save/load in Core.hpp
- Fix: convert remaining static globals to inline for multi-TU compatibility
- Phase 1-7: crash fixes, server security, multiplayer UI, editor fixes, refactoring, tests

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [raylib](https://www.raylib.com/) — Ramon Santamaria
- [raygui](https://github.com/raysan5/raygui) — Immediate-mode GUI
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) — MPEG1 video playback
- [c99-raylib-video-player](https://github.com/WEREMSOFT/c99-raylib-vide-player) — Video integration
