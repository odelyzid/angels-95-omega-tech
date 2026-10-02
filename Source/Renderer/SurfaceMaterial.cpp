#include "SurfaceMaterial.hpp"
#include "LitLightning.hpp"
#include "rlgl.h"
#include "../Package/PackageAssetLoader.hpp"
#include "../Log.hpp"
#include <cstring>

namespace oz {

SurfaceMaterial& SurfaceMaterial::Instance() {
    static SurfaceMaterial inst;
    return inst;
}

void SurfaceMaterial::Init(const char* shaderDir) {
    if (m_loaded) return;
    std::string dir = shaderDir ? shaderDir : "GameData/Shaders/";
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    m_shader = LoadShaderWithFallback((dir + "Surface.vs").c_str(),
                                      (dir + "Surface.fs").c_str());
    if (m_shader.id == 0) {
        // Not fatal: brushes keep the original DrawModel path, so a world still
        // loads and renders, just without the surface effects. Say so loudly -
        // a silently-missing Surface.fs would look like "the flags do nothing".
        OZ_WARN("SurfaceMaterial: Surface.vs/fs failed to load from '%s' - "
                "surface flags will be ignored", dir.c_str());
        return;
    }
    CacheLocations();
    m_loaded = true;
    OZ_INFO("SurfaceMaterial: shader loaded (id=%d)", m_shader.id);
}

void SurfaceMaterial::Shutdown() {
    if (m_loaded && m_shader.id > 0) UnloadShader(m_shader);
    m_shader = Shader{0};
    m_loaded = false;
    m_blendOpen = m_cullOff = m_depthMaskOff = false;
}

void SurfaceMaterial::CacheLocations() {
    m_lightsLoc    = GetShaderLocation(m_shader, "lights[0].position");
    m_ambientLoc   = GetShaderLocation(m_shader, "ambient");
    m_viewPosLoc   = GetShaderLocation(m_shader, "viewPos");
    m_timeLoc      = GetShaderLocation(m_shader, "uTime");
    m_flagsLoc     = GetShaderLocation(m_shader, "uSurfaceFlags");
    m_panLoc       = GetShaderLocation(m_shader, "uPan");
    m_alphaLoc     = GetShaderLocation(m_shader, "uAlpha");
    m_cutoffLoc    = GetShaderLocation(m_shader, "uAlphaCutoff");
    m_glowLoc      = GetShaderLocation(m_shader, "uGlow");
    m_glowScaleLoc = GetShaderLocation(m_shader, "uGlowScale");
}

void SurfaceMaterial::UpdateFrame(std::vector<LightNode>& lights,
                                  Camera3D camera, float dt) {
    if (m_shader.id == 0) return;

    // Same 32-light submission the world uses, pointed at this program. Reusing
    // LitLightning_Update is deliberate: a second copy of that loop would drift
    // and a surface-lit brush would disagree with the room around it.
    LitLightning_Update(lights, m_shader, camera, dt);

    const float viewPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    if (m_viewPosLoc >= 0)
        SetShaderValue(m_shader, m_viewPosLoc, viewPos, SHADER_UNIFORM_VEC3);

    // Ambient is not published by reference; the same value the world uses is
    // stored on OzoneLoader by whoever owns the LitFog uniform. Unlit surfaces
    // ignore it entirely, which is the common case for surface-flagged brushes.
    const float amb[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    if (m_ambientLoc >= 0)
        SetShaderValue(m_shader, m_ambientLoc, amb, SHADER_UNIFORM_VEC4);

    if (m_timeLoc >= 0) {
        const float t = (float)GetTime();
        SetShaderValue(m_shader, m_timeLoc, &t, SHADER_UNIFORM_FLOAT);
    }
}

void SurfaceMaterial::ApplyUniforms(const surface::SurfaceProps& p) {
    if (m_shader.id == 0) return;

    const int flags = (int)p.flags;
    if (m_flagsLoc >= 0)
        SetShaderValue(m_shader, m_flagsLoc, &flags, SHADER_UNIFORM_INT);

    const float pan[2] = { p.panU, p.panV };
    if (m_panLoc >= 0)
        SetShaderValue(m_shader, m_panLoc, pan, SHADER_UNIFORM_VEC2);

    if (m_alphaLoc >= 0)
        SetShaderValue(m_shader, m_alphaLoc, &p.alpha, SHADER_UNIFORM_FLOAT);
    if (m_cutoffLoc >= 0)
        SetShaderValue(m_shader, m_cutoffLoc, &p.alphaCutoff, SHADER_UNIFORM_FLOAT);
    if (m_glowLoc >= 0) {
        const float g[3] = { p.glowR, p.glowG, p.glowB };
        SetShaderValue(m_shader, m_glowLoc, g, SHADER_UNIFORM_VEC3);
    }
    if (m_glowScaleLoc >= 0)
        SetShaderValue(m_shader, m_glowScaleLoc, &p.glowScale, SHADER_UNIFORM_FLOAT);
}

bool SurfaceMaterial::NeedsStateChange(const surface::SurfaceProps& p) {
    using namespace oz::surface;
    return p.Has(SURF_TRANSLUCENT) || p.Has(SURF_ALPHABLEND) ||
           p.Has(SURF_MODULATED)   || p.Has(SURF_TWO_SIDED);
}

void SurfaceMaterial::BeginSurfaceState(const surface::SurfaceProps& p) {
    using namespace oz::surface;
    if (m_blendOpen || m_cullOff || m_depthMaskOff) return;  // never nest

    if (p.Has(SURF_MODULATED)) {
        // raylib has no BLEND_MULTIPLY, so open the blend and let the shader
        // multiply against whatever the framebuffer already holds. Depth write
        // stays on: a modulated surface is still an occluder.
        BeginBlendMode(BLEND_ALPHA);
        m_blendOpen = true;
    } else if (p.Has(SURF_TRANSLUCENT) || p.Has(SURF_ALPHABLEND)) {
        BeginBlendMode(BLEND_ALPHA);
        m_blendOpen = true;
        // A blended surface must not occlude what is behind it, or the second
        // translucent brush in the room disappears behind the first.
        rlDisableDepthMask();
        m_depthMaskOff = true;
    }
    if (p.Has(SURF_TWO_SIDED)) {
        rlDisableBackfaceCulling();
        m_cullOff = true;
    }
}

void SurfaceMaterial::EndSurfaceState() {
    if (m_cullOff)      { rlEnableBackfaceCulling();  m_cullOff = false; }
    if (m_depthMaskOff) { rlEnableDepthMask();         m_depthMaskOff = false; }
    if (m_blendOpen)    { EndBlendMode();              m_blendOpen = false; }
}

} // namespace oz
