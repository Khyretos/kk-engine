#ifndef KKE_TONEMAP_GLSL
#define KKE_TONEMAP_GLSL
// Tone mapping: scene light (unbounded, linear) -> displayable 0..1
// (linear; the sRGB swapchain encodes it). Shared by every lit shader so
// they can't drift apart. The operator and exposure come from the lighting
// UBO's toneParams (kke::Lighting::toneMapper / exposure):
//   0 = AgX (default): hue-preserving, highlights desaturate towards white
//       the way film and eyes do, instead of skewing (bright red going
//       orange, blue going purple).
//   1 = ACES (Narkowicz's fitted curve): more contrast, the "movie" look;
//       shifts some saturated hues.
//   2 = Reinhard, per channel: the engine's original curve.
// Per pixel, spatial only: no dithering, nothing temporal
// (docs/RENDERING_PRINCIPLES.md §7).

// AgX, as fitted by Benjamin Wrensch to Troy Sobotka's reference:
// into a slightly desaturated working space, log2 encode over a fixed EV
// range, a polynomial sigmoid, back out, then to linear.
// Matrices, EV range and sigmoid coefficients from Wrensch's "Minimal AgX
// Implementation", MIT License, Copyright (c) 2024 Missing Deadlines
// (Benjamin Wrensch); full notice in LICENSES/AgX-minimal-MIT.txt
// (docs/DEPENDENCIES.md).
vec3 agxSigmoid(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// The look: ASC CDL (slope, offset, power) then saturation around Rec.709
// luminance, on display-encoded values -- where Wrensch's minimal AgX
// applies its "punchy" and "golden" looks (kke::ColorGrade).
vec3 applyLook(vec3 v, vec4 slopeSat, vec4 offset, vec4 power) {
    v = pow(max(v * slopeSat.rgb + offset.rgb, vec3(0.0)), power.rgb);
    float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
    return luma + slopeSat.a * (v - luma);
}

vec3 toneMapAgXLook(vec3 c, vec4 slopeSat, vec4 offset, vec4 power, bool look) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 v = inset * c;
    v = clamp(log2(max(v, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = agxSigmoid(v);
    if (look) v = applyLook(v, slopeSat, offset, power);
    v = outset * v;
    return pow(max(v, vec3(0.0)), vec3(2.2)); // the sigmoid's output is display-encoded
}

vec3 toneMapAgX(vec3 c) {
    return toneMapAgXLook(c, vec4(1.0), vec4(0.0), vec4(1.0), false);
}

// ACES: Krzysztof Narkowicz's five-coefficient fit ("ACES Filmic Tone
// Mapping Curve", 2016), credited in docs/DEPENDENCIES.md.
vec3 toneMapAces(vec3 c) {
    c *= 0.6; // the fit expects this pre-scale to match the reference's exposure
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}

vec3 toneMap(vec3 c, vec4 toneParams) {
    c *= toneParams.y; // exposure
    int op = int(toneParams.x + 0.5);
    if (op == 1) return toneMapAces(c);
    if (op == 2) return c / (c + vec3(1.0));
    return toneMapAgX(c);
}

// toneMap() plus the scene's look (lookOffset.a = 1 when one is set).
// AgX takes it between its sigmoid and its outset, as Wrensch's reference
// does; ACES and Reinhard take it on their output, display-encoded.
vec3 toneMapLook(vec3 c, vec4 toneParams, vec4 slopeSat, vec4 offset, vec4 power) {
    bool look = offset.a > 0.5;
    int op = int(toneParams.x + 0.5);
    if (op == 0) return toneMapAgXLook(c * toneParams.y, slopeSat, offset, power, look);
    vec3 v = toneMap(c, toneParams);
    if (!look) return v;
    v = applyLook(pow(v, vec3(1.0 / 2.2)), slopeSat, offset, power);
    return pow(clamp(v, 0.0, 1.0), vec3(2.2));
}

#endif
