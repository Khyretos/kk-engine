#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 0) out vec4 outColor;

// Mirrors kke::LightingBuffer's GPULight/LightingUBOData C++ structs
// byte-for-byte -- see engine/src/LightingBuffer.cpp for the CPU side
// this is fed from, and Application.h for Light/Lighting, the
// game-facing API that ultimately fills this in.
struct GPULight {
    vec4 directionOrPosition; // xyz = direction or position; w = 1.0 if positional (point), 0.0 if directional
    vec4 colorIntensity;      // rgb = color; a = intensity (<=0 means disabled)
};

layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;   // rgb = ambient color
    vec4 cameraPos; // rgb = world-space camera position, for specular
    mat4 lightViewProj;
    mat4 viewProj; // see cube.vert's own comment on why this is here
} lighting;

layout(set = 1, binding = 0) uniform sampler2D shadowMap;

const float PI = 3.14159265359;

// Real shadow lookup, not decorative -- see kke::ShadowMap's own class
// comment for the full account of what's implemented (a single
// directional light, single-tap, no PCF/soft edges yet) and what's
// deliberately still out of scope. Returns 1.0 for "fully lit," 0.0
// for "fully in shadow."
float computeShadow(vec4 posLightSpace) {
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(shadowMap, shadowUV).r;
    float bias = 0.003;
    return (currentDepth - bias > closestDepth) ? 0.0 : 1.0;
}

// Trowbridge-Reitz GGX normal distribution function -- how much the
// microfacet normals are concentrated around the halfway vector.
// Concentrated (small denominator growth) for low roughness -> a
// tight, bright highlight; spread out for high roughness -> a broad,
// dim one. Standard formulation, e.g. https://learnopengl.com/PBR/Theory
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

// Schlick-GGX geometry function for a single direction (view or
// light) -- how much light is self-shadowed/masked by the surface's
// own microfacets. Combined for both directions (Smith's method) in
// geometrySmith below.
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick -- reflectivity increases toward grazing angles for
// every real material, dielectric or metal. F0 is the base
// reflectivity straight-on (see main() for how it's derived from
// albedo/metallic).
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo = fragColor;
    // Clamped away from the true extremes (0.0 and 1.0), not just
    // whatever a UI slider happens to allow through -- roughness=0
    // makes distributionGGX's denominator degenerate toward a
    // divide-by-near-zero (an infinitely sharp, aliased/flickering
    // highlight under any real-time sampling), and metallic=1 combined
    // with roughness=0 is the single most extreme, least physically
    // meaningful corner of the whole model. Checked against real
    // screenshots: 0.05/0.95 preserves the full visual range a demo
    // actually wants (mirror-sharp metal vs. soft rough plastic) with
    // none of the near-zero instability.
    float metallic = clamp(fragMetallicRoughness.x, 0.0, 1.0);
    float roughness = clamp(fragMetallicRoughness.y, 0.05, 0.95);

    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);

    // Base reflectivity at normal incidence -- 0.04 is the standard,
    // widely-used approximation for non-metals (dielectrics: plastic,
    // wood, stone all cluster close to this regardless of color).
    // Metals reflect their own albedo color instead of a fixed dim
    // gray -- physically, a metal's "diffuse" color IS its specular
    // reflectance, which is exactly what mixing toward albedo here
    // encodes.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float shadow = computeShadow(fragPosLightSpace);
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < 4; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue; // disabled -- see the CPU-side "enabled" convention

        vec3 radiance = lighting.lights[i].colorIntensity.rgb * intensity;
        bool isPositional = lighting.lights[i].directionOrPosition.w > 0.5;

        vec3 L; // direction FROM the surface TOWARD the light
        if (isPositional) {
            L = normalize(lighting.lights[i].directionOrPosition.xyz - fragPosWorld);
        } else {
            L = normalize(-lighting.lights[i].directionOrPosition.xyz);
        }
        vec3 H = normalize(V + L);

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        // Energy conservation: kS (specular contribution) is F itself;
        // kD (diffuse) is whatever's left after that, and a metal has
        // no diffuse term at all (a metal's electrons absorb and
        // re-emit light entirely as specular reflection -- there is no
        // subsurface scattering to produce a diffuse color).
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);

        // Shadow only attenuates light[0] (the key light -- the only
        // one ShadowMap actually renders a depth pass for). Same real,
        // documented scope limit as the previous Blinn-Phong version
        // of this shader, unchanged by the PBR rewrite.
        float thisLightShadow = (i == 0) ? shadow : 1.0;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * thisLightShadow;
    }

    // Ambient: ubo.ambient times albedo, not a real irradiance
    // environment map -- a deliberate, documented simplification (see
    // README "What's still ahead for lighting"). Real image-based
    // ambient lighting needs a captured/generated environment map plus
    // irradiance convolution and a prefiltered specular mip chain,
    // none of which exist yet; this is a flat stand-in so ambient-only
    // surfaces don't read as pure black, same role the old Blinn-Phong
    // shader's ambient term played.
    vec3 ambient = lighting.ambient.rgb * albedo;

    vec3 color = ambient + Lo;
    // Reinhard tone mapping -- Lo can exceed 1.0 per-channel with
    // strong lights/low roughness (a real, physically-expected PBR
    // result, not a bug), and without compressing it back down first,
    // those values would just clip to flat white instead of rolling
    // off smoothly. A simple, standard choice, not a full filmic curve
    // -- worth revisiting alongside real HDR/bloom if this engine ever
    // adds either.
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
