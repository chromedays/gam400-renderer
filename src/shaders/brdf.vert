#version 450

#include "standard_sets.glsl"

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aInvModel;

layout (location = 0) out vec2 vUV;
layout (location = 1) out vec3 vPosWorld;
layout (location = 2) out vec3 vNormalWorld;
layout (location = 3) out vec3 vTangentWorld;

// (Tangent/Bitangent/Normal) matrix. Used to transform normal from model space to tangent space

void main() {
    vec4 posWorld = aModel * vec4(aPosition, 1.0);
    vPosWorld = posWorld.xyz;
    gl_Position = uProjMat * uViewMat * posWorld;
    vUV = aUV;

    // TODO(ilgwon): Pass normal matrix through instance data
    mat3 normalMat = transpose(mat3(aInvModel));
    vNormalWorld = normalize(normalMat * aNormal);
    vTangentWorld = normalize(normalMat * aTangent);
}