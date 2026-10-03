#version 450
#extension GL_GOOGLE_include_directive : require
// Fabric: what makes silk look like silk and denim like denim. Drawn with
// cube.vert by kke::DynamicMeshRenderer::drawCloth (kke/Cloth.h Fabric).
//  - Two-sided: the back of a flag is lit as its own side.
//  - Weave: the threads, from the UVs (metres) x weaveScale: plain weave,
//    twill (denim's diagonal ribs), satin (long glossy floats), knit
//    (wool's V loops), none. Filtered by its own screen footprint: where a
//    thread is smaller than a pixel the pattern fades to its average, so
//    it never shimmers (no dithering, no temporal tricks).
//  - Sheen: the soft bright rim fibres give at grazing angles (Charlie
//    sheen, Estevez & Kulla 2017), strongest on wool and fleece.
//  - Thread gloss: satin and silk reflect along their threads (an
//    anisotropic Kajiya-Kay lobe along the weave direction).
//  - Fuzz: fibres standing off the surface soften the shading (wrapped
//    diffuse) and scatter light at the silhouette.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"

layout(push_constant) uniform ClothPush {
    mat4 model;
    float metallic;
    float roughness;
    float pad0, pad1;
    vec4 sheen;   // rgb = sheen colour (sRGB), a = sheen amount
    vec4 weave;   // x = pattern, y = threads per metre, z = fuzz, w = thread gloss
} pc;

// Thread pattern at uv (in threads): height of the thread on top (0..1)
// and whether the warp (1) or the weft (0) is on top.
vec2 weavePattern(vec2 t, int kind) {
    vec2 f = fract(t);
    vec2 cell = floor(t);
    // Rounded thread profiles across each thread.
    float across = 1.0 - pow(abs(f.y * 2.0 - 1.0), 2.0);
    float along = 1.0 - pow(abs(f.x * 2.0 - 1.0), 2.0);
    float warpTop;
    if (kind == 1) {        // twill 2/1: the over-under shifts one thread per row
        warpTop = mod(cell.x + cell.y, 3.0) < 2.0 ? 1.0 : 0.0;
    } else if (kind == 2) { // satin 4/1: long floats of warp
        warpTop = mod(cell.x * 2.0 + cell.y, 5.0) < 4.0 ? 1.0 : 0.0;
    } else if (kind == 3) { // knit: rows of V loops
        float v = abs(fract(t.x) * 2.0 - 1.0);
        float loop = 1.0 - pow(abs(fract(t.y + v * 0.5) * 2.0 - 1.0), 1.5);
        return vec2(loop, 1.0);
    } else {                // plain: over, under
        warpTop = mod(cell.x + cell.y, 2.0) < 1.0 ? 1.0 : 0.0;
    }
    float h = mix(along, across, warpTop);
    return vec2(h, warpTop);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

// Charlie sheen distribution (Estevez & Kulla), with the Neubelt visibility.
float charlieD(float roughness, float NdotH) {
    float invR = 1.0 / max(roughness, 0.07);
    float sin2h = max(1.0 - NdotH * NdotH, 0.0078125);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * PI);
}
float sheenVisibility(float NdotL, float NdotV) { return 1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV) + 1e-4); }

void main() {
    int kind = int(pc.weave.x + 0.5);
    float scale = pc.weave.y;
    float fuzz = pc.weave.z;
    float gloss = pc.weave.w;

    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    vec3 dpdx = dFdx(fragPosWorld), dpdy = dFdy(fragPosWorld);
    // Two-sided: the smooth normal turned to the side being looked at. The
    // triangle's own normal says which side that is (gl_FrontFacing would
    // trust the winding, which a cloth folded over itself can't keep).
    vec3 N = normalize(fragNormalWorld);
    vec3 Ng = cross(dpdx, dpdy);
    if (dot(Ng, V) < 0.0) Ng = -Ng;
    if (dot(N, Ng) < 0.0) N = -N;

    // Thread direction (warp = +u) in world space, from the UV derivatives.
    vec2 duvdx = dFdx(fragUV), duvdy = dFdy(fragUV);
    vec3 T = dpdx * duvdy.y - dpdy * duvdx.y;
    T = T - N * dot(N, T);
    T = length(T) > 1e-8 ? normalize(T) : normalize(cross(N, abs(N.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0)));

    // The weave, faded by how many threads fall in one pixel.
    vec2 t = fragUV * scale;
    vec2 footprint = fwidth(t);
    float fade = clamp(1.0 - (max(footprint.x, footprint.y) - 0.35) / 0.4, 0.0, 1.0);
    vec2 w = (kind == 4 || kind == 5 || scale <= 0.0) ? vec2(0.5, 0.5) : weavePattern(t, kind);
    float height = mix(0.5, w.x, fade);
    float warpTop = mix(0.5, w.y, fade);
    float speck = mix(0.5, valueNoise(t * 0.37), fade); // slub: thicker and thinner threads

    vec3 albedo = srgbToLinear(fragColor);
    // Grooves between threads are in shadow; denim's weft is white.
    albedo *= mix(0.62, 1.05, height) * mix(0.93, 1.07, speck);
    if (kind == 1) albedo = mix(albedo, albedo * 0.55 + vec3(0.18), (1.0 - warpTop) * 0.55 * fade);

    float rough = clamp(fragMetallicRoughness.y + (1.0 - height) * 0.08, 0.05, 1.0);
    vec3 sheenColor = srgbToLinear(pc.sheen.rgb) * pc.sheen.a;
    // Fibre direction on top: the warp or, where the weft is on top, across it.
    vec3 fibre = normalize(mix(cross(N, T), T, warpTop));

    vec3 sunL = lighting.lights[0].directionOrPosition.w > 0.5 ? normalize(lighting.lights[0].directionOrPosition.xyz - fragPosWorld)
                                                               : normalize(-lighting.lights[0].directionOrPosition.xyz);
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(shadowPosNormalOffset(fragPosWorld, N, sunL)) : 1.0;
    float NdotV = max(dot(N, V), 1e-3);
    vec3 F0 = vec3(0.04);
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue;
        vec3 L = lighting.lights[i].directionOrPosition.w > 0.5 ? normalize(lighting.lights[i].directionOrPosition.xyz - fragPosWorld)
                                                                : normalize(-lighting.lights[i].directionOrPosition.xyz);
        vec3 H = normalize(V + L);
        float NdotLraw = dot(N, L);
        // Fuzzy fabrics wrap light around (fibres are lit from the side too).
        float wrap = fuzz * 0.5;
        // Where a fold turns away from the light the cloth shades itself
        // smoothly; the shadow map, a texel at a time, would cut that soft
        // edge into hard blotches. So it fades in as the fold faces the light.
        float sunShadow = mix(1.0, shadow, smoothstep(-wrap, 0.3, NdotLraw));
        vec3 radiance = lighting.lights[i].colorIntensity.rgb * intensity * (i == 0 ? sunShadow : 1.0);
        float NdotL = max((NdotLraw + wrap) / (1.0 + wrap), 0.0);
        float NdotLs = max(NdotLraw, 0.0);
        float NdotH = max(dot(N, H), 0.0);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 spec = distributionGGX(N, H, rough) * geometrySmith(N, V, L, rough) * F / (4.0 * NdotV * max(NdotLs, 1e-3) + 1e-4);
        // Thread gloss: a Kajiya-Kay lobe along the fibres (silk, satin).
        float TdotH = dot(fibre, H);
        float threadSpec = pow(sqrt(max(1.0 - TdotH * TdotH, 0.0)), mix(8.0, 90.0, 1.0 - rough)) * gloss * 0.35 * mix(0.6, 1.0, height);
        vec3 sheenTerm = sheenColor * charlieD(mix(0.3, 0.6, fuzz), NdotH) * sheenVisibility(NdotLs, NdotV);
        vec3 kD = (vec3(1.0) - F);
        Lo += (kD * albedo / PI * NdotL + (spec + vec3(threadSpec)) * NdotLs + sheenTerm * NdotLs) * radiance;
    }
    vec3 ambient = ambientIrradiance(N) * albedo;
    // Fibres catch the sky at the silhouette.
    ambient += ambientIrradiance(N) * sheenColor * pow(1.0 - NdotV, 3.0) * (0.5 + fuzz);

    vec3 color = applyFog(ambient + Lo, fragPosWorld);
    outColor = vec4(displayColor(color), 1.0);
}
