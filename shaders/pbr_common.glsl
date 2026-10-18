// Shared lighting for lit mesh shaders. #include after declaring the
// fragment inputs; provides the set 0 lighting UBO, set 1 shadow map,
// srgbToLinear() and shadeSurface().
// Mirrors kke::LightingBuffer's GPULight/LightingUBOData C++ structs
// byte-for-byte -- see engine/src/LightingBuffer.cpp for the CPU side
// this is fed from, and Application.h for Light/Lighting, the
// game-facing API that ultimately fills this in.
#include "lighting_ubo.glsl"

// A depth-compare sampler (kke::ShadowMap): texture() returns how lit.
layout(set = 1, binding = 0) uniform sampler2DShadow shadowMap;
// The point map (kke::ShadowMap, a second one): six faces per shadow-casting point light.
layout(set = 1, binding = 1) uniform sampler2DShadow pointShadowMap;

// Set 2: the material's own albedo texture -- see kke::Texture and
// kke::Application's own default 1x1 white texture (bound here for
// any object that doesn't have a real one of its own, so this
// binding is always valid regardless of whether that specific
// object's material actually has a texture). Sampled and multiplied
// into albedo below, not used to replace it outright: a plain white
// texture leaves the vertex color fully in control (white * color =
// color, an exact no-op), while a real texture modulates it --
// meaning every object drawn through this shader keeps working
// exactly as before, whether or not it has a texture of its own.

const float PI = 3.14159265359;

// The nearest cascade whose map holds this point (its clip-space xy in
// the far cascade, lightViewProj), with room for the PCF taps; -1 when
// none does (outside every map: lit).
int shadowCascade(vec2 farClip) {
    float mapTexels = float(textureSize(shadowMap, 0).x);
    for (int c = 0; c < SHADOW_CASCADES; ++c) {
        vec2 p = farClip * lighting.cascadeScaleOffset[c].xy + lighting.cascadeScaleOffset[c].zw;
        float edge = 1.0 - 4.0 / max(mapTexels * lighting.shadowTiles[c].z, 1.0); // two texels: the 3x3 taps and their bilinear footprint
        if (abs(p.x) <= edge && abs(p.y) <= edge) return c;
    }
    return -1;
}

// Shadow lookup, 3x3 PCF on a depth-compare sampler: each tap is itself
// a bilinear 2x2 comparison done by the hardware, so the nine taps give a
// smooth edge ~4 texels wide instead of nine hard yes/no steps. Soft
// edges come from filtering, never from random or dithered taps
// (docs/RENDERING_PRINCIPLES.md). Returns 1.0 fully lit, 0.0 in shadow.
// Most of the acne bias is on the casters (ShadowMap::casterConfig's
// slope-scaled depth bias); this small constant only covers the PCF
// taps landing on a sloped receiver.
float computeShadow(vec4 posLightSpace) {
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    int c = shadowCascade(projCoords.xy);
    if (c < 0) return 1.0;
    vec4 so = lighting.cascadeScaleOffset[c];
    vec2 tile = lighting.shadowTiles[c].xy, tileSize = lighting.shadowTiles[c].zw;
    vec2 shadowUV = (projCoords.xy * so.xy + so.zw) * 0.5 + 0.5;
    // The same bias in texels in every cascade: so.x is how many times finer it is.
    float ref = projCoords.z - 0.00025 / so.x;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    // The taps stay inside this cascade's tile so none reads another cascade or view.
    vec2 tileMin = tile + texelSize * 1.5;
    vec2 tileMax = tile + tileSize - texelSize * 1.5;
    shadowUV = tile + shadowUV * tileSize;

    float litSum = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            litSum += texture(shadowMap, vec3(clamp(shadowUV + vec2(dx, dy) * texelSize, tileMin, tileMax), ref));
        }
    }
    return litSum / 9.0;
}

// Point-light shadow: the cube face that sees the point (kke::ShadowMap::pointFace),
// pushed out along the normal by about 1.5 texels, then 3x3 PCF inside that
// face's tile. Returns 1.0 fully lit, 0.0 in shadow.
float computePointShadow(int light, int slot, vec3 worldPos, vec3 N) {
    vec3 d = worldPos - lighting.lights[light].directionOrPosition.xyz;
    vec3 a = abs(d);
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = (d.x > 0.0 ? 0 : 1);
    } else if (a.y >= a.z) {
        face = (d.y > 0.0 ? 2 : 3);
    } else {
        face = (d.z > 0.0 ? 4 : 5);
    }
    int f = slot * 6 + face; // slot: kke::Lighting::pointShadowSlots
    vec4 tile = lighting.pointShadowTiles[f];
    vec2 mapTexels = vec2(textureSize(pointShadowMap, 0));
    float texel = 2.0 * max(max(a.x, a.y), a.z) / (mapTexels.x * tile.z);
    vec4 c = lighting.pointShadowViewProj[f] * vec4(worldPos + N * (1.5 * texel), 1.0);
    vec3 p = c.xyz / c.w;
    if (p.z >= 1.0) return 1.0;
    vec2 uv = tile.xy + (p.xy * 0.5 + 0.5) * tile.zw;
    vec2 texelSize = 1.0 / mapTexels;
    vec2 tileMin = tile.xy + 0.5 * texelSize;
    vec2 tileMax = tile.xy + tile.zw - 0.5 * texelSize;
    float litSum = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            litSum += texture(pointShadowMap, vec3(clamp(uv + vec2(dx, dy) * texelSize, tileMin, tileMax), p.z));
        }
    }
    return litSum / 9.0;
}

// The light-space position for a shadow lookup, pushed out along the
// surface normal by about a shadow-map texel (more the more the surface
// turns away from the light). Thin surfaces lying on or right over other
// casters (cloth on a ball or a mattress, a sheet's own folds) otherwise
// sample the caster they touch and break up into blocky lit and dark
// patches: the depth bias alone can't separate surfaces closer than a
// texel. N faces the viewer; L points towards the light.
vec4 shadowPosNormalOffset(vec3 worldPos, vec3 N, vec3 L) {
    // An orthographic light: the first row of lightViewProj is the light's
    // right axis divided by the half width the far cascade covers; a near
    // cascade covers that divided by its scale.
    mat4 m = lighting.lightViewProj;
    vec4 farClip = m * vec4(worldPos, 1.0);
    int c = max(shadowCascade(farClip.xy / farClip.w), 0);
    float halfWidth = 1.0 / max(length(vec3(m[0][0], m[1][0], m[2][0])) * lighting.cascadeScaleOffset[c].x, 1e-6);
    float texel = 2.0 * halfWidth / (float(textureSize(shadowMap, 0).x) * max(lighting.shadowTiles[c].z, 1e-6));
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    return m * vec4(worldPos + N * texel * (1.0 + 2.0 * (1.0 - NdotL)), 1.0);
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


// Colors are authored in sRGB (color pickers, hex codes, palette values)
// but the swapchain is VK_FORMAT_B8G8R8A8_SRGB, which gamma-encodes
// whatever the shader writes. Writing sRGB values straight out encoded
// them twice: everything looked washed out. Convert to linear first.
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

// Specular anti-aliasing (Kaplanyan & Tokuyoshi; the same constants as
// Filament): where the normal changes a lot within one pixel (curved or
// distant surfaces), a sharp highlight sparkles from frame to frame. The
// normal's screen-space derivatives measure that spread; it is added to
// the GGX roughness so the pixel shows the averaged highlight it covers.
// Spatial and per-frame: no temporal accumulation, no dithering, no blur
// of the image (docs/RENDERING_PRINCIPLES.md). Returns the extra alpha^2.
// Derivatives: call it before any discard.
#ifndef KKE_COMPUTE // derivatives: fragment shaders only (compute includes this for the shadow lookups)
float specularAAKernel(vec3 normalWorld) {
    vec3 n = normalize(normalWorld);
    vec3 du = dFdx(n);
    vec3 dv = dFdy(n);
    float variance = 0.15 * (dot(du, du) + dot(dv, dv));
    return min(2.0 * variance, 0.2);
}
#endif

#ifndef KKE_COMPUTE
vec3 shadeSurfaceAA(vec3 albedo, vec2 metallicRoughness, vec3 normalWorld, vec3 posWorld, vec4 posLightSpace, float specAAKernel);
#endif

// What a lit opaque surface writes as its alpha (ENG-F02): 2 + smoothness.
// Translucent shaders, particles and the sky write 1 or less, so the
// reflections pass (shaders/reflect.glsl) knows a reflecting surface by
// alpha >= 2 and how sharp it is from the rest, with no extra target.
float reflectionMark(float roughness) { return 2.0 + clamp(1.0 - roughness, 0.0, 1.0); }

// Full lighting for one surface point: PBR (GGX) for up to 4 lights,
// shadow on light 0, ambient from the sky, fog; returns scene light. Shared by every
// lit mesh shader (cube.frag, model.frag) so they can't drift apart.
// Shaders that discard call shadeSurfaceAA with a kernel taken first.
#ifndef KKE_COMPUTE
vec3 shadeSurface(vec3 albedo, vec2 metallicRoughness, vec3 normalWorld, vec3 posWorld, vec4 posLightSpace) {
    return shadeSurfaceAA(albedo, metallicRoughness, normalWorld, posWorld, posLightSpace, specularAAKernel(normalWorld));
}
#endif

#ifndef KKE_COMPUTE
// Decals (ENG-F12, kke/Decals.h): the ones this pixel's cell lists, mixed into
// its colour and roughness before it is lit, oldest first so the newest is on
// top. kke::decalWeight decides how much shows (inside the box, facing it);
// the texture is read with this pixel's own gradients, so it filters right
// even though the loop is not uniform across the quad.
void applyDecals(inout vec3 albedo, inout float roughness, vec3 pos, vec3 N) {
    uint count = uint(lighting.decalParams.x + 0.5);
    vec3 dpdx = dFdx(pos), dpdy = dFdy(pos);
    if (count == 0u) return;
    float viewDepth = (lighting.viewProj * vec4(pos, 1.0)).w;
    if (viewDepth < lighting.decalParams.y || viewDepth >= lighting.decalParams.z) return;
    uvec3 g = uvec3(lighting.clusterGrid.xyz);
    uint cz = min(uint(lighting.clusterGrid.z * log(viewDepth / lighting.decalParams.y) / log(lighting.decalParams.z / lighting.decalParams.y)), g.z - 1u);
    uvec2 cxy = min(uvec2(gl_FragCoord.xy / lighting.decalScreen.xy * vec2(g.xy)), g.xy - 1u);
    uint cell = (cz * g.y + cxy.y) * g.x + cxy.x;
    uint first = cell * uint(lighting.clusterGrid.w), n = decalCounts[cell];
    for (uint k = 0u; k < n; ++k) {
        DecalData d = decals[decalIndices[first + k]];
        vec3 local = (d.worldToDecal * vec4(pos, 1.0)).xyz;
        if (any(greaterThan(abs(local), vec3(1.0)))) continue;
        // kke::decalWeight
        float facing = smoothstep(0.2, 0.5, dot(N, d.normal.xyz));
        float w = facing * (1.0 - clamp((abs(local.z) - 0.6) / 0.4, 0.0, 1.0));
        if (w <= 0.0) continue;
        mat3 m = mat3(d.worldToDecal);
        vec4 t = textureGrad(decalTextures, vec3(local.xy * 0.5 + 0.5, d.params.x), (m * dpdx).xy * 0.5, (m * dpdy).xy * 0.5);
        float a = t.a * d.colorOpacity.a * w;
        albedo = mix(albedo, t.rgb * d.colorOpacity.rgb, a);
        if (d.params.y >= 0.0) roughness = mix(roughness, d.params.y, a);
    }
}

vec3 shadeSurfaceAA(vec3 albedo, vec2 metallicRoughness, vec3 normalWorld, vec3 posWorld, vec4 posLightSpace, float specAAKernel) {
    vec2 fragMetallicRoughness = metallicRoughness;
    applyDecals(albedo, fragMetallicRoughness.y, posWorld, normalize(normalWorld));
    vec3 fragNormalWorld = normalWorld;
    vec3 fragPosWorld = posWorld;
    vec4 fragPosLightSpace = posLightSpace;
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
    // Perceptual roughness r, GGX alpha = r^2, kernel adds to alpha^2 = r^4.
    float r = clamp(fragMetallicRoughness.y, 0.0, 1.0);
    float roughness = clamp(pow(r * r * r * r + specAAKernel, 0.25), 0.05, 0.95);

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

    float shadow = lighting.ambient.a > 0.5 ? computeShadow(fragPosLightSpace) : 1.0;
    // Ray-traced sun shadows (ENG-F07): last frame's ray where there is one.
    float rayShadow = lighting.ambient.a > 0.5 ? shadowRayAt(fragPosWorld) : -1.0;
    if (rayShadow >= 0.0) shadow = rayShadow;
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue; // disabled -- see the CPU-side "enabled" convention
        float falloff = lightFalloff(i, fragPosWorld); // a point light's range (kke::Light::range)
        if (falloff <= 0.0) continue;                   // out of its reach: costs nothing more

        vec3 radiance = lighting.lights[i].colorIntensity.rgb * intensity * falloff;
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

        // The directional key light (light 0) uses the cascaded map; a point
        // light with a point-shadow slot (params.y >= 0) uses its six faces
        // in that slot's tiles of the point map.
        int slot = int(lighting.lights[i].params.y + 0.5);
        float thisLightShadow = isPositional ? (lighting.lights[i].params.y > -0.5 ? computePointShadow(i, slot, fragPosWorld, N) : 1.0) : (i == 0 ? shadow : 1.0);

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * thisLightShadow;
    }

    // Many lights (ENG-F08): only the ones this pixel's cell lists (kke::clusterIndex).
    uint manyCount = uint(lighting.clusterParams.x + 0.5);
    if (manyCount > 0u) {
        float viewDepth = (lighting.viewProj * vec4(fragPosWorld, 1.0)).w;
        uint first = 0u, count = 0u;
        bool all = lighting.clusterParams.w > 0.5; // KKE_CLUSTERS=0: every light, for comparison
        if (all) {
            count = manyCount;
        } else if (viewDepth >= lighting.clusterParams.y && viewDepth < lighting.clusterParams.z) {
            uvec3 g = uvec3(lighting.clusterGrid.xyz);
            uint cz = min(uint(lighting.clusterGrid.z * log(viewDepth / lighting.clusterParams.y) / log(lighting.clusterParams.z / lighting.clusterParams.y)), g.z - 1u);
            uvec2 cxy = min(uvec2(gl_FragCoord.xy / lighting.clusterScreen.xy * vec2(g.xy)), g.xy - 1u);
            uint cell = (cz * g.y + cxy.y) * g.x + cxy.x;
            first = cell * uint(lighting.clusterGrid.w);
            count = clusterCounts[cell];
        }
        for (uint k = 0u; k < count; ++k) {
            ManyLight m = manyLights[all ? k : clusterIndices[first + k]];
            vec3 toLight = m.positionRange.xyz - fragPosWorld;
            float t = length(toLight) / m.positionRange.w;
            float w = clamp(1.0 - t * t * t * t, 0.0, 1.0); // kke::Light::falloffAt
            if (w <= 0.0) continue;
            vec3 L = toLight / max(length(toLight), 1e-4);
            vec3 H = normalize(V + L);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3 specular = distributionGGX(N, H, roughness) * geometrySmith(N, V, L, roughness) * F /
                            (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            Lo += (kD * albedo / PI + specular) * m.colorIntensity.rgb * m.colorIntensity.a * w * w * max(dot(N, L), 0.0);
        }
    }

    // Ambient: light from the sky (kke::Sky::lightsScene). Diffuse from its
    // spherical harmonics (kke::SkySH), so a surface facing up takes the sky's
    // blue and one facing down the ground's bounce; with no sky it is the flat
    // Lighting::ambientColor exactly. Specular from the prefiltered sky
    // (kke::EnvMap) along the reflection, weighted by the environment BRDF:
    // metals and glancing angles mirror the sky, and what is reflected is
    // not also scattered (the split sum, Karis 2013).
    // With screen-space GI (ENG-F01) the sky is shaded by what hides it and
    // the light bounced off what is near is added.
    float NdotV = max(dot(N, V), 1e-4);
    vec3 envSpecular = envBRDFApprox(F0, roughness, NdotV);
    vec4 gi = screenGiAt(fragPosWorld);
    // Reflections (ENG-F02): on smooth surfaces what the ray met, over the
    // sky, sharper surfaces trusting it more.
    vec3 reflected = skyReflection(reflect(-V, N), roughness) * gi.a;
    if (roughness < lighting.refl.w) {
        vec4 scene = screenReflectionAt(fragPosWorld);
        reflected = mix(reflected, scene.rgb, scene.a * (1.0 - roughness / lighting.refl.w));
        if (lighting.gi.w > 1.5) return scene.rgb * scene.a; // KKE_GI_VIEW=2: smooth surfaces show what they reflect, alone
    }
    vec3 ambient = (ambientIrradiance(N) * gi.a + gi.rgb) * albedo * (vec3(1.0) - envSpecular) * (1.0 - metallic)
                 + reflected * envSpecular;
    if (lighting.gi.w > 0.5) return vec3(gi.a); // KKE_GI_VIEW=1: the visibility alone (grey: no light to bounce)

    vec3 color = ambient + Lo;
    return applyFog(color, fragPosWorld);
}
#endif
