#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 6) in vec3 fragOverlayPos;
layout(location = 7) in vec3 fragOverlayNormal;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D albedoTexture;
// Set 3: optional object-space overlay (Synty POLYGON Prototype's
// *_Grid_* textures). Bound to a 1x1 white texture when off.
layout(set = 3, binding = 0) uniform sampler2D overlayTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 material;
    vec4 tint;
} pc;

#include "pbr_common.glsl"

// Triplanar projection: sample the overlay on the three planes of the
// object's rest shape and blend by the normal, so a grid lies flat on
// every face regardless of its UVs, measured in metres (1 cell = N m) like
// Synty's own Prototype grid shader. Object space rather than world space
// (which Synty uses for static level geometry) so the grid moves with the
// object: a thrown or breaking crate doesn't slide through its own grid.
vec3 triplanarOverlay(vec3 pos, vec3 n, float cellSize) {
    vec3 w = pow(abs(n), vec3(4.0));
    w /= (w.x + w.y + w.z);
    vec3 p = pos / cellSize;
    vec3 x = texture(overlayTexture, p.zy).rgb;
    vec3 y = texture(overlayTexture, p.xz).rgb;
    vec3 z = texture(overlayTexture, p.xy).rgb;
    return x * w.x + y * w.y + z * w.z;
}

void main() {
    vec4 texel = texture(albedoTexture, fragUV);
    // Cutout cards (leaves, ferns): transparent texels aren't there.
    if (texel.a < 0.5) discard;
    vec3 albedo = srgbToLinear(fragColor) * texel.rgb;
    float cellSize = pc.material.z;
    if (cellSize > 0.0) {
        vec3 overlay = triplanarOverlay(fragOverlayPos, normalize(fragOverlayNormal), cellSize);
        albedo *= mix(vec3(1.0), overlay, pc.material.w);
    }
    outColor = vec4(shadeSurface(albedo, fragMetallicRoughness, fragNormalWorld, fragPosWorld, fragPosLightSpace), 1.0);
}
