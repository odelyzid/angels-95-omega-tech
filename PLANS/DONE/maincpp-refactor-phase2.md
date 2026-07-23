# Phase 2: Zone Volume Integration

> **Status:** Active | **Started:** 2026-07-22 | **Target:** 0.5 day

## Objective
Wire zone volume detection (`ZoneVolumeNode` from `PawnSystem`) into the player movement loop in `Source/Main.cpp` so water, ladder, reverb, and sound zones affect gameplay.

---

## Changes Made

### Source/Main.cpp — Movement block (L802-870)

Added zone detection BEFORE the jump/fly/noclip block:

```cpp
// --- Zone volume detection + movement effects ---
{
    float dt = GetFrameTime();
    Vector3 playerPos = OmegaTechData.MainCamera.position;
    ZoneVolumeNode* activeZone = PawnSystem::Instance().CheckZoneCollision(playerPos, OmegaPlayer.PlayerBounds);
    OmegaPlayer.inWater = false;

    if (activeZone) {
        switch (activeZone->zoneType) {
            case ZoneType::ZONE_WATER:
                OmegaPlayer.inWater = true;
                break;
            case ZoneType::ZONE_LADDER:
                OmegaPlayer.velocityY = 0.0f;
                OmegaPlayer.onGround = false;
                if (IsKeyDown(KEY_W)) camera.y += 6.0f * dt;
                if (IsKeyDown(KEY_S)) camera.y -= 6.0f * dt;
                break;
            case ZoneType::ZONE_REVERB:
                // Placeholder for future DSP
                break;
            default:
                break;
        }
    }
}
```

### Source/Main.cpp — Modified jump/fly/noclip block

Added a new `else if (OmegaPlayer.inWater)` branch after flying:
- Reduced gravity: `-8.0f * dt` (vs normal `-20.0f * dt`)
- Water drag: `velocityY *= 0.95f`
- Space to swim upward at 5.0f (vs jump at 8.0f)

### Source/Pawn/Player.hpp

Added `bool inWater = false;` flag for other systems to query.

---

## Zone Effects Implemented

| Zone Type | Effect | Status |
|-----------|--------|--------|
| `ZONE_WATER` | Reduced gravity (-8 vs -20), drag (0.95), swim up (Space) | ✅ |
| `ZONE_LADDER` | Gravity disabled, W/S climbs vertically at 6.0f/s | ✅ |
| `ZONE_REVERB` | Placeholder for audio DSP | 🟡 Placeholder |
| `ZONE_SKY` | Already handled in Core.hpp (skybox rendering) | ✅ Pre-existing |
| `ZONE_GAMEPLAY_SOUND` | Already handled in Core.hpp (music crossfade) | ✅ Pre-existing |

---

## Verification
- [ ] Player in water zone: reduced gravity, drag, swim upward
- [ ] Player in ladder zone: vertical climb with W/S, no fall
- [ ] Player in reverb zone: no crash (no-op)
- [ ] Player outside zones: normal gravity/jump
- [ ] Compiles clean

---

## Progress Log

| Date | Step | Status | Notes |
|------|------|--------|-------|
| 2026-07-22 | Plan created | ✅ | |
| 2026-07-22 | Water zone physics | ✅ | Reduced gravity + drag + swim |
| 2026-07-22 | Ladder zone climbing | ✅ | W/S vertical movement, no gravity |
| 2026-07-22 | Reverb placeholder | ✅ | No-op, safe |
| 2026-07-22 | Compile check | ✅ | g++ -std=c++20 clean |
