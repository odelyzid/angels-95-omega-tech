#pragma once
#include "raylib.h"
#include "raymath.h"
#include "rlights/rlights.h"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// Light types matching rlights.h LightType enum
enum class LitLightType : int {
    DIRECTIONAL = 0,
    POINT = 1,
    SPOT = 2
};

// Light effect types matching rlights.h LightEffect enum
enum class LitLightEffect : int {
    NONE = 0,
    WATERY = 1,
    TORCH = 2,
    FIRE = 3,
    LAMP = 4
};

// LightNode — a first-class light entity managed by PawnSystem
struct LightNode {
    uint32_t id = 0;
    bool active = false;

    LitLightType type = LitLightType::POINT;
    Vector3 position{0, 0, 0};
    Vector3 target{0, 0, 0};
    Color color = WHITE;

    float intensity = 1.0f;
    float radius = 50.0f;

    // Spot light cone (cosine of half-angles)
    float innerCone = 0.95f;   // cos(~18 deg)
    float outerCone = 0.80f;   // cos(~37 deg)

    // Light effect animation
    LitLightEffect effect = LitLightEffect::NONE;
    float phase = 0.0f;
    float period = 1.0f;

    // Static = baked into lightmap, skipped per-frame update
    bool isStatic = false;
    bool castShadow = false;    // deferred to later shadow phase
    bool flare = false;         // draws a bright additive billboard at the light
    bool corona = false;        // draws a larger, dimmer halo billboard

    int zoneId = -1;            // -1 = affects all zones, 0+ = only affects matching zone
    std::string name;           // editor label
};

// ---------------------------------------------------------------------------
// Core runtime functions
// ---------------------------------------------------------------------------

// Update all active lights: animate dynamics, sort by distance, submit to shader
void LitLightning_Update(std::vector<LightNode>& lights, Shader shader, Camera3D camera, float dt);

// Full per-frame lighting pass: uploads the camera view uniform, submits
// `lights` via LitLightning_Update, applies a directional camera fill light
// only when no world light is active, and advances the uTime uniform.
// `fallbackDirectional` is the headlight slot (engine GameLight[0]).
// Called once per frame from UpdateLightSources().
void LitLightning_UpdateFrame(std::vector<LightNode>& lights, Shader shader,
                              Camera3D camera, Light& fallbackDirectional, float dt);

// Animate a dynamic light based on its effect type and phase/period
void LitLightning_Animate(LightNode& node, float dt);

// Sort lights by distance to camera (nearest first)
void LitLightning_SortByDistance(std::vector<LightNode>& lights, Vector3 cameraPos);
