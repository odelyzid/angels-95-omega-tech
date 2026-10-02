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
    m_blendOpen = m_depthMaskOff = false;
    m_cullScope.reset();
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
    // Surface.fs declares and uses all five fog uniforms (see the SF_NO_FOG block
    // in its fragment stage) but none were cached here, so surface-flagged
    // brushes always fogged at the GLSL defaults — 10/100 with density 1 —
    // regardless of what the world's own fog was set to.
    m_fogStartLoc     = GetShaderLocation(m_shader, "fogStart");
    m_fogEndLoc       = GetShaderLocation(m_shader, "fogEnd");
    m_fogDensityLoc   = GetShaderLocation(m_shader, "fogDensity");
    m_fogColorLoc     = GetShaderLocation(m_shader, "fogColor");
    m_fogIntensityLoc = GetShaderLocation(m_shader, "fogIntensity");
}

void SurfaceMaterial::SetFog(const float color[3], float start, float end,
                             float density, float intensity) {
    m_fogColor[0] = color[0];
    m_fogColor[1] = color[1];
    m_fogColor[2] = color[2];
    m_fogStart   = start;
    m_fogEnd     = end;
    m_fogDensity = density;
    m_fogIntensity = intensity;
    m_fogDirty   = true;
}

void SurfaceMaterial::UpdateFrame(std::vector<LightNode>& lights,
                                  Camera3D camera, float dt,
                                  const float ambient[4]) {
    if (m_shader.id == 0) return;

    // Same 32-light submission the world uses, pointed at this program. Reusing
    // LitLightning_Update is deliberate: a second copy of that loop would drift
    // and a surface-lit brush would disagree with the room around it.
    LitLightning_Update(lights, m_shader, camera, dt);

    const float viewPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    if (m_viewPosLoc >= 0)
        SetShaderValue(m_shader, m_viewPosLoc, viewPos, SHADER_UNIFORM_VEC3);

    // The ambient the caller owns, i.e. the same value the world pushes to LitFog.
    // This used to be a hardcoded {0.1,0.1,0.1,1} with a comment claiming the
    // value lived on OzoneLoader — there was no accessor and it never read one, so
    // a surface-flagged brush was lit by a flat 0.1 no matter how bright or dark
    // the room actually was. Unlit surfaces ignore the uniform entirely, which is
    // the common case for surface-flagged brushes.
    if (m_ambientLoc >= 0 && ambient)
        SetShaderValue(m_shader, m_ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Push fog only when it changed: UpdateFrame runs every frame, and five
    // redundant uniform uploads per frame per material is pure overhead for a
    // value that only changes when the user edits it.
    if (m_fogDirty) {
        if (m_fogColorLoc     >= 0) SetShaderValue(m_shader, m_fogColorLoc,     m_fogColor,     SHADER_UNIFORM_VEC3);
        if (m_fogStartLoc     >= 0) SetShaderValue(m_shader, m_fogStartLoc,     &m_fogStart,     SHADER_UNIFORM_FLOAT);
        if (m_fogEndLoc       >= 0) SetShaderValue(m_shader, m_fogEndLoc,       &m_fogEnd,       SHADER_UNIFORM_FLOAT);
        if (m_fogDensityLoc   >= 0) SetShaderValue(m_shader, m_fogDensityLoc,   &m_fogDensity,   SHADER_UNIFORM_FLOAT);
        if (m_fogIntensityLoc >= 0) SetShaderValue(m_shader, m_fogIntensityLoc, &m_fogIntensity, SHADER_UNIFORM_FLOAT);
        m_fogDirty = false;
    }

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
    if (m_blendOpen || m_cullScope || m_depthMaskOff) return;  // never nest

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
        // Scoped, not a raw call plus a private bool: the tracked flag in
        // Renderer/CullState.hpp is the single authority, so a two-sided face
        // restores whatever the caller had set rather than assuming "on".
        m_cullScope.emplace();
    }
}

void SurfaceMaterial::EndSurfaceState() {
    if (m_cullScope) m_cullScope.reset();   // restores the pre-draw state
    if (m_depthMaskOff) { rlEnableDepthMask();         m_depthMaskOff = false; }
    if (m_blendOpen)    { EndBlendMode();              m_blendOpen = false; }
}

} // namespace oz
