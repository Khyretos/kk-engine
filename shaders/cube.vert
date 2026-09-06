#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec4 fragPosLightSpace;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
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
    mat4 lightViewProj; // new field, appended -- see LightingBuffer.cpp's own comment on why appending (not inserting) matters for the CPU-side struct's matching byte layout
} lighting;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
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
}
