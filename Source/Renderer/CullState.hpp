#pragma once
#include "raylib.h"
#include "rlgl.h"   // rlEnable/rlDisableBackfaceCulling live here, not raylib.h

// ---------------------------------------------------------------------------
// oz::CullState — the single authority on backface-culling state.
//
// WHY THIS EXISTS
// ---------------
// The engine reached a point where three different modules each toggled GL
// backface culling with raw `rlEnableBackfaceCulling()` / `rlDisable…` calls
// and each remembered the state it had set in its own private bool. That is
// exactly how a "restore" ends up restoring the wrong thing:
//
//   * `Core.hpp` turned culling ON after the skybox and never turned it off,
//     so every mesh drawn later in the frame was culled.
//   * `SurfaceMaterial` tracked `m_cullOff` independently.
//   * AngelEd made the call from inside `if (s_skyTex.id > 0)`, so a world
//     with no skybox texture never set it at all and inherited whatever the
//     previous frame happened to leave behind.
//
// Imported models are the casualty. `tools/convert_fbx.ps1` bakes a Z-up ->
// Y-up rotation into the character/pawn/weapon GLBs, which flips triangle
// winding on a lot of them; drawn with culling on, such a model is completely
// invisible. Generated OZONE brushes keep correct winding, so they are fine
// either way — which is why the bug read as "some models are missing" rather
// than "the renderer is broken".
//
// THE RULE
// --------
// Exactly one tracked flag. Nobody calls `rlEnable/rlDisableBackfaceCulling()`
// directly any more; they call `SetBackfaceCulling()` so the flag stays true.
// A scope that wants culling off uses `ScopedCullOff`, which restores the
// tracked value rather than assuming it was on.
//
// This header is deliberately dependency-free and inline-only: it is included
// from the client, the editor and the headless tools, and adding a .cpp would
// mean touching the root Makefile, AngelEd/Makefile AND the inline g++ list in
// .github/workflows/ci.yml (they have drifted before — see commit fcbf049).
// ---------------------------------------------------------------------------

namespace oz {

// Tracked state. Starts matching raylib's own default (culling enabled), which
// is also what the engine wants for the generated world pass.
inline bool& CullFlagRef() {
    static bool enabled = true;
    return enabled;
}

// Set the GL culling state and keep the tracked flag in step.
inline void SetBackfaceCulling(bool on) {
    CullFlagRef() = on;
    if (on) rlEnableBackfaceCulling();
    else    rlDisableBackfaceCulling();
}

inline bool BackfaceCullingEnabled() { return CullFlagRef(); }

// RAII: culling off for the duration of a scope, restored to whatever the
// tracked value was on entry. Used by oz::Mesh (imported assets) and by the
// per-face surface path.
class ScopedCullOff {
public:
    ScopedCullOff() : m_prev(CullFlagRef()) { SetBackfaceCulling(false); }
    ~ScopedCullOff() { SetBackfaceCulling(m_prev); }
    ScopedCullOff(const ScopedCullOff&) = delete;
    ScopedCullOff& operator=(const ScopedCullOff&) = delete;
private:
    bool m_prev;
};

} // namespace oz
