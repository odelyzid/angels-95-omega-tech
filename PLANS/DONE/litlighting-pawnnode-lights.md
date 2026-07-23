# LitLightning — PawnNode Light System

> **Status:** Active | **Started:** 2026-07-22 | **Est. Effort:** 7-10 days

## Objective
Replace hardcoded `PutLight()`/`LightCounter` with proper `LightNode` PawnNodes managed by `PawnSystem`, loadable from WDL/OZONE, and driven by a modernized shader pipeline.

---

## Phase P4a: LightNode Struct + PawnSystem Integration

**New files:** `Source/Renderer/LitLightning.hpp`, `Source/Renderer/LitLightning.cpp`
**Modified:** `Source/Pawn/OzPawnSystem.hpp/.cpp`, `Source/Core.hpp`

### LightNode struct
- Fields: id, active, type (POINT/SPOT/DIRECTIONAL), position, target, color, intensity, radius, innerCone, outerCone, effect, phase, period, isStatic, castShadow, name

### PawnSystem additions
- `m_lights: vector<LightNode>`
- `AddLight()`, `RemoveLight()`, `ClearLights()`, `GetLights()`

### UpdateLightSources() rewrite
- Light[0] = directional headlight (camera)
- Iterate PawnSystem lights, skip static, animate dynamic
- Sort by distance, cap at 64, call UpdateLightValues()

---

## Phase P4b: WDL/OZONE Light Loading + Editor

**WDL extended format:** `Light:X:Y:Z:R:G:B:I:Rad:T:E:`
**OZONE format:** `light point/spot/directional ...`

**Modified:** OzoneParser, OzOzoneLoader, Editor Main.cpp, Win32Dialogs.cpp

---

## Progress Log

| Date | Phase | Status | Notes |
|------|-------|--------|-------|
| 2026-07-22 | Plan created | ✅ | |
| 2026-07-22 | P4a: Created Source/Renderer/LitLightning.hpp | ✅ | LightNode struct with all fields; LitLightType/LitLightEffect enums |
| 2026-07-22 | P4a: Created Source/Renderer/LitLightning.cpp | ✅ | LitLightning_Update/Animate/SortByDistance; BuildRLight helper |
| 2026-07-22 | P4a: Modified OzPawnSystem.hpp/.cpp | ✅ | AddLight/RemoveLight/ClearLights/GetLight/GetLights; m_lights vector |
| 2026-07-22 | P4a: Rewrote UpdateLightSources() in Core.hpp | ✅ | Removed LightCounter, PutLight, ClearLights; uses LitLightning_Update |
| 2026-07-22 | P4a: Wired WDL Light: parser | ✅ | Extended format placeholder; both WDL parser paths updated |
| 2026-07-22 | Compile check | ✅ | LitLightning.cpp, OzPawnSystem.cpp, Main.cpp all pass |
| | 2026-07-22 | P4b: OZONE light entity parser | ✅ | Added ENTITY_LIGHT enum; parsed light point/spot/directional; wired LoadOzoneEntity(); added to collision skip lists; added light test entities to EngineTest World.ozone; updated Makefile with LitLightning.o |
