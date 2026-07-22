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

### 2.6 CSG `MergePass()` never called ❌
**File:** `Source/Physics/OzBsp.cpp:188-251`  
**Issue:** `MergePass` implements AABB adjacency merging but `CsgProcessor` is never instantiated anywhere in the compiled codebase.  
**Verdict:** Entire `CsgProcessor` is dead code — no `Apply()` calls exist. No action taken.

### 2.7 Particle system wired but unimplemented ✅
**File:** `Source/Core.hpp:36`, `Source/Main.cpp:983-986`  
**Issue:** `ParticleSystem` was only forward-declared. `RainEffect` variable (`ParticleDemon.hpp:16`) was unreachable.  
**Fix:** Replaced forward declaration `class ParticleSystem;` with `#include "ParticleDemon/ParticleDemon.hpp"` — the class is fully implemented with explosion, trail, and rain effects.

### 2.8 `ZONE_REVERB` declared, no reverb effect ⏳
**File:** `Source/Pawn/OzPawnSystem.hpp:36`  
**Issue:** Zone type `ZONE_REVERB = 3` can be placed but has zero audio signal processing.  
**Status:** Requires custom DSP (raylib has no built-in reverb). Lower priority.

---

## Phase 3 — Error Handling & Safety (P1)

### 3.1 Unchecked file/net/memory operations (widespread) ⏳
**Files:** `Core.hpp:256-277,310-327,678-679,1814-1828`, `OzPackage.hpp:124,128,132,194`, `Server.cpp:389-396`, `Network.cpp` (multiple `sendto`/`recvfrom` sites), `OzOzoneLoader.cpp:155-157`  
**Issue:** Return values from `LoadMusicStream`, `LoadImage`, `LoadTexture`, `fwrite`, `setsockopt`, `bind`, `listen`, `sendto`, `recvfrom`, `RL_MALLOC`, `new[]` are unchecked.  
**Status:** 20+ sites — mechanical but high-value. Each needs individual review.

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

### 4.1 AngelEd non-Win32 stubs (editor unusable on Linux/macOS) ⏳
**File:** `AngelEd/Source/Win32Dialogs.hpp:184-206`  
**Issue:** All 17+ editor panel functions are `{}` or `return false`/`return nullptr` outside `_WIN32`.  
**Fix:** Either:
- Implement native Linux panel equivalents (e.g. GTK3, or raylib-based in-viewport panels), or
- Document the Win32 constraint explicitly and add a build-time error for non-Win32.

### 4.2 Dead `Editor` class in client source ⏳
**File:** `Source/Editor.hpp` (17 lines)  
**Issue:** `OmegaTechEditor` struct holds position/scale/rotation fields but was superseded by AngelEd. Left in the client codebase as dead code.  
**Fix:** Remove or file under `#ifdef ANGELED_EMBEDDED`.

### 4.3 LightningScript entity system missing test coverage ⏳
**File:** `tests/` — only `LightningScriptParser`, `LightningScriptContext`, `LightningEntityRegistry` have tests.  
**Issue:** `LightningEntityManager` has no test file. Integration tests (parser → context → entity spawn) are absent.  
**Fix:** Add `test_entity_manager` target to Makefile and write basic lifecycle tests.

### 4.4 Hardcoded world name, resolution list, server bounds ⏳
**Files:** `Core.hpp:22,719-738`, `GameState.cpp:56-60,92-104,114-154`, `Server.cpp:393`  
**Issue:** `g_world_to_load = "EngineTest"` hardcoded; resolution list is static; world bounds fixed to `[-2000,2000]`; HTTP server binds to `127.0.0.1` only.  
**Fix:** Make world name configurable (INI/CLI flag), support custom world bounds in WDL, add `--bind` flag for HTTP, dynamic resolution detection.

### 4.5 Editor: Lighting toggle (Lit/Unlit) does not swap shader ⏳
**File:** `AngelEd/Source/Main.cpp:470-473`, `AngelEd/Source/Editor.hpp:34-40`  
**Issue:** `LightingMode::UNLIT` sets no actual shader override. Models retain `LitFogShader` in their material — "Unlit" is a no-op.  
**Fix:** Iterate cached models on toggle; save original shader; replace with `GetShaderDefault()` in UNLIT; restore on LIT/WIREFRAME.

### 4.6 Editor: Right-click has no context menu ⏳
**File:** `AngelEd/Source/Win32Dialogs.cpp`, `AngelEd/Source/Main.cpp:635-639`  
**Issue:** Right-click in viewport only resizes placement ghost. No `WM_CONTEXTMENU`, no entity properties, no delete or edit actions on placed objects.  
**Fix:** Add right-click raycast → pick nearest entity → show popup menu with Properties/Delete/Duplicate.

### 4.7 Editor: CSG Add/Subtract UI wired but backend never called ⏳
**File:** `AngelEd/Source/Main.cpp:929-939`, `Source/Physics/OzBsp.hpp`  
**Issue:** CSG operation value (ADD/SUB/INTERSECT) is stored in `CSGOperation` and displayed in the sidebar, but `CsgProcessor::Apply()` is never invoked. Brushes are placed as regular model instances.  
**Fix:** Integrate `CsgProcessor` into the brush placement commit path: call `Apply()` per brush, then execute `MergePass()`.

### 4.8 Editor: Texture browser uses plain listbox, no tile thumbnails ⏳
**File:** `AngelEd/Source/Win32Dialogs.cpp:331-454`  
**Issue:** Texture manager uses `LBS_NOTIFY` listbox — text only. No `LBS_OWNERDRAWFIXED`, no `WM_DRAWITEM`. The "preview" area is a static `(preview)` label.  
**Fix:** Convert listbox to owner-drawn; load thumbnails via `LoadImage` → scale → `CreateBitmap`; render in `WM_DRAWITEM`. Replace `(preview)` label with actual `STM_SETIMAGE` static control.

### 4.9 Editor: No test-play / launch functionality ⏳
**File:** `AngelEd/Source/Main.cpp` (nowhere)  
**Issue:** No menu item or button to launch the game client with the current world. Editor is purely an editing tool.  
**Fix:** Add a "Play" button that writes the world to a temp file and spawns `Angels95.exe` as a subprocess with `--world` pointing to the temp file.

---

## Execution Order

```
Phase 1 (bugs)       → ✅ 3/3 done
Phase 2 (features)   → ✅ 6/8 done, 1 ❌ (dead code), 1 ⏳ (ZONE_REVERB)
Phase 3 (safety)     → ✅ 2/3 done, 1 ⏳ (3.1 unchecked ops — 20+ sites)
Phase 4 (quality)    → ⏳ 4/4 pending original + 5 new editor gaps (4.5-4.9)
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
