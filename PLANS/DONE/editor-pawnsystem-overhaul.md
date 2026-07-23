# Editor & PawnSystem Overhaul Plan

> **Status:** Active | **Started:** 2026-07-22 | **Target:** All tasks

---

## Task 1: Fix Billboard/Texture Loading (P0)

**Status:** PENDING
**Files:** `Source/Renderer/EngineBillboard.hpp`, `Source/Package/OzAssetMapper.cpp`, `Source/Pawn/OzPawnSystem.cpp`

### Problem
Viewport billboards for entity icons (PlayerStart, PawnNode, ZoneWater, Light, Sound, Music, etc.) silently fail to render because the AssetMapper texture aliases are missing or PNG assets don't exist.

### Solution
1. Verify `GameData/Global/Engine/` contains all required PNGs:
   - `PawnNode.png`, `PlayerStart.png`, `ZoneWater.png`, `ZoneLadder.png`, `ZoneSky.png`, `ZoneSound.png`, `ZoneReverb.png`, `ZoneInfo.png`, `Light.png`, `Sound.png`, `Music.png`
2. Verify `GameData/Global/Items/` contains: `HealthVial.png`, `ManaVial.png`, `EnergyCrystal.png`, `Key.png`, `Coin.png`, `Powerup.png`
3. Check `AssetMapper::RegisterEngineTextures()` registers all required aliases
4. Add missing texture assets or alias registrations
5. Add `TraceLog(LOG_INFO)` diagnostics on texture load failure
6. Fix `PawnSystem::DrawAll()` to use `AssetMapper::GetTexture(p.defName)` instead of raw sprite path

### Acceptance
- All entity billboards render in editor viewport
- No silent texture load failures

---

## Task 2: Fix Right-Click Properties + Duplicate (P0)

**Status:** PENDING
**Files:** `AngelEd/Source/Main.cpp` (lines 1067-1079)

### Problem
Right-click context menu shows "Properties" and "Duplicate" options, but clicking them does nothing (action handlers are missing).

### Solution
1. **Properties (action 1):** Open raylib overlay panel or Win32 dialog showing:
   - NPCs: name, position X/Y/Z (editable), health, speed, aggro range
   - Pickups: type name, position, respawn time
   - Brushes: position, dimensions, CSG op
   - Models: name, path
2. **Duplicate (action 3):** Clone entity at `pos + {2, 0, 2}`
3. **Delete (action 2):** Already implemented — keep as-is

### Acceptance
- Right-click entity → "Properties" shows editable property panel
- Right-click entity → "Duplicate" creates a copy at offset position
- Right-click entity → "Delete" removes entity

---

## Task 3: Zone Tab Panel Show/Hide (P1)

**Status:** PENDING
**Files:** `AngelEd/Source/Win32Dialogs.cpp` (lines 974-1150)

### Problem
`ShowZoneTab()` doesn't hide/show tab control groups. All 4 tabs' controls (Fog, Ambient, GameType, Particles) are created unconditionally in `WM_CREATE` and remain visible simultaneously, making the panel unusable.

### Solution
1. Store HWND arrays for each tab's controls at creation time
2. `ShowZoneTab()` calls `ShowWindow(SW_HIDE)` on inactive groups, `ShowWindow(SW_SHOW)` on active group
3. Add missing `fogStart`/`fogEnd` input fields for Fog tab
4. Add scrollbar value labels (show current numeric value near each slider)

### Acceptance
- Only active tab's controls are visible
- Tab switching correctly hides previous tab's controls
- Fog start/end fields appear on Fog tab

---

## Task 4: Data-Driven Pawn Definitions (P1)

**Status:** PENDING
**Files:** `AngelEd/Source/Main.cpp` (lines 443-454), `Source/Pawn/OzPawnSystem.hpp`

### Problem
Walker, Skaarj, Brute, Floater definitions are hardcoded in `Main.cpp:444-453`, making it impossible to add new enemy types without recompiling.

### Solution
1. Create `GameData/Global/PawnDefs/` directory
2. Add config files (`.cfg` or `.json`):
   ```
   name=Walker
   speed=1.5
   aggroRange=6.0
   attackRange=1.5
   damage=10.0
   maxHealth=100
   sprite_path=GameData/Global/Pawn/Walker.png
   scream_path=GameData/Global/Pawn/Walker.wav
   ```
3. Scan directory at startup → `RegisterDef()` for each file
4. Keep legacy hardcoded fallback if no config dir
5. Remove duplicate `PawnManagerAddPawn()` calls — derive from defs
6. Add "Refresh Defs" button in Pawn Manager dialog

### Acceptance
- Adding a new `.cfg` file to `PawnDefs/` makes the pawn available in editor
- No hardcoded enemy type registrations remain

---

## Task 5: .OZONE Save Round-Trip (P1)

**Status:** PENDING
**Files:** `AngelEd/Source/Main.cpp` (lines 362-371), `AngelEd/Source/Win32Dialogs.cpp` (lines 552-574), `Source/OzOzoneLoader.hpp`, `Source/OzOzoneLoader.cpp`

### Problem
- Save dialog filter only includes `*.wdl`
- `SaveWorldDocument()` rejects non-.wdl paths
- After loading `.ozone`, brush geometry is lost on save; only entity nodes are preserved as WDL text

### Solution
1. Extend `ChooseSaveWorldFile` filter to include `*.ozone`
2. Implement `ExportToOzone()` that serializes full scene:
   - Collision volumes → OZONE primitives (`add box`/`add cyl`/`add sph`)
   - Heightmap → `heightmap` instruction
   - Entities → `playerstart`/`pickup`/`npc`/`zone` instructions
3. Preserve original OZONE source text during load for lossless round-trip
4. Detect file extension in save dialog and call appropriate serializer

### Acceptance
- Open `.ozone` → edit → Save → re-open preserves all data
- Save dialog offers both `.wdl` and `.ozone`

---

## Task 6: Viewport Presets + Axis Gizmo (P2)

**Status:** PENDING
**Files:** `AngelEd/Source/Main.cpp` (camera section), `AngelEd/Source/Editor.hpp`

### Problem
Only perspective camera with MMB orbit. No orthographic top/side/bottom views. No axis gizmo for direct manipulation.

### Solution
1. Add `CAMERA_ORTHOGRAPHIC` projection
2. Keyboard shortcuts:
   - `NumPad 7` → Top view (position above, looking down Z)
   - `NumPad 1` → Bottom view (position below, looking up)
   - `NumPad 3` → Right/Side view
   - `NumPad 9` → Left view
   - `NumPad 5` → Perspective (save/restore state)
3. Save perspective camera state before switching to ortho
4. Render axis gizmo (red X, green Y, blue Z arrows) at placement ghost / selected entity position
5. Raycast gizmo handles on left-click → constrain movement to that axis
6. Extend to work on selected (right-click picked) entities
7. Add visual feedback (highlight active axis)

### Acceptance
- NumPad 7/1/3/9/5 switch between view presets
- Axis arrows render at selected entity
- Left-drag on axis handle moves entity along that axis

---

## Task 7: PawnManager Tree Hierarchy (P3)

**Status:** PENDING
**Files:** `Source/Pawn/OzPawnSystem.hpp`, `AngelEd/Source/Win32Dialogs.cpp` (PawnMgrProc), `AngelEd/Source/Main.cpp`

### Problem
PawnSystem stores pawns, pickups, zones, emitters, playerstarts in separate flat vectors. Pawn Manager shows a flat list. No tree structure.

### Solution
1. Implement `PawnTreeNode` struct:
   ```cpp
   struct PawnTreeNode {
       const char* label;
       bool isExpanded;
       std::vector<PawnTreeNode> children;
       const char* defName;  // Valid for leaf nodes only
   };
   ```
2. Build tree scaffold:
   ```
   Actor
   └── Pawn
       ├── PlayerPawn → OmegaPlayer
       ├── EnemyPawn → Brute, Floater, Walker, Skaarj
       ├── InventoryPawn → Weapons, Pickups (HealthVial, ManaVial...), Armor
       └── VolumeMarkers → PlayerStartNode, EmitterNode, ZoneVolumeNode
   ```
3. **Option A (recommended):** Keep flat runtime vectors for fast iteration, build tree view only for editor UI
4. Replace Win32 ListBox with `WC_TREEVIEW` in Pawn Manager dialog
5. Add expand/collapse, drag between categories
6. Hover/select highlights entity in viewport

### Acceptance
- Pawn Manager shows tree hierarchy, not flat list
- Expanding/collapsing categories works
- Selecting a leaf highlights the entity in viewport
- Refactored code compiles and runs without regression

---

## Task 8: Zone Slider Issue (covered by Task 3)

**Status:** COVERED_BY_TASK_3

---

## Progress Log

| Date | Task | Status | Notes |
|------|------|--------|-------|
| 2026-07-22 | Plan created | ✅ | All tasks documented |
| 2026-07-22 | Task 1: Billboard/Texture loading | ✅ | Added missing zone type aliases to RegisterEngineTextures(); created missing PNGs for ZoneWater/Ladder/Sky/Sound/Reverb; created GameData/Global/Pawn/ default sprites |
| 2026-07-22 | Task 2: Right-Click Properties + Duplicate | ✅ | Implemented DrawPropertiesPanel() overlay with editable fields; added keyboard text input; implemented Properties (action 1), Duplicate (action 3); extended Delete for zones; added RaycastTestZones() for zone selection |
| 2026-07-22 | Task 3: Zone Tab Panel show/hide | ✅ | Replaced static y-offset control groups with HWND arrays per tab; ShowZoneTab() hides inactive groups; added missing fog start/end input fields |
| 2026-07-22 | Task 4: Data-driven Pawn Defs | ✅ | Created GameData/Global/PawnDefs/ with .cfg files; replaced hardcoded Walker/Skaarj/Brute/Floater with directory scanner; added legacy fallback |
| 2026-07-22 | Task 5: .OZONE save round-trip | ✅ | Added ExportToOzone() serializing brushes, heightmap, entities; updated ChooseWorldFile filter to include .ozone in save dialog; SaveWorldDocument now handles .ozone extension |
| 2026-07-22 | Task 6: Ortho views + axis gizmo | ✅ | Added SetViewPreset/SetViewPerspective functions; NumPad 7/1/3/9 for top/bottom/right/left views; NumPad 5 back to perspective; enhanced axis gizmo with sphere endpoints |
| 2026-07-22 | Task 7: PawnManager tree hierarchy | ✅ | Added PawnTreeNode struct and BuildPawnTree(); replaced listbox with WC_TREEVIEW; hierarchy: Actor→Pawn→PlayerPawn/EnemyPawn/InventoryPawn/VolumeMarkers |
