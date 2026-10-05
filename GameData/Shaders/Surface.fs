#version 330

// Surface.fs — LitFog.fs plus the UT99-style per-face surface properties.
//
// Why a separate shader instead of adding uniforms to LitFog.fs:
//   * Lighting.vs/LitFog.fs is used by every mesh, particle, pickup, projectile
//     and the view-model. Touching its output changes the whole game.
//   * The surface properties are UNIFORMS, i.e. per draw call, so a brush with
//     per-face differences needs its own draw anyway. A dedicated program means
//     only surface-flagged brushes pay for it.
//
// uSurface is a bitfield; see World/SurfaceFlags.hpp for the bit allocation. The
// flag VALUES are duplicated here as #defines - they must stay in step with the
// header, and tests/Surface.test.cpp asserts the two legacy bits (3 and 4) that
// shipped worlds depend on.

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

#define MAX_LIGHTS 32
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

#define LIGHT_EFFECT_NONE   0
#define LIGHT_EFFECT_WATERY 1
#define LIGHT_EFFECT_TORCH  2
#define LIGHT_EFFECT_FIRE   3
#define LIGHT_EFFECT_LAMP   4

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float intensity;
    float radius;
    float innerCone;
    float outerCone;
    int effect;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;
uniform float uTime;

// --- per-surface uniforms ---------------------------------------------------
uniform int   uSurfaceFlags = 0;
uniform vec2  uPan          = vec2(0.0);   // texture units / second
uniform float uAlpha        = 1.0;
uniform float uAlphaCutoff  = 0.0;
uniform vec3  uGlow         = vec3(0.0);
uniform float uGlowScale    = 1.0;
// Gain applied by the full-bright branch (UNLIT / FAKE_LIT / FAKEBACKDROP).
// 1.0 means "the painted value exactly". The backdrop pass pushes 0.55, which is
// the gain the old DrawZoneGeometry uniform hack produced via ambient/10 - see
// OzoneLoader::DrawZoneGeometry. A per-pass scalar rather than a per-face one, so
// it is uploaded once per pass instead of once per draw.
uniform float uFullBright   = 1.0;

uniform float fogStart = 10.0;
uniform float fogEnd = 100.0;
uniform float fogDensity = 1.0;
uniform vec3 fogColor = vec3(0.7, 0.7, 0.8);
uniform float fogIntensity = 1.0;

// Must match oz::surface::SurfaceFlag in Source/World/SurfaceFlags.hpp
#define SF_INVISIBLE   (1 << 5)
#define SF_MASKED      (1 << 6)
#define SF_TRANSLUCENT (1 << 7)
#define SF_ALPHABLEND  (1 << 8)
#define SF_MODULATED   (1 << 9)
#define SF_TWO_SIDED   (1 << 10)
#define SF_UNLIT       (1 << 11)
#define SF_FAKE_LIT    (1 << 12)
#define SF_GLOW        (1 << 14)
#define SF_PAN_U       (1 << 18)
#define SF_PAN_V       (1 << 19)
#define SF_NO_FOG      (1 << 26)
#define SF_NO_SMOOTH   (1 << 25)
#define SF_FAKEBACKDROP (1 << 3)

bool HasFlag(int bit) { return (uSurfaceFlags & bit) != 0; }

float hash11(float p) {
    return fract(sin(p * 127.1 + 311.7) * 43758.5453);
}

float lightAttenuation(vec3 lightPos, vec3 fragPos, float radius) {
    float dist = distance(lightPos, fragPos);
    if (radius > 0.0 && dist > radius) return 0.0;
    float d = dist;
    return 1.0 / (1.0 + 0.09 * d + 0.032 * d * d);
}

float spotCone(vec3 lightPos, vec3 lightTarget, vec3 fragPos, float innerCone, float outerCone) {
    vec3 spotDir = normalize(lightTarget - lightPos);
    vec3 toFrag = normalize(fragPos - lightPos);
    float cosAngle = dot(-toFrag, spotDir);
    return smoothstep(outerCone, innerCone, cosAngle);
}

vec3 applyLightEffect(vec3 lightColor, float intensity, int effect, float phase, float ndotl) {
    vec3 col = lightColor;
    float f = intensity;

    if (effect == LIGHT_EFFECT_TORCH) {
        float flicker = 0.85 + 0.15 * sin(uTime * 17.0 + phase) * cos(uTime * 13.0 + phase * 0.5);
        f *= flicker;
        col *= vec3(1.0, 0.85, 0.6);
    }
    else if (effect == LIGHT_EFFECT_FIRE) {
        float pulse = 0.7 + 0.3 * sin(uTime * 5.0 + phase) * sin(uTime * 7.3 + phase * 1.2);
        float noise = hash11(phase + floor(uTime * 10.0)) * 0.2;
        f *= pulse + noise;
        col *= vec3(1.4, 0.7, 0.3);
    }
    else if (effect == LIGHT_EFFECT_WATERY) {
        float shimmer = 0.9 + 0.1 * sin(uTime * 3.0 + phase);
        f *= shimmer;
        col *= vec3(0.8, 0.9, 1.2);
    }
    else if (effect == LIGHT_EFFECT_LAMP) {
        float pulse = 0.95 + 0.05 * sin(uTime * 2.0 + phase * 1.5);
        f *= pulse;
        col *= vec3(1.1, 0.9, 0.7);
    }

    return col * f;
}

void main()
{
    // U/V pan. The speed lives in the flag, not in the value, so a zero speed is
    // a hard no-op rather than a per-frame multiply.
    vec2 uv = fragTexCoord;
    if (HasFlag(SF_PAN_U) || HasFlag(SF_PAN_V)) {
        if (HasFlag(SF_PAN_U)) uv.x += uPan.x * uTime;
        if (HasFlag(SF_PAN_V)) uv.y += uPan.y * uTime;
        // Wrap instead of letting the offset grow without bound. uTime is seconds
        // since startup, so at 0.5 units/s a long session reaches ~1800 texels of
        // offset and texture() sampling precision visibly degrades long before
        // that. fract() keeps the sampled argument small. Guarded by the pan flags
        // so an un-panned surface takes the identical path it always did.
        uv = fract(uv);
    }

    vec4 baseColor = texture(texture0, uv);

    // Alpha test (masked). Cheaper than blending and needs no depth sorting,
    // which is why it is a separate flag from translucent.
    if (HasFlag(SF_MASKED) && uAlphaCutoff > 0.0 && baseColor.a < uAlphaCutoff) discard;

    float alpha = baseColor.a * uAlpha;

    vec3 normal = normalize(fragNormal);
    if (HasFlag(SF_NO_SMOOTH)) normal = normal;   // reserved: flat shading hook

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightAccum = vec3(0.0);
    vec3 specAccum = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 lightDir = vec3(0.0);
            float attenuation = 1.0;
            float spotFactor = 1.0;

            if (lights[i].type == LIGHT_DIRECTIONAL)
                lightDir = -normalize(lights[i].target - lights[i].position);

            if (lights[i].type == LIGHT_POINT) {
                lightDir = normalize(lights[i].position - fragPosition);
                attenuation = lightAttenuation(lights[i].position, fragPosition, lights[i].radius);
            }

            if (lights[i].type == LIGHT_SPOT) {
                lightDir = normalize(lights[i].position - fragPosition);
                attenuation = lightAttenuation(lights[i].position, fragPosition, lights[i].radius);
                spotFactor = spotCone(lights[i].position, lights[i].target, fragPosition,
                                      lights[i].innerCone, lights[i].outerCone);
            }

            float NdotL = max(dot(normal, lightDir), 0.0);
            float phase = float(i) * 2.399;
            vec3 effColor = applyLightEffect(lights[i].color.rgb, lights[i].intensity,
                                              lights[i].effect, phase, NdotL);

            lightAccum += effColor * NdotL * attenuation * spotFactor;

            if (NdotL > 0.0) {
                vec3 halfDir = normalize(lightDir + viewDir);
                float NdotH = max(dot(normal, halfDir), 0.0);
                specAccum += effColor * pow(NdotH, 32.0) * attenuation * spotFactor;
            }
        }
    }

    vec4 amb = ambient;
    vec3 lit;

    if (HasFlag(SF_UNLIT) || HasFlag(SF_FAKE_LIT) || HasFlag(SF_FAKEBACKDROP)) {
        // Full-bright. This is the PER-DRAW replacement for the old
        // DrawZoneGeometry hack, which forced colDiffuse/ambient on the shared
        // LitFog program and then "restored" a constant - so everything drawn
        // after the sky pass inherited it. Here the override lives in this
        // draw's own uniforms and dies with the draw.
        //
        // uFullBright is 1.0 for the world pass and 0.55 for the backdrop pass;
        // see the declaration for why. Note this branch deliberately does NOT
        // include a light contribution: a painted backdrop exists to be seen at
        // its authored value, and the old shared-uniform path added
        // `baseColor * colDiffuse * lightAccum` on top of the gain, so a
        // directional sun blew out one panel of a painting and left the rest of
        // it dim. Dropping that term is what makes the flag mean what it says.
        lit = baseColor.rgb * colDiffuse.rgb * uFullBright;
    } else {
        lit = baseColor.rgb * ((colDiffuse.rgb + specAccum) * lightAccum);
        lit += baseColor.rgb * (amb.rgb / 10.0) * colDiffuse.rgb;
    }

    // Modulated: multiply against what is already in the framebuffer (set up by
    // the renderer's BLEND_MULTIPLY state).
    if (HasFlag(SF_MODULATED)) lit *= colDiffuse.rgb;

    // Self-illumination. Added AFTER lighting so a glowing surface stays bright
    // in an unlit room, which is the whole point of the flag.
    if (HasFlag(SF_GLOW)) lit += uGlow * uGlowScale;

    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));

    // Fog, unless the surface opts out. SF_ALPHABLEND opts out too - it is the
    // documented "blend but do not fog" flag (SurfaceFlags.hpp), which TRANSLUCENT
    // deliberately is NOT: a sheet of glass in a foggy corridor should still
    // recede, whereas an AlphaBlend surface is used for UI-ish decals and signage
    // that must stay legible at any distance. Before this distinction the two
    // flags were pixel-identical and the header comment was simply false.
    if (!HasFlag(SF_NO_FOG) && !HasFlag(SF_ALPHABLEND)) {
        float fogDist = length(viewPos - fragPosition);
        float fogFactor = clamp((fogDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
        fogFactor *= fogDensity * fogIntensity;
        lit = mix(lit, fogColor, fogFactor);
    }

    if (HasFlag(SF_ALPHABLEND) || HasFlag(SF_TRANSLUCENT))
        finalColor = vec4(lit, alpha);
    else
        finalColor = vec4(lit, 1.0);
}
