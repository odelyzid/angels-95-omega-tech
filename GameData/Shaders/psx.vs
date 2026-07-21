#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
uniform vec2 resolution = vec2(320.0, 240.0);

void main() {
    vec4 projected = mvp * vec4(vertexPosition, 1.0);
    // SNAP VERTICES TO INT GRID (rounding coordinates)
    vec2 grid = projected.xy / projected.w;
    grid = floor(grid * resolution) / resolution;
    gl_Position = vec4(grid * projected.w, projected.z, projected.w);
}