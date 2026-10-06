#version 330

// Sky.vs - vertex stage for the skybox cube (Core.hpp's six SkyboxFace quads).
//
// Deliberately trivial: the sky has no lighting and no UV remapping of its own,
// the faces already carry correct per-face texcoords. The one thing it must supply
// is the WORLD POSITION, because the fragment stage derives the view ray from it
// in order to find the horizon.
//
// The cube is drawn at kSkyboxDist (700) from the camera with depth writes off, so
// world position is the only way to know which way the camera is looking;
// fragTexCoord alone cannot do it.
//
// Uniform names mvp / matModel are raylib's own (rlgl.h:
// RL_DEFAULT_SHADER_UNIFORM_NAME_MVP / _MODEL), which is why DrawModel on a
// material bound to this program feeds it without any manual plumbing here.
// Lighting.vs, Surface.vs and Wind.vs use the same two names for the same reason.

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec3 fragWorldPos;

void main()
{
    vec4 world = matModel * vec4(vertexPosition, 1.0);
    fragWorldPos = world.xyz;
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}