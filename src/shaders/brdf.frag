#version 450

#include "brdf.glsl"

layout (binding = 0) uniform UniformBlock {
    mat4 viewMat;
    mat4 projMat;
    vec3 viewPos;
} ub;

layout (binding = 1) uniform sampler uSamplers[2]; // 0 - Nearest, 1 - Bilinear
#define NUM_MATERIALS 1
layout (binding = 2) uniform texture2D uAlbedoMap[NUM_MATERIALS];
layout (binding = 3) uniform texture2D uMetallicMap[NUM_MATERIALS];
layout (binding = 4) uniform texture2D uRoughnessMap[NUM_MATERIALS];
layout (binding = 5) uniform texture2D uAOMap[NUM_MATERIALS];
layout (binding = 6) uniform texture2D uNormalMap[NUM_MATERIALS];
layout (binding = 7) uniform texture2D uHeightMap[NUM_MATERIALS];

layout (location = 0) in vec2 vUV;
layout (location = 1) in vec3 vPosWorld;
layout (location = 2) in vec3 vNormalWorld;
layout (location = 3) in flat vec3 vAlbedo;
layout (location = 4) in flat vec3 vMRA; // Metallic, Roughness, AO
layout (location = 5) in flat int vMaterialIndex;

layout (location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(sampler2D(uAlbedoMap[vMaterialIndex], uSamplers[1]), vUV).rgb;
    float metallic = texture(sampler2D(uMetallicMap[vMaterialIndex], uSamplers[1]), vUV).r;
    float roughness = texture(sampler2D(uRoughnessMap[vMaterialIndex], uSamplers[1]), vUV).r;
    float ao = texture(sampler2D(uAOMap[vMaterialIndex], uSamplers[1]), vUV).r;

    vec3 lightDir = vec3(-1, -1, 0);

    vec3 L = -normalize(lightDir);
    vec3 V = normalize(ub.viewPos - vPosWorld);
    vec3 N = normalize(vNormalWorld);
    vec3 H = normalize(L + V);
    

    float D = distributionGGX(N, H, roughness);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlick(N, V, F0);
    float G = geometrySmith(N, V, L, roughness);

    vec3 color;

    vec3 lightColor = vec3(23.47, 21.31, 20.79) * 0.1;
    vec3 radiance = lightColor;

    vec3 specular = (D * F * G) / max(4 * max(dot(V, N), 0) * max(dot(L, N), 0), 0.001);
    vec3 kS = F;
    vec3 kD = vec3(1) - kS;
    kD *= (1 - metallic);

    vec3 Lo = vec3(0);
    Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0);
    
    vec3 ambient = vec3(0.03) * albedo * ao;
    color = ambient + Lo;

    outColor = vec4(color, 1);
}