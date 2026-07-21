#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

uniform vec4 fogColor = vec4(0.78, 0.78, 0.82, 1.0);
uniform float fogIntensity = 0.3;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 blended = mix(texel, fogColor, fogIntensity);
    finalColor = vec4(blended.rgb, texel.a);
}
