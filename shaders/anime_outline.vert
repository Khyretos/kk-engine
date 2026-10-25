#version 450
#extension GL_GOOGLE_include_directive : require
// kke::AnimeRenderer's outline: the inverted hull. The mesh is pushed out
// along its normals averaged across seams (the colour attribute), so the
// hull never splits at a hard edge, by the material's width (cm) times its
// width map, and drawn with front faces culled. Far away it widens a
// little so the line stays, close up it never fattens.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inSmoothNormal;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragNormalWorld;

#include "lighting_ubo.glsl"
#include "anime_common.glsl"

void main() {
    vec4 world = pc.model * vec4(inPosition, 1.0);
    vec3 n = normalize(mat3(pc.model) * (dot(inSmoothNormal, inSmoothNormal) > 0.0 ? inSmoothNormal : inNormal));
    float width = toon.outline.w * 0.01 * pc.forward.w * textureLod(outlineWidthTexture, inUV, 0.0).r;
    float kind = toon.rim2.w;
    if (kind == KIND_FACE) width *= pc.look.w;
    float dist = length(lighting.cameraPos.xyz - world.xyz);
    world.xyz += n * width * clamp(dist / 1.2, 0.5, 2.5);
    gl_Position = lighting.viewProj * world;
    fragPosWorld = world.xyz;
    fragUV = inUV;
    fragNormalWorld = n;
}
