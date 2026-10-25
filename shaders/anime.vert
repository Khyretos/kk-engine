#version 450
#extension GL_GOOGLE_include_directive : require
// kke::AnimeRenderer: the avatar's posed vertices (model space, kke::Vertex;
// colour holds the normal averaged across seams, for the outline).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inSmoothNormal;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragPosLightSpace;

#include "lighting_ubo.glsl"
#include "anime_common.glsl"

void main() {
    vec4 world = pc.model * vec4(inPosition, 1.0);
    gl_Position = lighting.viewProj * world;
    fragPosWorld = world.xyz;
    fragNormalWorld = normalize(mat3(pc.model) * inNormal);
    fragUV = inUV;
    fragPosLightSpace = lighting.lightViewProj * world;
}
