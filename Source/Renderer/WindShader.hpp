#pragma once
#include "raylib.h"

// Wind uniform upload helper for the Wind.vs foliage shader.
// windParams = (dirX, dirZ, strength, frequency). Strength 0 disables sway.
// baseY/height are model-space bounds so the sway weight is height-based.
namespace oz {

inline void SetWindUniforms(Shader shader, const Vector4& params, float time,
                            float baseY, float height) {
    if (shader.id == 0) return;

    // Uniform locations are stable per program; cache them (avoids a string
    // lookup per foliage draw).
    static unsigned int s_lastId = 0;
    static int s_locParams = -1, s_locTime = -1, s_locBaseY = -1, s_locHeight = -1;
    if (shader.id != s_lastId) {
        s_lastId = shader.id;
        s_locParams = GetShaderLocation(shader, "windParams");
        s_locTime   = GetShaderLocation(shader, "windTime");
        s_locBaseY  = GetShaderLocation(shader, "windBaseY");
        s_locHeight = GetShaderLocation(shader, "windHeight");
    }

    SetShaderValue(shader, s_locParams, &params, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, s_locTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_locBaseY, &baseY, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_locHeight, &height, SHADER_UNIFORM_FLOAT);
}

} // namespace oz
