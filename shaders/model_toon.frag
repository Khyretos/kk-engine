#version 450
#extension GL_GOOGLE_include_directive : require
// kke::ModelModule::setToon: anime ("cel") shading in the spirit of VRM's
// MToon material (docs/HUMANS.md "Anime style"). Same inputs as model.frag,
// so the same meshes, skinning and materials draw either way. Light is two
// tones, not a gradient: lit, or a cooler darker shade, split at a narrow
// soft edge; a rim of light on the silhouette; one small hard highlight on
// glossy parts; the sky's light flattened so forms read by their outline
// and shade shape, not by fine shading. No normal map: pores and grain
// would be noise in this style. The sun (light 0) and its shadow only.
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
layout(set = 2, binding = 1) uniform sampler2D normalTexture;
layout(set = 2, binding = 2) uniform sampler2D roughnessTexture;
layout(set = 2, binding = 3) uniform sampler2D metallicTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 material;
    vec4 tint;
} pc;

#include "pbr_common.glsl"

void main() {
    vec4 texel = texture(albedoTexture, fragUV);
    if (texel.a < 0.5) discard;
    float roughness = fragMetallicRoughness.y * texture(roughnessTexture, fragUV).r;
    vec3 base = srgbToLinear(fragColor) * texel.rgb;
    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    if (dot(N, V) < 0.0) N = -N; // thin strips seen from behind

    float intensity = lighting.lights[0].colorIntensity.a;
    vec3 sun = intensity > 0.0 ? lighting.lights[0].colorIntensity.rgb * intensity : vec3(0.0);
    vec3 L = sunDirection();
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(fragPosLightSpace) : 1.0;
    // The terminator a little past the side (shade shift), with a soft edge
    // a few degrees wide; cast shadows are banded the same way.
    // Cast shadows only half way: a full-strength nose or fringe shadow
    // cut the face in two.
    float lit = smoothstep(-0.04, 0.04, dot(N, L) + 0.08) * mix(0.5, 1.0, smoothstep(0.35, 0.65, shadow));

    // MToon's shade colour: darker and cooler than the lit one, keeping hue.
    vec3 shade = base * vec3(0.58, 0.55, 0.72);
    vec3 direct = sun / PI * 0.95;
    vec3 sky = mix(ambientIrradiance(N), ambientIrradiance(vec3(0.0, 1.0, 0.0)), 0.75);
    vec3 colour = mix(shade * (sky + direct * 0.25), base * (sky + direct), lit);

    // Rim light: brightest on the lit side's edge, a hint on the shaded one.
    float rim = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    colour += base * (direct * mix(0.15, 0.5, lit) + sky * 0.3) * rim;
    // A small hard highlight where the surface is glossy (lips, eyes).
    float gloss = clamp((0.5 - roughness) * 2.5, 0.0, 1.0); // only the really glossy: skin's T-zone isn't
    float spec = step(0.985, dot(N, normalize(L + V))) * lit * gloss;
    colour += sun / PI * 0.6 * spec;

    outColor = vec4(applyFog(colour, fragPosWorld), reflectionMark(1.0));
}
