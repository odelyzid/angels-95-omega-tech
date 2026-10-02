#version 330

// Surface.vs — vertex stage for OZONE brush surfaces with per-face properties.
//
// A copy of Lighting.vs (same uniforms, same varyings) so that a brush can be
// switched to this shader without disturbing the meshes, particles, pickups and
// view-models that depend on Lighting.vs/LitFog.fs behaving exactly as before.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragWorldPos;

void main()
{
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    fragPosition = vec3(worldPos);
    fragWorldPos = vec3(worldPos);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
