# Editor Gaps — Implementation Plan

The five remaining editor features that are wired in UI but non-functional.
Priority: **P0 (blocks editing workflow) → P1 (missing feature) → P2 (polish)**

---

## E1 — Lighting Toggle (Lit/Unlit) — P0

**File:** `AngelEd/Source/Main.cpp:462-473,649-655`, `AngelEd/Source/Editor.hpp:34-40`

### Problem
`LightingMode::UNLIT` is set from the toolbar button, but no code actually removes the `LitFogShader` from model materials. Models retain the shader at all times — "Unlit" is a zero-impact no-op.

### Root cause
Models are loaded with `materials[0].shader = OTEditor.LitFogShader` (set during `LoadModelWithFallback` in the WDL/OZONE load pipeline). The UNLIT branch at line 471 just has a `// skip LitFogShader` comment with no action.

### Fix steps

1. **Add shader save/restore fields** to `Editor` class:
   ```cpp
   Shader PrevShaders[MAX_CACHED_MODELS];  // saved original shader per model
   int CachedModelCount = 0;
   ```

2. **On `ViewMode` change to UNLIT** — iterate `GameModels::ModelArray` and `OzoneLoader` renderables:
   - Save each model's `materials[0].shader` into `PrevShaders[i]`
   - Set `materials[0].shader = GetShaderDefault()` (raylib's default unlit shader)

3. **On `ViewMode` change back to LIT/WIREFRAME** — restore from `PrevShaders[i]`.

4. **Edge case**: OzoneLoader's `Draw()` iterates `m_renderables` which store `Model` copies. Changes to their shader apply immediately but won't persist if `OzoneLoader::LoadFile()` is called again (models are rebuilt). No additional save needed for the load path since the shader is re-assigned on model creation.

### Verification
- Click "Unlit" in toolbar → all 3D viewport renders without shading/lighting
- Click "Lit" → lighting returns immediately
- Wireframe → as before (culling/depth mask toggle)

---

## E2 — Right-Click Context Menu (Entity Properties) — P0

**File:** `AngelEd/Source/Win32Dialogs.cpp`, `AngelEd/Source/Main.cpp:630-639`

### Problem
Right-click in the 3D viewport currently **resizes the placement ghost** (left=move, right=resize). There is no entity selection, no context menu, no properties dialog. Placed objects cannot be inspected or edited after placement.

### Root cause
No `WM_CONTEXTMENU` handler, no raycast entity picker, no entity properties dialog anywhere in the codebase. The entity system stores all placed objects in `WDLModels.ModelArray` and `OzoneLoader::m_renderables`, but there is no selection state or per-entity editing.

### Fix steps

1. **Add viewport entity raycast** — at mouse click, cast a ray from camera through cursor position and test against:
   - `OzoneLoader::GetCollisionVolumes()` — AABB test for brush CSG geometry
   - `WDLModels.ModelArray` — AABB test for WDL-placed models
   - `PawnSystem::GetPawns()` — point-radius test for NPCs

2. **Add selection state** to editor globals:
   ```cpp
   enum class SelType { NONE, BRUSH, MODEL, NPC, PICKUP, LIGHT };
   struct Selection {
       SelType type = SelType::NONE;
       int index = -1;      // index into the respective array
       std::string name;
   };
   static Selection g_selection;
   ```

3. **Add `WM_CONTEXTMENU` handler** — when right-clicking in the 3D viewport:
   - Raycast to find nearest entity under cursor
   - If hit → store selection, show popup menu via `TrackPopupMenu`
   - Menu options: **Properties** (opens entity editor dialog), **Delete** (removes from world), **Duplicate** (copies at offset)

4. **Change right-click resize** — move the resize interaction to **Shift+left-click** or **Ctrl+left-click** so right-click can be used for context menus.

5. **Entity Properties dialog** — create a new Win32 dialog (`DialogProc`) showing:
   - Position X/Y/Z (editable float fields)
   - Rotation/Scale
   - For NPCs: PawnDef name, health, aggro range
   - For pickups: type, respawn time
   - For lights: color picker, radius, intensity

### Verification
- Right-click on a placed brush → popup menu appears
- "Properties" → dialog with position/rotation fields opens
- "Delete" → entity removed from world, viewport refreshes
- Right-click on empty space → no menu (or "Create New..." menu)

---

## E3 — CSG Add/Subtract/Intersect — P1

**File:** `AngelEd/Source/Main.cpp:929-939`, `AngelEd/Source/Win32Dialogs.cpp:1293-1448`, `Source/Physics/OzBsp.hpp`

### Problem
CSG operation buttons (Add/Sub/Intersect/DeResc) are fully wired in the sidebar and toolbar. The selected operation is stored in `OmegaTechEditor.CSGOperation` and displayed. But when a brush is placed, the operation value is **never consumed** — no `CsgProcessor::Apply()` call processes it. Brushes are placed as passive WDL model instances with CSG metadata that nothing reads.

### Root cause
The `CsgProcessor` class at `Source/Physics/OzBsp.hpp:31` implements AABB-based CSG boolean operations (ADD, SUB, INTERSECT) but is never instantiated or called. The brush placement path at `Main.cpp:929-939` just sets `EMID` and `CSGOperation` as metadata — the backend pipeline from UI → CsgProcessor → collision geometry is missing.

### Fix steps

1. **Instantiate `CsgProcessor`** in editor state — add to `Editor.hpp` or as a static in `Main.cpp`:
   ```cpp
   static CsgProcessor g_csgProc;
   ```

2. **At brush commit time** (`Main.cpp:571-628`, the `KEY_ENTER` handler):
   - After building the WDL command, also call:
     ```cpp
     CsgBrush brush;
     brush.op = (CsgOp)OmegaTechEditor.CSGOperation;
     brush.minX = px; brush.minY = py; brush.minZ = pz;
     brush.maxX = px + w; brush.maxY = py + h; brush.maxZ = pz + d;
     g_csgProc.Apply(brush);
     g_csgProc.MergePass();
     ```

3. **After CSG processing**, retrieve final solid volumes via `g_csgProc.GetVolumes()` and store them in `OzoneLoader::GetCollisionVolumes()` for rendering and collision.

4. **CSG preview** — show a translucent preview of the brush's effect before committing:
   - For SUB: draw subtractor volume in red wireframe over existing solids
   - For ADD: draw new volume in green wireframe

5. **Undo** — store CSG operation history in a vector so `Ctrl+Z` can pop the last operation and re-run all remaining operations.

### Verification
- Place a SOLID box → renders as opaque collision geometry
- Place a SUB brush overlapping the SOLID → the overlapping region is removed (carved out)
- Place an ADD brush overlapping gap → fills the gap
- MergePass reduces split count

---

## E4 — Texture Browser Tile Thumbnails — P2

**File:** `AngelEd/Source/Win32Dialogs.cpp:331-454`, `AngelEd/Source/Win32Dialogs.hpp:87-88`

### Problem
The Texture Manager window shows texture filenames in a plain `LBS_NOTIFY` listbox (text-only). Selecting a texture shows a static `(preview)` label instead of a rendered thumbnail. There is no visual browsing experience — users must guess textures by name.

### Root cause
The listbox at line 390-393 uses `LB_ADDSTRING` with text only. No `LBS_OWNERDRAWFIXED` or `LBS_OWNERDRAWVARIABLE` style is set. No `WM_DRAWITEM` or `WM_MEASUREITEM` handler exists for the texture list. The preview control is a plain `STATIC` label with text "(preview)" (line 357).

### Fix steps

1. **Convert listbox to owner-drawn**:
   - Change window style from `LBS_NOTIFY` to `LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS`
   - Add `WM_MEASUREITEM` handler returning item height = thumbnail height (e.g., 64px)

2. **Load and cache thumbnails**:
   - On `ScanTextureBrowserFiles()`, after collecting paths, load each texture via `LoadImageWithFallback()` and scale to 64×64
   - Store thumbnails as `HBITMAP` in `g_textureFiles[i].thumbnail`
   - Cache the `HBITMAP` handles until the next scan

3. **Add `WM_DRAWITEM` handler**:
   ```cpp
   case WM_DRAWITEM: {
       LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
       int idx = dis->itemID;
       // Draw thumbnail at dis->rcItem left side (64x64)
       if (g_textureFiles[idx].thumbnail) {
           HDC hdcMem = CreateCompatibleDC(dis->hDC);
           SelectObject(hdcMem, g_textureFiles[idx].thumbnail);
           StretchBlt(dis->hDC, dis->rcItem.left, dis->rcItem.top, 64, 64,
                      hdcMem, 0, 0, 64, 64, SRCCOPY);
           DeleteDC(hdcMem);
       }
       // Draw filename text to the right of thumbnail
       DrawTextA(dis->hDC, g_textureFiles[idx].name.c_str(), -1,
                 &RECT{dis->rcItem.left + 68, dis->rcItem.top,
                       dis->rcItem.right, dis->rcItem.bottom},
                 DT_VCENTER | DT_SINGLELINE);
       return TRUE;
   }
   ```

4. **Replace `(preview)` label with actual preview**:
   - When a texture is selected in the listbox (`LBN_SELCHANGE`), load the full-resolution image
   - Scale to fit the preview area
   - Create `HBITMAP` and send `STM_SETIMAGE` to the static control

5. **Package-aware thumbnails** — for textures inside `.oztex` packages, extract to temp file, load, scale, then clean up.

### Verification
- Open Texture Manager → listbox shows 64×64 thumbnails next to filenames
- Click a texture → preview area shows the actual image
- Scroll list → thumbnails render correctly (no flicker, no memory leak)
- Thumbnails update when `ScanTextureBrowserFiles()` is called again

---

## E5 — Test-Play / Launch Client — P2

**File:** `AngelEd/Source/Main.cpp` (no existing code), `AngelEd/Source/Win32Dialogs.cpp`

### Problem
There is no way to test the current world from within the editor. The user must save, exit, run `Angels95.exe`, type connect commands, etc. No "Play in Editor" (PIE) mode exists.

### Root cause
The editor is purely a world editing tool with no integration to the game client. The game client and editor are separate executables.

### Fix steps

1. **Add "Play" button** to the toolbar or File menu — command ID e.g. `CMD_PLAY = 100`.

2. **On Play click**:
   - Save current world data to a temp WDL or OZONE file in `System/Cache/editor_test_zone.wdl`
   - Launch `Angels95.exe` as a subprocess with `CreateProcess`, passing the world path:
     ```
     Angels95.exe --world System/Cache/editor_test_zone.wdl
     ```
   - Minimize the editor window (optional)

3. **World serialization** — before launching, write `OTEditor.WorldData` to the temp file in WDL format. This reuses the existing `CacheWDL()` infrastructure.

4. **Command-line flag** — add `--world <path>` flag to `Source/Main.cpp` that loads the specified world instead of the default. This is a single `if (argc > 2 && strcmp(argv[1], "--world") == 0) ...` check before `LoadWorld()`.

5. **Optional: Editor re-focus** — when the game client exits, the editor window should regain focus and optionally prompt "Reload world from playtest changes?".

### Verification
- Click "Play" in editor → Angels95.exe launches with current world
- Editor stays open (minimized or in background)
- Game loads the test world correctly
- Close game → editor is still running, world data intact

---

## Execution Order

```
E1 (Lighting toggle)    → P0, quick: shader swap on toggle
E2 (Right-click menu)    → P0, medium: raycast + dialog + menu
E3 (CSG operations)     → P1, large: integrate CsgProcessor pipeline
E4 (Texture thumbnails) → P2, medium: owner-draw listbox + image loading
E5 (Test-play)          → P2, medium: subprocess launch + --world flag
```

E1 and E2 unblock basic editing workflow. E3 is the largest effort (requires full CSG boolean pipeline). E4 and E5 are polish/quality-of-life.
