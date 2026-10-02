#pragma once
// ---------------------------------------------------------------------------
// Renderer/SurfaceMaterial — the CPU half of the UT99-style per-face surface
// system (see World/SurfaceFlags.hpp for the data model).
//
// Owns GameData/Shaders/Surface.{vs,fs} and turns one SurfaceProps into shader
// uniforms + GL state for a single draw. A brush with per-face differences is
// drawn as up to six DrawMesh calls (one per face) because the surface
// properties are uniforms, i.e. per draw call.
//
// Design note — why a second shader program rather than extra uniforms on
// LitFog: Lighting.vs/LitFog.fs drives every mesh, particle, pickup, projectile
// and the view-model. Adding branches to it would change the whole game's
// output and cost a uniform check per fragment everywhere. Only brushes that
// actually carry a surface flag reach this path; everything else keeps the
// original single DrawModel fast path.
// ---------------------------------------------------------------------------

#include "raylib.h"
#include "../World/SurfaceFlags.hpp"
#include <vector>

// Forward-declared: SurfaceMaterial only needs to know it is a light source.
struct LightNode;

namespace oz {

class SurfaceMaterial {
public:
    static SurfaceMaterial& Instance();

    // Load Surface.vs/Surface.fs through the package-aware loader. Safe to call
    // repeatedly; a second call while loaded is a no-op.
    void Init(const char* shaderDir = "GameData/Shaders/");
    void Shutdown();
    bool Ready() const { return m_shader.id > 0; }
    Shader Get() const { return m_shader; }

    // Re-submit the world lights, view position and ambient to the surface
    // program. Must run once per frame before any surface draw, otherwise the
    // lights[] array on this program holds whatever the last world set.
    //
    // This deliberately reuses LitLightning_Update rather than duplicating the
    // 32-light loop: two copies of that loop would drift, and a surface brush
    // that was lit by a different set of lights than the room around it is
    // exactly the kind of bug that is very hard to see.
    //
    // Non-const because LitLightning_Update sorts the light vector in place.
    void UpdateFrame(std::vector<LightNode>& lights, Camera3D camera, float dt);

    // Push one face's properties. Leaves GL blend/depth state alone; the caller
    // brackets the draw with BeginSurfaceState/EndSurfaceState.
    void ApplyUniforms(const surface::SurfaceProps& p);

    // Bracket a translucent / masked / modulated / two-sided draw. No-op for
    // flags that do not change GL state.
    void BeginSurfaceState(const surface::SurfaceProps& p);
    void EndSurfaceState();

    // True when `p` needs the GL state changed (blend mode, cull, depth mask).
    static bool NeedsStateChange(const surface::SurfaceProps& p);

private:
    SurfaceMaterial() = default;
    void CacheLocations();

    Shader m_shader{0};
    bool   m_loaded = false;

    int m_lightsLoc = -1;
    int m_ambientLoc = -1;
    int m_viewPosLoc = -1;
    int m_timeLoc = -1;
    int m_flagsLoc = -1;
    int m_panLoc = -1;
    int m_alphaLoc = -1;
    int m_cutoffLoc = -1;
    int m_glowLoc = -1;
    int m_glowScaleLoc = -1;

    // Blend/depth/cull state actually changed by the currently open bracket, so
    // EndSurfaceState restores exactly what it found instead of guessing.
    bool m_blendOpen = false;
    bool m_cullOff = false;
    bool m_depthMaskOff = false;
};

} // namespace oz
