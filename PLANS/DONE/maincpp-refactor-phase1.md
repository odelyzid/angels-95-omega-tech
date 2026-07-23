# Phase 1: Purge Legacy Demo Code + Wire Engine Systems

> **Status:** Active | **Started:** 2026-07-22 | **Target:** 1 day

## Objective
Remove all hardcoded demo fallback code from `Source/Main.cpp` (pickups, projectiles, redundant hotbar) and wire the systems through `PawnSystem`, `LightningEntityManager`, and the unified 3D render pass in `DrawWorld()`.

---

## Step 1: Remove Legacy Demo Pickup System

**Status:** PENDING
**Files:** `Source/Main.cpp`

### Remove
| Symbol | Lines | Reason |
|--------|-------|--------|
| `g_demoPickupsReady` | L95 | Demo guard flag |
| `IconForPickupType()` | L501-511 | Hardcoded type→icon mapping |
| `DrawPickupBillboard()` | L513-521 | Replaced by `EngineBillboard::DrawPickup()` |
| `LocalDemoPickup` struct | L524 | Hardcoded struct |
| `g_demoPickupCount` | L525 | Hardcoded count |
| `g_demoPickups[6]` | L527 | Static array |
| `InitDemoPickupPositions()` | L529-547 | Hardcoded placement |
| `UpdateDemoPickupCollect()` | L549-587 | Demo collection logic |
| `DrawGroundPickups()` | L589-604 | Demo render dispatch |
| Second 3D pass in main loop | L1036-1040 | Redundant `BeginTextureMode/BeginMode3D/EndMode3D/EndTextureMode` |

### Acceptance
- File compiles without demo pickup symbols
- Pickups render via `PawnSystem::DrawEntities()` inside `DrawWorld()`

---

## Step 2: Wire PawnSystem Pickups into DrawWorld

**Status:** PENDING
**Files:** `Source/Core.hpp`

### Changes
- Inside `DrawWorld()`, `UpdateEntities()` block (around L2023), verify `PawnSystem::Instance().DrawEntities(camera)` is called
- Ensure `DrawEntities()` renders pickups via `EngineBillboard::DrawPickup()`
- If pickups not rendering in DrawWorld, add the call

### Verification
- Pickups visible in game after load
- No duplicate render pass

---

## Step 3: Remove Legacy Projectile System

**Status:** PENDING
**Files:** `Source/Main.cpp`

### Remove
| Symbol | Lines | Reason |
|--------|-------|--------|
| `LocalProjectile` struct | L98-103 | Hardcoded struct |
| `g_local_projectiles[32]` | L103 | Static array |
| `g_local_proj_count` | L104 | Counter |
| `DrawActiveProjectiles()` | L259-300 | Renders spheres outside BeginMode3D (bug) |
| `now_f()` (if unused after) | L106-108 | Helper only used by projectiles |
| `DrawActiveProjectiles()` call | L1157 | In 2D pass, uses 3D draw calls (bug) |

### Rewrite
- `FireWeapon()` (L305-330): Delegate to `LightningEntityManager::SpawnProjectile()` instead of filling local array

### Acceptance
- `FireWeapon()` triggers entity-managed projectile
- Client builds without projectile array symbols

---

## Step 4: Remove Redundant Hotbar

**Status:** PENDING
**Files:** `Source/Main.cpp`

### Remove
| Symbol | Lines | Reason |
|--------|-------|--------|
| `UpdateObjectBar()` call | L1075 | Already handled by `LightningEntityManager::DrawHotbar()` at L1073 |

### Acceptance
- Only one hotbar renders (LEMDrawHotbar, not ObjectBar)
- No double-draw artifact

---

## Step 5: Remove Demo Pickups Flag from Console

**Status:** PENDING
**Files:** `Source/Main.cpp`

### Change
- In `/world` command handler (L403): Remove `g_demoPickupsReady = false;`

### Acceptance
- `/world` command compiles and functions without demo pickup reference

---

## Step 6: Verify PawnSystem Pickup Rendering

**Status:** PENDING
**Files:** `Source/Pawn/OzPawnSystem.cpp`

### Verify
- `DrawEntities()` at L437-440 calls `EngineBillboard::DrawPickup()` for each `PickupNode`
- This is already implemented correctly

### Acceptance
- Pickup billboards render with correct icon, bobbing animation

---

## Verification Checklist
- [ ] All demo pickup code removed — compiles clean
- [ ] All legacy projectile array code removed — compiles clean
- [ ] `UpdateObjectBar()` call removed — compiles clean
- [ ] Second 3D render pass removed — compiles clean
- [ ] `DrawWorld()` renders pickups via `PawnSystem::DrawEntities()` visually
- [ ] `FireWeapon()` delegates to `LightningEntityManager::SpawnProjectile()`
- [ ] No 3D spheres drawn outside BeginMode3D
- [ ] `make OTENGINE` succeeds (or build.ps1)

---

## Progress Log

| Date | Step | Status | Notes |
|------|------|--------|-------|
| 2026-07-22 | Plan created | ✅ | |
| 2026-07-22 | Step 1: Remove demo pickup system | ✅ | Removed g_demoPickups, InitDemoPickupPositions, UpdateDemoPickupCollect, DrawGroundPickups, DrawPickupBillboard, IconForPickupType (~100 lines) |
| 2026-07-22 | Step 2: Wire PawnSystem into DrawWorld | ✅ | Already wired — DrawWorld()→UpdateEntities()→PawnSystem::UpdatePickups()/DrawAll()/DrawEntities() at Core.hpp L1609-1615 |
| 2026-07-22 | Step 3: Remove legacy projectile system | ✅ | Removed LocalProjectile struct, g_local_projectiles[32], g_local_proj_count, now_f(), DrawActiveProjectiles(); rewrote FireWeapon() to use LightningEntityManager::SelectedEntity() |
| 2026-07-22 | Step 4: Remove redundant UpdateObjectBar() | ✅ | Removed call at L1075; LightningEntityManager::DrawHotbar() at L1073 remains |
| 2026-07-22 | Step 5: Remove demo flag from console | ✅ | Removed g_demoPickupsReady = false from /world command |
| 2026-07-22 | Step 6: Verify PawnSystem rendering | ✅ | OzPawnSystem.cpp L437-440 already calls EngineBillboard::DrawPickup() for each PickupNode |
| 2026-07-22 | Compile check | ✅ | g++ -std=c++20 compiles clean (pre-existing ParasiteScript.hpp warnings only) |
