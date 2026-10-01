#pragma once

namespace oz {

// ---------------------------------------------------------------------------
// Shared combat math.
//
// Lives in a raylib-free header so the client and the dedicated server compile
// the *same* formula. These two used to disagree outright:
//
//   client (Core.hpp)  damage * (100 / (100 + defense))   -- diminishing returns
//   server (GameState) damage * 0.8                       -- flat 20% reduction
//
// So a player took different damage depending on whether a server was running,
// and the server's value also ignored the defense stat entirely (equipment
// granted by .ozls defs was meaningless for anyone connected).
//
// defense is a flat reduction score: 0 = no mitigation, 100 = half damage,
// 200 = a third. Kept additive-friendly so per-piece values can be summed.
// ---------------------------------------------------------------------------

// Damage after armor. Never returns negative for non-negative input.
inline float MitigateDamage(float rawDamage, float defense) {
    if (rawDamage <= 0.0f) return 0.0f;
    const float d = defense > 0.0f ? defense : 0.0f;
    // Same diminishing-returns shape the client always used, so switching to it
    // is a behaviour change on the server but not a divergence between ends.
    return rawDamage * (100.0f / (100.0f + d));
}

} // namespace oz