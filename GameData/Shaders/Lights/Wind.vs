#version 330

// Wind-aware variant of Lighting.vs. Identical varyings/outputs so it pairs
// with LitFog.fs; adds a height-weighted sway driven by the WindZone uniforms.
// Non-foliage meshes use Lighting.vs unchanged.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Wind (set per foliage draw; strength 0 => no displacement)
uniform vec4 windParams;   // xy = world XZ direction, z = strength, w = frequency
uniform float windTime;
uniform float windBaseY;    // model-space Y where sway begins (bounds min)
uniform float windHeight;   // model-space height of the sway range

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragWorldPos;

void main()
{
    vec3 pos = vertexPosition;

    // Sway scales with height above the mesh base so trunks/roots stay planted.
    float h = clamp((pos.y - windBaseY) / max(windHeight, 0.001), 0.0, 1.0);
    float sway = h * h;
    float phase = windTime * windParams.w + pos.x * 0.15 + pos.z * 0.15;
    float gust = 0.7 + 0.3 * sin(phase);
    vec2 off = windParams.xy * (windParams.z * gust * sway);
    pos.x += off.x;
    pos.z += off.y;

    vec4 worldPos = matModel * vec4(pos, 1.0);
    fragPosition = vec3(worldPos);
    fragWorldPos = vec3(worldPos);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));

    gl_Position = mvp * vec4(pos, 1.0);
}
