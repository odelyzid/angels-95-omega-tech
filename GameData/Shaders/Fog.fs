#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

uniform vec4 fogColor = vec4(0.78, 0.78, 0.82, 1.0);
uniform float fogDensity = 0.02;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    float depth = gl_FragCoord.z;
    float factor = clamp(1.0 - exp(-fogDensity * depth * 10.0), 0.0, 1.0);
    vec4 blended = mix(texel, fogColor, factor);
    finalColor = vec4(blended.rgb, texel.a);
}
