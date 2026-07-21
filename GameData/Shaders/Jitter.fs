#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform float uTime = 0.0;
uniform float uIntensity = 1.0;

out vec4 finalColor;

// Simple pseudo-random hash
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
    vec2 uv = fragTexCoord;
    float t = uTime * 8.0;

    // Jitter offset per pixel (pseudo-random)
    vec2 jitter = vec2(
        hash(vec2(uv.x * 100.0, floor(uv.y * 200.0 + t))) - 0.5,
        hash(vec2(floor(uv.x * 200.0 + t), uv.y * 100.0)) - 0.5
    ) * uIntensity * 0.01;

    vec3 col = texture(texture0, uv + jitter).rgb;
    finalColor = vec4(col, 1.0);
}
