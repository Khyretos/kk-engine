#version 450
// kke::ModelModule::setToon's outline: the inverted hull. model.vert's
// inputs and push constants; the mesh is pushed out along its normals by
// material.z metres (more for far away things, so the line stays visible
// but never fattens on a close-up), and the pipeline draws only back faces.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosWorld;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 material; // z: outline width in metres
    vec4 tint;
} pc;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; vec4 params; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[16]; // kke::Lighting::kMaxLights
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    vec3 n = normalize(mat3(pc.model) * inNormal);
    float dist = length(lighting.cameraPos.xyz - worldPos.xyz);
    worldPos.xyz += n * pc.material.z * clamp(dist / 1.5, 0.6, 3.0);
    gl_Position = lighting.viewProj * worldPos;
    fragColor = pc.tint.rgb;
    fragPosWorld = worldPos.xyz;
}
