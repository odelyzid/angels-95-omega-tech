#include "LitLightning.hpp"
#include "../rlights/rlights.h"
#include "../Log.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Animate a dynamic light based on effect type
// ---------------------------------------------------------------------------
void LitLightning_Animate(LightNode& node, float dt) {
    if (node.isStatic) return;

    node.phase += dt * (1.0f / fmaxf(node.period, 0.01f));

    switch (node.effect) {
        case LitLightEffect::TORCH:
            node.intensity = 0.85f + 0.15f * sinf(node.phase * 17.0f) * cosf(node.phase * 13.0f);
            node.radius = 45.0f + 10.0f * sinf(node.phase * 11.0f);
            break;

        case LitLightEffect::FIRE:
            node.intensity = 0.7f + 0.3f * sinf(node.phase * 5.0f) * sinf(node.phase * 7.3f + 1.2f);
            node.radius = 40.0f + 20.0f * sinf(node.phase * 4.0f);
            break;

        case LitLightEffect::WATERY:
            node.intensity = 0.9f + 0.1f * sinf(node.phase * 3.0f);
            break;

        case LitLightEffect::LAMP:
            node.intensity = 0.95f + 0.05f * sinf(node.phase * 2.0f + 1.5f);
            break;

        default:
            node.intensity = 1.0f;
            break;
    }
}

// ---------------------------------------------------------------------------
// Build an rlights Light struct from a LightNode
// ---------------------------------------------------------------------------
static Light BuildRLight(const LightNode& node, Shader shader, int index) {
    Light light = {0};
    light.type = (int)node.type;
    light.enabled = node.active;
    light.position = node.position;
    light.target = node.target;
    float intensity = fmaxf(node.intensity, 0.0f);
    light.color = (Color){
        (unsigned char)fminf(node.color.r * intensity, 255),
        (unsigned char)fminf(node.color.g * intensity, 255),
        (unsigned char)fminf(node.color.b * intensity, 255),
        node.color.a
    };
    light.intensity = intensity;
    light.intensity = intensity;
    light.radius = node.radius;

    light.enabledLoc = GetShaderLocation(shader, TextFormat("lights[%i].enabled", index));
    light.typeLoc = GetShaderLocation(shader, TextFormat("lights[%i].type", index));
    light.positionLoc = GetShaderLocation(shader, TextFormat("lights[%i].position", index));
    light.targetLoc = GetShaderLocation(shader, TextFormat("lights[%i].target", index));
    light.colorLoc = GetShaderLocation(shader, TextFormat("lights[%i].color", index));
    light.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", index));
    light.intensityLoc = GetShaderLocation(shader, TextFormat("lights[%i].intensity", index));
    light.radiusLoc = GetShaderLocation(shader, TextFormat("lights[%i].radius", index));

    return light;
}

// ---------------------------------------------------------------------------
// Sort lights by distance from camera (nearest first)
// Also culls lights beyond their effective radius
// ---------------------------------------------------------------------------
void LitLightning_SortByDistance(std::vector<LightNode>& lights, Vector3 cameraPos) {
    std::sort(lights.begin(), lights.end(),
        [cameraPos](const LightNode& a, const LightNode& b) {
            // Sort order: active, not culled, nearest
            if (a.active != b.active) return a.active;

            // Radius culling: lights farther than 2x radius from camera are pushed to end
            float ra = (a.radius > 0.0f) ? a.radius * 2.0f : 1e9f;
            float rb = (b.radius > 0.0f) ? b.radius * 2.0f : 1e9f;
            float da = Vector3Distance(a.position, cameraPos);
            float db = Vector3Distance(b.position, cameraPos);
            bool cullA = (da > ra);
            bool cullB = (db > rb);
            if (cullA != cullB) return !cullA;

            return da < db;
        });
}

// ---------------------------------------------------------------------------
// Main update: animate, sort, and submit all lights to the shader
// ---------------------------------------------------------------------------
void LitLightning_Update(std::vector<LightNode>& lights, Shader shader, Camera3D camera, float dt) {
    // Animate dynamic lights
    for (auto& node : lights) {
        if (node.active && !node.isStatic)
            LitLightning_Animate(node, dt);
    }

    // Sort active lights by distance to camera
    LitLightning_SortByDistance(lights, camera.position);

    // Submit lights to shader
    int submitted = 0;
    for (auto& node : lights) {
        if (!node.active) continue;
        if (submitted >= MAX_LIGHTS) break;
        Light rlight = BuildRLight(node, shader, submitted);
        UpdateLightValues(shader, rlight);
        submitted++;
    }

    // Disable remaining light slots
    for (int i = submitted; i < MAX_LIGHTS; i++) {
        Light dummy = {0};
        dummy.enabledLoc = GetShaderLocation(shader, TextFormat("lights[%i].enabled", i));
        dummy.typeLoc = GetShaderLocation(shader, TextFormat("lights[%i].type", i));
        dummy.enabled = false;
        UpdateLightValues(shader, dummy);
    }
}
