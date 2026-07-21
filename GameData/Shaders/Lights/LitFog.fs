#version 330

// Input vertex attributes
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// Lighting
#define MAX_LIGHTS 4
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

// Fog uniforms
uniform float fogStart = 10.0;
uniform float fogEnd = 100.0;
uniform float fogDensity = 1.0;
uniform vec3 fogColor = vec3(0.7, 0.7, 0.8);
uniform float fogIntensity = 1.0;

void main()
{
    // Texel color fetching
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    // Lighting calculation
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 light = vec3(0.0);

            if (lights[i].type == LIGHT_DIRECTIONAL)
            {
                light = -normalize(lights[i].target - lights[i].position);
            }

            if (lights[i].type == LIGHT_POINT)
            {
                light = normalize(lights[i].position - fragPosition);
            }

            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb * NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) 
                specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo;
        }
    }

    // Base lit color
    vec3 litColor = (texelColor.rgb * ((colDiffuse.rgb + vec3(specular)) * vec3(lightDot, 1.0)));
    litColor += texelColor.rgb * (ambient.rgb / 10.0) * colDiffuse.rgb;

    // Gamma correction
    litColor = pow(litColor, vec3(1.0 / 2.2));

    // Distance-based fog calculation
    float fogDistance = length(viewPos - fragPosition);
    float fogFactor = clamp((fogDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor *= fogDensity * fogIntensity;

    // Light-influenced fog color (fog glows near lights)
    vec3 lightInfluence = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1 && lights[i].type == LIGHT_POINT)
        {
            float dist = length(lights[i].position - fragPosition);
            float influence = 1.0 / (1.0 + dist * 0.1);
            lightInfluence += lights[i].color.rgb * influence * 0.3;
        }
    }

    vec3 finalFogColor = fogColor + lightInfluence;

    // Apply fog to lit color
    vec3 finalLit = mix(litColor, finalFogColor, fogFactor);

    finalColor = vec4(finalLit, texelColor.a);
}
