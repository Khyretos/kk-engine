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
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = lighting.viewProj * worldPos;
    fragColor = pc.tint.rgb;
    // inColor = this vertex's rest position in model space (see
    // ModelModule's toVertex): the overlay is projected from it so it's
    // glued to the object. Scaled to metres by the object's own scale.
    vec3 scale = pc.tint.w > 0.0 ? vec3(pc.tint.w)
                                 : vec3(length(pc.model[0].xyz), length(pc.model[1].xyz), length(pc.model[2].xyz));
    fragOverlayPos = inColor * scale;
    fragOverlayNormal = inNormal; // model space for rigid meshes; deformed parts pass their current one (close enough for the blend weights)
    fragPosWorld = worldPos.xyz;
    fragNormalWorld = normalize(mat3(pc.model) * inNormal);
    fragPosLightSpace = lighting.lightViewProj * worldPos;
    fragMetallicRoughness = pc.material.xy;
    fragUV = inUV;
}
