#version 450
// kke::ModelModule's vertex shader: cube.vert plus per-instance tint and
// world-grid overlay parameters passed through push constants (96 bytes,
// under the 128-byte guaranteed minimum).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec2 fragMetallicRoughness;
layout(location = 5) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 material; // x metallic, y roughness, z overlay cell size in metres (0 = off), w overlay strength
    vec4 tint;     // rgb multiplies the vertex color
} pc;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = lighting.viewProj * worldPos;
    fragColor = inColor * pc.tint.rgb;
    fragPosWorld = worldPos.xyz;
    fragNormalWorld = normalize(mat3(pc.model) * inNormal);
    fragPosLightSpace = lighting.lightViewProj * worldPos;
    fragMetallicRoughness = pc.material.xy;
    fragUV = inUV;
}
