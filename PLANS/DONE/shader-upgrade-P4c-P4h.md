# Shader Upgrade — P4c through P4h

> **Status:** Active | **Started:** 2026-07-22 | **Target:** ~6.5 days total

## Objective
Upgrade the LitFog shader pipeline from PS1-style limited lighting to a modern UBO-driven system with attenuation, spot cones, per-light effects, bump mapping, detail textures, and zone-based culling.

---

## P0: /summon Crash Fix (immediate)

**Root cause:** `Core.hpp:1422-1431` calls `DrawModelEx()` on `OmegaTechGameObjects.Object1`-`Object5` without null-checking `model.meshes`. When `.obj` files are missing, models have null meshes → access violation.

**Fix:** Add `&& OmegaTechGameObjects.ObjectN.meshes != nullptr` guard to each of the 5 `DrawModelEx` calls.

---

## P4c: UBO Lights + Attenuation + MAX_LIGHTS=32 **(2 days)**

| File | Change |
|------|--------|
| `GameData/Shaders/Lights/LitFog.fs` | Replace `uniform Light lights[4]` with `layout(std140) uniform LightBlock { Light lights[32]; }` |
| `GameData/Shaders/Lights/LitFog.fs` | Add quadratic falloff: `atten = 1.0 / (1.0 + 0.09*dist + 0.032*dist*dist)` |
| `GameData/Shaders/Lights/LitFog.fs` | Add spot cone: `spot = pow(max(dot(lightDir, spotDir), 0.0), exponent)` |
| `Source/rlights/rlights.h` | Already uploads all fields — no change needed |
| `Source/Core.hpp` | Verify MAX_LIGHTS constant alignment (CPU=64, GPU=32 — cap at 32) |

**Attenuation formula (Unreal-style):**
```glsl
float dist = length(light.position - fragPosition);
float atten = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
```

---

## P4d: GPU Light Animation **(0.5 day)**

| File | Change |
|------|--------|
| `LitFog.fs` | Add `uniform float uTime; uniform float uLightPhase[32];` |
| `LitFog.fs` | Torch: `intensity *= 0.85 + 0.15 * sin(uTime*17 + phase) * cos(uTime*13)` |
| `LitFog.fs` | Fire: `intensity *= 0.7 + 0.3 * sin(uTime*5 + phase) * sin(uTime*7.3 + 1.2)` |
| `LitFog.fs` | Watery: `intensity *= 0.9 + 0.1 * sin(uTime*3 + phase)` |
| `Source/Core.hpp` | Pass `GetTime()` to shader via uniform |

Note: Animation is already implemented on CPU side in `LitLightning_Animate()`. GPU-side is optional redundancy for when light updates are stalled. CPU animation is sufficient for now — this phase may be deferred.

---

## P4e: Per-Light Visual Effects **(1 day)**

| Effect | Visual | Implementation in LitFog.fs |
|--------|--------|-----------------------------|
| WATERY | Caustic-like color shift + UV distortion | `diffuse.rgb += sin(uv.x*10 + uTime) * 0.05 * light.color.rgb` |
| TORCH | Warm flickering radial glow | `diffuse *= 0.8 + 0.2 * sin(uTime*17)` + warm hue shift |
| FIRE | Red/orange noise, intensity variation | `diffuse.rg *= 1.0 + 0.3 * noise(uv, uTime)` with red push |
| LAMP | Subtle warm pulse | `diffuse *= 0.95 + 0.05 * sin(uTime*2)` |

Read `light.effect` from shader uniform to select effect.

---

## P4f: Shadow Mapping **(DEFERRED)**

Not planned for this phase.

---

## P4g: Bump Mapping + Detail Textures + Gouraud **(2 days)**

| Feature | Files | Detail |
|---------|-------|--------|
| Normal/bump mapping | `Lighting.vs`, `LitFog.fs` | Pass TBN matrix from vertex; sample normal map in fragment; world-space lighting |
| Detail texture blending | `LitFog.fs` | Secondary UV sample, blend via `DF_DetailTexture` pattern (see Unreal xopengl reference) |
| Gouraud toggle | `LitFog.fs` | Optional vertex-shader lighting mode for performance |

**Shader additions for bump:**
```glsl
// Vertex shader: pass tangent, bitangent
out mat3 vTBN;

// Fragment shader: sample normal map
vec3 normal = texture(BumpMap, vTexCoord).rgb * 2.0 - 1.0;
vec3 worldNormal = normalize(vTBN * normal);
```

---

## P4h: Zone-Based Light Culling **(1 day)**

| File | Change |
|------|--------|
| `Source/Renderer/LitLightning.cpp` | Add radius-based culling: skip lights where `dist > radius * 2` |
| `Source/Pawn/OzPawnSystem.hpp` | Add `int zoneId = -1` field to `LightNode` |
| `Source/OzOzoneLoader.cpp` | Parse optional zone ID from OZONE light format |
| `LitFog.fs` | Early discard: `if (dist > light.radius) continue;` |

---

## Implementation Order

```
P0: Fix null-mesh crash  ── 5 min
P4c: UBO + attenuation   ── 2 days ← START HERE
P4e: Per-light effects    ── 1 day
P4d: GPU animation        ── 0.5 day (can merge with P4e)
P4g: Bump + detail        ── 2 days
P4h: Zone culling         ── 1 day
```

---

## Progress Log

| Date | Phase | Status | Notes |
|------|-------|--------|-------|
| 2026-07-22 | Plan created | ✅ | |
| | | | |
