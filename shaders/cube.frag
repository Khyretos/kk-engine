#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#extension GL_GOOGLE_include_directive : require

// Set 2: the material's own albedo texture -- see kke::Texture and
// kke::Application's own default 1x1 white texture (bound here for
// any object that doesn't have a real one of its own), multiplied
// into the vertex color: white * color = color.
layout(set = 2, binding = 0) uniform sampler2D albedoTexture;

#include "pbr_common.glsl"

void main() {
    // Albedo textures are VK_FORMAT_R8G8B8A8_SRGB, so sampling already
    // returns linear values; vertex colors need converting here.
    vec3 albedo = srgbToLinear(fragColor) * texture(albedoTexture, fragUV).rgb;
    outColor = vec4(shadeSurface(albedo, fragMetallicRoughness, fragNormalWorld, fragPosWorld, fragPosLightSpace), 1.0);
}
