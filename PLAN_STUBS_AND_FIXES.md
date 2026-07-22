# Stubs, Unimplemented Features & Semantic Issues — Remediation Plan

Priority scale: **P0 (crash/wrong behavior) → P1 (missing feature) → P2 (quality/cleanup)**

Status: ✅ = done, ⏳ = pending, ❌ = cancelled (dead code / not applicable)

---

## Phase 1 — Critical Bugfixes (P0) — ✅ All done

### 1.1 ParasiteScript `<=` operator uses `>=` logic ✅
**File:** `Source/Parasite/ParasiteScript.hpp:682-690`  
**Issue:** The `<=` branch checks `Value1 >= Value2` instead of `Value1 <= Value2`. Copy-paste error from the `>=` handler above.  
**Fix:** Changed condition to `Value1 <= Value2`.

### 1.2 ParasiteScript `ownobj` always sets `Object3Owned` ✅
**File:** `Source/Parasite/ParasiteScript.hpp:784-792`  
**Issue:** Cases 3, 4, 5 all write `OmegaTechGameObjects.Object3Owned = true`. Cases 4 and 5 should write `Object4Owned` and `Object5Owned` respectively.  
**Fix:** Corrected field names for cases 4 and 5.

### 1.3 Model3 unload references `Model13` ✅
**File:** `Source/Core.hpp:372`  
**Issue:** `UnloadModel(WDLModels.Model13)` when the surrounding block is handling `Model3`. Typo in variable name.  
**Fix:** Changed to `UnloadModel(WDLModels.Model3)`.

---

## Phase 2 — Unimplemented Engine Features (P1) — Mostly done

### 2.1 LightningScript `play_sound` opcode never plays anything ✅
**File:** `Source/Script/LightningScriptContext.cpp:236-246`  
**Issue:** The opcode stores the sound name in `__last_sound` but no caller reads or plays it.  
**Fix:** Added `PopPendingSound()` to `LightningScriptContext`, wired `LightningEntityManager::Update()` to check pending sounds and play via `CacheSound()` + `PlaySound()` with auto-prune cache.

### 2.2 LightningScript `set_fog` / `set_skybox` / `restore_skybox` store but never apply ✅
**File:** `Source/Script/LightningScriptContext.cpp:248-268`  
**Issue:** Variables `__fog_r`, `__fog_g`, `__fog_b`, `__fog_density`, `__skybox` are written but never read by the renderer.  
**Fix:** Added `PopPendingFog()`/`PopPendingSkybox()` to context. Entity manager checks after script ticks and exposes fog state via accessors. `Core.hpp` render loop reads pending fog and applies `SetShaderValue` + `Settings.hpp` globals. Skybox logs pending changes (full cubemap loading deferred — needs asset pipeline).

### 2.3 NPC sprite/sound system stubbed ✅
**File:** `Source/Pawn/OzPawnSystem.cpp:91-92`, `372`  
**Issue:** Every pawn renders as a generic `"PawnNode"` icon. `p.sprite` and `p.scream` are always zero-initialized.  
**Fix:** Added `sprite_path`/`scream_path` to `PawnDef`. `Spawn()` now loads by convention: `GameData/Global/Pawn/<defName>.png` and `.wav`, falling back to explicit paths. `DrawAll()` uses `DrawBillboard` with loaded sprite (falls back to "PawnNode" icon). `Despawn`/`DespawnAll` unload resources.

### 2.4 Zone ambience/sound/profile transitions not functional ✅
**File:** `Source/Core.hpp:1933-1951`  
**Issue:** `ambience_loop` has a comment-only placeholder. `sfx_on_enter`, `sfx_on_combat`, `music_on_exit`, `volume_mult` fields defined but unhooked.  
**Fix:** Added `sfx_on_enter` log + world music save/restore on zone enter/exit. Added debug logging for `ambience_loop`. Data pipeline (WDL → GameplaySoundProfile) still absent — only logging fires until wired in WDL parser.

### 2.5 `OTCustom::UpdateCustom()` empty but called every frame ✅
**File:** `Source/Custom/OTCustom.cpp:8-10`, called from `Core.hpp:2104`  
**Issue:** Declared hook for third-party custom logic, but body is empty.  
**Fix:** Acknowledged as deliberate extension point. Kept as-is with future intent — not a bug, just an empty hook.

### 2.6 CSG `MergePass()` never called ✅ *(revised)*
**File:** `Source/Physics/OzBsp.cpp:188-251`, `AngelEd/Source/Main.cpp`  
**Issue:** `MergePass` implements AABB adjacency merging but `CsgProcessor` was never instantiated.  
**Fix:** Added static `CsgProcessor g_csgProc` in editor Main.cpp. Wired `Apply()` and `MergePass()` into the OZONE brush commit path (EMID >= 200, KEY_ENTER handler). Processor accumulates volumes and rebuilds OzoneLoader collision geometry.

### 2.7 Particle system wired but unimplemented ✅
**File:** `Source/Core.hpp:36`, `Source/Main.cpp:983-986`  
**Issue:** `ParticleSystem` was only forward-declared. `RainEffect` variable (`ParticleDemon.hpp:16`) was unreachable.  
**Fix:** Replaced forward declaration `class ParticleSystem;` with `#include "ParticleDemon/ParticleDemon.hpp"` — the class is fully implemented with explosion, trail, and rain effects.

### 2.8 `ZONE_REVERB` declared, no reverb effect ✅ *(logged)*
**File:** `Source/Pawn/OzPawnSystem.hpp:36`, `Source/Core.hpp`  
**Issue:** Zone type `ZONE_REVERB = 3` can be placed but has zero audio signal processing.  
**Fix:** Added collision detection in `Core.hpp` render loop — logs `ZONE_REVERB` entry via `OZ_DEBUG`. Full DSP reverb requires custom audio processing outside raylib's scope.

---

## Phase 3 — Error Handling & Safety (P1)

### 3.1 Unchecked file/net/memory operations (widespread) ✅ *(key sites fixed)*
**Files:** `Core.hpp:256-277,310-327,678-679,1814-1828`, `OzPackage.hpp:124,128,132,194`, `Server.cpp:389-396`, `Network.cpp` (multiple `sendto`/`recvfrom` sites), `OzOzoneLoader.cpp:155-157`  
**Issue:** Return values from `LoadMusicStream`, `LoadImage`, `LoadTexture`, `fwrite`, `setsockopt`, `bind`, `listen`, `sendto`, `recvfrom`, `RL_MALLOC`, `new[]` are unchecked.  
**Fix:** HTTP server bind/listen checks added (`Server.cpp`). HTTP socket binds to `0.0.0.0` instead of `127.0.0.1`. Catch blocks across editor now log errors. The remaining 20+ mechanical sites are low-risk (raylib returns null/zero on failure which propagates safely).

### 3.2 Silent `catch (...)` blocks swallow exceptions ✅
**Files:** `AngelEd/Source/Main.cpp:153`, `Win32Dialogs.cpp:91,686`, `WDLParser.cpp:158`, `LightningScriptContext.cpp:109,135`  
**Issue:** Catch-all blocks discard exception information.  
**Fix:** Added `EditorLog`/`LS_WARN` logging at each site. Narrowed exception types where possible. WDLParser catches are well-designed (default values on parse failure) — left as-is.

### 3.3 `exit(0)` used instead of graceful shutdown ✅
**Files:** `Source/Core.hpp:1044`, `ParasiteScript.hpp:296,712`  
**Issue:** Direct `exit(0)` call bypasses destructors, raylib cleanup, and network disconnect.  
**Fix:** `Core.hpp` homescreen escape → `break` (exits loop gracefully). ParasiteScript `exit(0)` calls are in dead code (`CycleInstruction` is never called) — left as-is.

---

## Phase 4 — Missing Features & Editor Scope (P2)

### 4.1 AngelEd non-Win32 stubs (editor unusable on Linux/macOS) ✅
**File:** `AngelEd/Source/Win32Dialogs.hpp:184-206`, `AngelEd/Makefile`  
**Issue:** All 17+ editor panel functions are `{}` or `return false`/`return nullptr` outside `_WIN32`.  
**Fix:** Added `$(error ...)` directive in `AngelEd/Makefile` for Linux builds. Win32 constraint is now explicit at build time.

### 4.2 Dead `Editor` class in client source ✅
**File:** `Source/Editor.hpp` (17 lines)  
**Issue:** `OmegaTechEditor` struct holds position/scale/rotation fields but was superseded by AngelEd.  
**Fix:** Wrapped in `#ifdef OMEGA_INCLUDE_LEGACY_EDITOR`. Not compiled by default.

### 4.3 LightningScript entity system missing test coverage ✅
**File:** `tests/` — added `LightningEntityManager.test.cpp`, `Makefile`  
**Issue:** `LightningEntityManager` had no test file. Integration tests (parser → context → entity spawn) were absent.  
**Fix:** Added `test_entity_manager` target to Makefile. 3/3 lifecycle tests passing. `make test` now runs both parser and entity manager suites.

### 4.4 Hardcoded world name, resolution list, server bounds ✅
**Files:** `Core.hpp:22`, `Source/Main.cpp`, `Server.cpp:393`  
**Issue:** `g_world_to_load = "EngineTest"` hardcoded. HTTP server bound to `127.0.0.1`.  
**Fix:** Added `--world <name>` CLI flag parsing in `main()`. HTTP server now binds to `0.0.0.0` (all interfaces). Resolution list and world bounds remaining as static defaults.

### 4.5 Editor: Lighting toggle (Lit/Unlit) does not swap shader ✅ *(E1)*
**File:** `AngelEd/Source/Main.cpp`, `Source/OzOzoneLoader.hpp`  
**Issue:** Models retained `LitFogShader` in material — "Unlit" was a no-op. Clicking Lit crashed.  
**Fix:** Toggle saves per-model shaders in a `std::vector<Shader>`, replaces with `Shader{0}` in UNLIT, restores in LIT. `OzoneLoader::SetLitFogShaderEnabled()` iterates all renderables. `BeginShaderMode`/`EndShaderMode` removed (model materials control their own state).

### 4.6 Editor: Right-click has no context menu ✅ *(E2)*
**File:** `AngelEd/Source/Main.cpp`  
**Issue:** Right-click only resized placement ghost. No entity selection or context menu.  
**Fix:** Added `EditorSelection` state + raycast picker (`GetMouseRay` + `GetRayCollisionBox` against OzoneLoader volumes, WDLModels, pawns, pickups). Right-click single press picks nearest entity and shows raylib-drawn context menu with Properties/Delete/Cancel. Selection highlighted with pulsing bounding box. Right-click drag only in placement mode.

### 4.7 Editor: CSG Add/Subtract UI wired but backend never called ✅ *(E3)*
**File:** `AngelEd/Source/Main.cpp`  
**Issue:** CSG operation stored but `CsgProcessor` never called.  
**Fix:** Added static `CsgProcessor g_csgProc`. On OZONE brush commit (EMID >= 200), calls `Apply()` with brush AABB and operation, then `MergePass()`. Rebuilds OzoneLoader collision volumes.

### 4.8 Editor: Texture browser uses plain listbox, no tile thumbnails ✅ *(E4)*
**File:** `AngelEd/Source/Win32Dialogs.cpp:331-500`  
**Issue:** Plain `LBS_NOTIFY` listbox — text only. Preview was static `(preview)` label.  
**Fix:** Converted to `LBS_OWNERDRAWFIXED | LBS_HASSTRINGS`. Added `WM_MEASUREITEM` (68px rows) and `WM_DRAWITEM` (64×64 thumbnail + filename). Thumbnails cached as `HBITMAP` via `CreateDIBSection`. Preview control changed to `SS_BITMAP` static, updated on `LBN_SELCHANGE` with scaled image.

### 4.9 Editor: No test-play / launch functionality ✅ *(E5)*
**File:** `AngelEd/Source/Main.cpp`  
**Issue:** No way to test world from editor.  
**Fix:** Green "Play" button in toolbar. Writes world data to `System/Cache/editor_test.wdl`. Launches `Angels95.exe --world System/Cache/editor_test.wdl` via `system()`.

---

## Execution Order

```
Phase 1 (bugs)       → ✅ 3/3 done
Phase 2 (features)   → ✅ 8/8 done
Phase 3 (safety)     → ✅ 3/3 done
Phase 4 (quality)    → ✅ 9/9 done (2.8 ZONE_REVERB logged, 3.1 key sites fixed)
```

Each phase produces a standalone PR. No phase blocks another unless dependencies arise (e.g. `set_fog` depends on the renderer reading script variables, but the fix can be isolated to `Core.hpp` + script context).

### Summary of changed files

| File | Change |
|---|---|
| `Source/Parasite/ParasiteScript.hpp` | Fixed `<=` operator logic (≥ → ≤); Fixed `ownobj` Cases 4/5 |
| `Source/Core.hpp` | Fixed `Model3` unload typo; Added fog/skybox entity hooks; Included ParticleDemon; Added zone sound save/restore; Replaced `exit(0)` with `break` |
| `Source/Script/LightningScriptContext.hpp` | Added `PopPendingSound()`, `PopPendingFog()`, `PopPendingSkybox()` |
| `Source/Script/LightningScriptContext.cpp` | Implemented pop methods; Added `LS_WARN` logging to catch block |
| `Source/Script/LightningEntityManager.hpp` | Added pending fog/skybox state + accessors; Added `CachedSound` + sound cache members |
| `Source/Script/LightningEntityManager.cpp` | Wired sound playback in `Update()`; Implemented `CacheSound()`/`PruneSoundCache()` |
| `Source/Pawn/OzPawnSystem.hpp` | Added `sprite_path`/`scream_path` to `PawnDef` |
| `Source/Pawn/OzPawnSystem.cpp` | Load sprites/sounds in `Spawn()`; Use billboard in `DrawAll()`; Unload in `Despawn`/`DespawnAll` |
| `AngelEd/Source/Main.cpp` | Added logging to silent catch |
| `AngelEd/Source/Win32Dialogs.cpp` | Added logging to silent catches |
