#version 450
// kke::ModelModule's INSTANCED vertex shader (model_instanced.vert): model.vert
// with the model matrix and tint per instance (binding 1) instead of in
// push constants, so N copies of a mesh part are one draw call. Original:
// kke::ModelModule's vertex shader: cube.vert plus per-instance tint and
// world-grid overlay parameters passed through push constants (96 bytes,
// under the 128-byte guaranteed minimum).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
// Per instance (binding 1): model matrix columns and tint.
layout(location = 8) in vec4 inModel0;
layout(location = 9) in vec4 inModel1;
layout(location = 10) in vec4 inModel2;
layout(location = 11) in vec4 inModel3;
layout(location = 12) in vec4 inTint;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec2 fragMetallicRoughness;
layout(location = 5) out vec2 fragUV;
layout(location = 6) out vec3 fragOverlayPos;    // metres, in the object's rest shape
layout(location = 7) out vec3 fragOverlayNormal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 material; // x metallic, y roughness, z overlay cell size in metres (0 = off), w overlay strength
    vec4 tint;     // rgb: material colour x instance tint (sRGB); w: overlay scale for pre-transformed (deformed) vertices, 0 = from the model matrix
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
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = lighting.viewProj * worldPos;
    fragColor = pc.tint.rgb * inTint.rgb; // material colour x instance tint
    // inColor = this vertex's rest position in model space (see
    // ModelModule's toVertex): the overlay is projected from it so it's
    // glued to the object. Scaled to metres by the object's own scale.
    vec3 scale = vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
    fragOverlayPos = inColor * scale;
    fragOverlayNormal = inNormal; // model space for rigid meshes; deformed parts pass their current one (close enough for the blend weights)
    fragPosWorld = worldPos.xyz;
    fragNormalWorld = normalize(mat3(model) * inNormal);
    fragPosLightSpace = lighting.lightViewProj * worldPos;
    fragMetallicRoughness = pc.material.xy;
    fragUV = inUV;
}
