# CSG Implementation Plan — Angels95 / OmegaTech

## Status Overview

| Phase | Feature | Status |
|---|---|---|
| 1 | OZONE primitive collision | ✅ Complete |
| 2 | Heightmap as OZONE element | ✅ Complete |
| 3 | AABB CSG brushes (add/sub/intersect/deresc) | ✅ Complete |
| 4 | SkyZoneInfo skybox rendering | ✅ Complete |
| 5 | Editor UI for CSG + Heightmap | ⏳ Pending |
| 6+7 | Server sync + Large-scale partitioning/chunking | ⏳ Pending |
| 8 | CSG overflow protection | ⏳ Pending |
| 9 | World migration (World3 → EngineTest.ozone) | ⏳ Pending |
| 10 | LightningScript — ZoneInfo/SkyZoneInfo pawn | 🔮 Future |

---

## Phase 5 — Editor Sidepanel for CSG + Heightmap Editor

### 5.1 CSG Operation Dropdown
- **File**: `AngelEd/Source/Main.cpp` — existing Model Browser / Environment Settings panels
- Add a **CSG operation combo box** in the existing model placement panel (or new sub-panel)
- Options: None (Solid), Add, Sub, Intersect, De-resc
- When placing a primitive, store `csgOp` in the element data
- **Display**: CSG operation shown in the element list alongside type/position
- **Export**: CSG prefix written to `.ozone` files on save

### 5.2 Heightmap Editor (separate window)
- **New window** under `Models > Heightmap Editor` menu item
- **Features**:
  - Load/reload grayscale heightmap PNG
  - Adjust world-space dimensions (width, height, depth sliders)
  - Set world position (X, Y, Z) with real-time preview
  - Set texture overlay path
  - Brush height tool: paint height values directly onto the heightmap preview
  - Export as OZONE `heightmap` primitive line
- **Implementation**: Reuse existing `Win32Dialogs` pattern for native window panels; raylib render texture for heightmap preview

### Files to modify:
| File | Changes |
|---|---|
| `AngelEd/Source/Main.cpp` | Add menu items, panel toggles, CSG UI handling |
| `AngelEd/Source/Win32Dialogs.cpp` | New window creation for Heightmap Editor |
| `AngelEd/Source/Win32Dialogs.hpp` | Heightmap Editor window state |
| `AngelEd/Source/Editor.hpp` | CSG operation field on element data |
| `Source/OzOzoneLoader.cpp` | (references for preview rendering) |

---

## Phase 6+7 — Server Sync + Large-Scale Partitioning/Chunking

### 6.1 Server-Side CSG Awareness
- **Goal**: `AngelServ` must process CSG brushes identically to the game client so the HTTP map API reports correct collision geometry
- **Approach**: Move `CsgProcessor` into a shared header/translation unit linked by both client and server
- **Server changes**: Link `OzBsp.o` into `AngelServ`. The server doesn't need rendering, but needs the final processed collision volumes for partition queries (spawning NPCs/pickups in valid locations)

### 7.1 Spatial Partitioning (Grid/Chunk System)
- **Problem**: CSG can produce thousands of AABBs. The current linear scan in `DrawWorld` is O(n) per frame.
- **Solution**: Uniform grid partitioning:
  - Divide the world into `CHUNK_SIZE × CHUNK_SIZE` cells (e.g., 32×32 units)
  - Each cell stores indices of overlapping collision AABBs
  - Player's cell + adjacent 8 cells checked each frame (O(1) per cell)
- **Chunk manager class**: `WorldChunkManager` — builds grid from CsgProcessor output, queries by player position
- **Files**:
  - **New**: `Source/Physics/WorldChunk.hpp`, `Source/Physics/WorldChunk.cpp`
  - **Modify**: `Source/Core.hpp` — use chunk query instead of linear scan
  - **Modify**: `Source/Server/GameState.hpp` — server uses same chunk system for partition queries

### Files to modify:
| File | Changes |
|---|---|
| `Source/Physics/WorldChunk.hpp` (new) | Chunk grid class |
| `Source/Physics/WorldChunk.cpp` (new) | Build grid, query overlapping cells |
| `Source/Core.hpp` | Replace linear OZONE collision scan with chunk query |
| `Makefile` | Add `WorldChunk.o` |
| `Source/Server/GameState.cpp` | Use chunks for server-side NPC/pickup placement |

---

## Phase 8 — CSG Overflow Protection

### Problem
Repeated subtractive CSG can fragment a single solid AABB into dozens/hundreds of small adjacent AABBs, exploding memory and frame time.

### Mitigations

#### 8.1 Merge Adjacent Coplanar AABBs (post-CSG pass)
After the CsgProcessor finishes, run a merge pass:
- For each pair of AABBs that share a complete face (same min/max on 2 axes, adjacent on the 3rd), merge them into a single larger AABB
- Repeat until no more merges possible
- This reduces total volume count by typically 40-60%

#### 8.2 Fragment Limit
- Hard cap on `CsgProcessor::Apply()` split count: if a single SUB operation would create more than `MAX_SPLITS` (e.g., 64) sub-volumes, warn and skip
- Log: `"CSG overflow: brush at (x,y,z) exceeded split limit"`

#### 8.3 Editor Preview
- Before saving, show estimated collision volume count in the editor
- If count exceeds threshold (e.g., 500), warn the user about potential performance impact

### Files to modify:
| File | Changes |
|---|---|
| `Source/Physics/OzBsp.hpp` | Add `MergePass()`, max split constant |
| `Source/Physics/OzBsp.cpp` | Implement merge algorithm, fragment limit |
| `Source/OzOzoneLoader.cpp` | Call `MergePass()` after CSG processing |

---

## Phase 9 — World Migration

### EngineTest World
- Convert `World3/` to `EngineTest/` using `.ozone` format
- OZONE `heightmap` primitive for terrain
- CSG `add`/`sub` brushes for structures
- `zone sky` for skybox volume
- Remove dependency on `World.wdl` for EngineTest (WDL kept for legacy worlds)

### Legacy Worlds (World1, World2, Snowy, etc.)
- Keep untouched for asset reference
- Can be ported later via editor export to OZONE

### Steps:
1. Create `GameData/Worlds/EngineTest/` directory
2. Write `World.ozone` with heightmap + CSG brushes replicating World3's layout
3. Set `g_world_to_load = "EngineTest"` in `Core.hpp:19`
4. Test all collision types (heightmap, brushes, CSG subtract)

### Files to modify:
| File | Changes |
|---|---|
| `Source/Core.hpp` | `g_world_to_load` default → `"EngineTest"` |
| `GameData/Worlds/EngineTest/` (new) | World assets + `World.ozone` |

---

## Phase 10 — LightningScript (Future)

- **SkyZoneInfo** pawn type will be implemented as a LightningScript script, not hardcoded
- ZoneInfo scripts can react to player enter/exit, control sky color, fog settings, ambient light
- `zone sky` continues to work as the zone trigger; its behavior becomes scriptable
- PawnSystem will expose zone events to the LightningScript runtime when it's built

---

## Dependency Graph

```
Phase 5 (Editor) ──┐
                   ├──→ Phase 9 (World)
Phase 6+7 (Server) ─┘
                   │
Phase 8 (Overflow) ─┘
                       
Phase 10 (Script) ───→ (future, independent)
```

Phases 5, 6+7, and 8 can be worked on in parallel. Phase 9 depends on 5, 6+7, and 8 being complete.
