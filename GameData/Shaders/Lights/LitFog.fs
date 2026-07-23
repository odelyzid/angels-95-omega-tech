#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Optional detail texture (bind at application level)
uniform sampler2D DetailTexture;
uniform vec2 DetailScale = vec2(16.0);
uniform float DetailBlend = 0.3;

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

uniform float fogStart = 10.0;
uniform float fogEnd = 100.0;
uniform float fogDensity = 1.0;
uniform vec3 fogColor = vec3(0.7, 0.7, 0.8);
uniform float fogIntensity = 1.0;

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

// Schlick Fresnel approximation
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
    vec4 baseColor = texture(texture0, fragTexCoord);

    // Apply detail texture using world-space UV
    if (DetailBlend > 0.0) {
        vec2 detailUV = fragWorldPos.xz * DetailScale;
        vec4 detailColor = texture(DetailTexture, detailUV);
        baseColor.rgb = mix(baseColor.rgb, baseColor.rgb * detailColor.rgb, DetailBlend);
    }

    vec3 normal = normalize(fragNormal);
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

            // Blinn-Phong specular
            if (NdotL > 0.0) {
                vec3 halfDir = normalize(lightDir + viewDir);
                float NdotH = max(dot(normal, halfDir), 0.0);
                float spec = pow(NdotH, 32.0);

                // Fresnel: stronger specular at grazing angles
                float F = fresnelSchlick(max(dot(viewDir, halfDir), 0.0), 0.04);
                specAccum += effColor * spec * F * attenuation * spotFactor;
            }
        }
    }

    vec3 litColor = baseColor.rgb * ((colDiffuse.rgb + specAccum) * lightAccum);
    litColor += baseColor.rgb * (ambient.rgb / 10.0) * colDiffuse.rgb;

    // Gamma
    litColor = pow(litColor, vec3(1.0 / 2.2));

    // Fog
    float fogDist = length(viewPos - fragPosition);
    float fogFactor = clamp((fogDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor *= fogDensity * fogIntensity;

    // Light-influenced fog
    vec3 lightInfluence = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (lights[i].enabled == 1 && lights[i].type != LIGHT_DIRECTIONAL) {
            float dist = distance(lights[i].position, fragPosition);
            if (lights[i].radius > 0.0 && dist > lights[i].radius) continue;
            float influence = 1.0 / (1.0 + dist * 0.1);
            float phase = float(i) * 2.399;
            vec3 effColor = applyLightEffect(lights[i].color.rgb, lights[i].intensity,
                                              lights[i].effect, phase, 1.0);
            lightInfluence += effColor * influence * 0.3;
        }
    }

    vec3 finalFogColor = fogColor + lightInfluence;
    vec3 finalLit = mix(litColor, finalFogColor, fogFactor);

    finalColor = vec4(finalLit, baseColor.a);
}
