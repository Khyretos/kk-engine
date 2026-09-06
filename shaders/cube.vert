#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec2 fragMetallicRoughness;

// Shrunk from { mat4 mvp; mat4 model; } (128 bytes -- exactly at the
// guaranteed-minimum Vulkan push constant limit, no room left at all)
// specifically to make real room for per-object PBR material
// properties. mvp is gone entirely, not just moved -- it was always
// redundant with proj*view*model, and proj*view is identical for
// every object drawn in a given frame, so recomputing it per-object in
// C++ and re-uploading a full 64-byte matrix for each one was pure
// waste. That shared proj*view now lives once in LightingUBO.viewProj
// below (see LightingBuffer.cpp's own comment), computed once per
// frame instead of once per object. What's left fits three real fields
// in 72 bytes, comfortably under the 128-byte limit: model (still
// needed per-object, still can't be shared), and the two real PBR
// inputs this whole change was for.
layout(push_constant) uniform PushConstants {
    mat4 model;
    float metallic;
    float roughness;
} pc;

// Mirrors kke::LightingBuffer's GPULight/LightingUBOData C++ structs
// byte-for-byte -- see engine/src/LightingBuffer.cpp for the CPU side
// this is fed from, and Application.h for Light/Lighting, the
// game-facing API that ultimately fills this in.
struct GPULight {
    vec4 directionOrPosition;
    vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj; // appended -- see this file's own PushConstants comment for why
} lighting;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = lighting.viewProj * worldPos;
    fragColor = inColor;
    fragPosWorld = worldPos.xyz;
    // mat3(model) extracts the rotation+scale part, ignoring translation --
    // correct for rotation and uniform scale (everything this engine
    // currently draws through this shader), not fully correct for
    // non-uniform scale, which would need the inverse-transpose instead.
    // A real, deliberate simplification for this first lighting slice,
    // not an oversight -- worth revisiting if a future mesh needs
    // non-uniform scaling with correct lighting.
    fragNormalWorld = normalize(mat3(pc.model) * inNormal);
    fragPosLightSpace = lighting.lightViewProj * worldPos;
    fragMetallicRoughness = vec2(pc.metallic, pc.roughness);
}
