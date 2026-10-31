// ENG-F14: FSR 1-style spatial upscaling inside the tone-map pass
// (shaders/tonemap_post.frag), used when the scene is drawn at a render
// scale below 1. Written from AMD's published FSR 1 algorithm (EASU:
// edge-adaptive 12-tap Lanczos-2 with deringing; RCAS: robust
// contrast-adaptive sharpening), not a copy of its source. Both run on
// the HDR scene light through a reversible tone map (c / (1 + luma)), so
// bright pixels don't ring; the result goes on through the normal curve.
// The sharpening uses the source texels around the pixel (one pass, no
// extra full-resolution image), where AMD's RCAS reads the upscaled
// image: the same lobe, a little softer. kke::fsrEasuWeight is the CPU
// mirror of the tap weight (tests). #include after the uScene sampler.

float fsrLuma(vec3 c) { return 0.5 * c.r + c.g + 0.5 * c.b; } // FSR's cheap luma (2x)

vec3 fsrIn(ivec2 p, ivec2 size) {
    vec3 c = texelFetch(uScene, clamp(p, ivec2(0), size - 1), 0).rgb;
    return c / (1.0 + max(max(c.r, c.g), c.b)); // reversible: undone in fsrOut
}
vec3 fsrOut(vec3 c) { return c / max(1.0 - max(max(c.r, c.g), c.b), 1.0 / 4096.0); }

// One quadrant's edge direction and length (EASU's "set").
void fsrSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    // x: B (left), C (centre), D (right)
    float dc = lD - lC, cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = lenX > 0.0 ? 1.0 / lenX : 0.0;
    float dirX = lD - lB;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    // y: A (up), C, E (down)
    float ec = lE - lC, ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = lenY > 0.0 ? 1.0 / lenY : 0.0;
    float dirY = lE - lA;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    dir += vec2(dirX, dirY) * w;
    len += (lenX + lenY) * w;
}

// The weight of a tap `off` texels from the sample point, along the edge (EASU's "tap").
void fsrTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len2, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, -off.x * dir.y + off.y * dir.x) * len2;
    float d2 = min(dot(v, v), clp);
    // Lanczos-2 approximated: (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (lob * x^2 - 1)^2
    float wB = 0.4 * d2 - 1.0, wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

// The scene light at `uv` (0..1 of the output), upscaled; `sharpness` 0 = most, 2 = none (stops, as FSR's RCAS).
vec3 fsrUpscale(vec2 uv, float sharpness) {
    ivec2 size = textureSize(uScene, 0);
    vec2 pp = uv * vec2(size) - 0.5;
    vec2 fp = floor(pp);
    pp -= fp;
    ivec2 i0 = ivec2(fp);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec3 b = fsrIn(i0 + ivec2(0, -1), size), c = fsrIn(i0 + ivec2(1, -1), size);
    vec3 e = fsrIn(i0 + ivec2(-1, 0), size), f = fsrIn(i0, size), g = fsrIn(i0 + ivec2(1, 0), size), h = fsrIn(i0 + ivec2(2, 0), size);
    vec3 i = fsrIn(i0 + ivec2(-1, 1), size), j = fsrIn(i0 + ivec2(0, 1), size), k = fsrIn(i0 + ivec2(1, 1), size), l = fsrIn(i0 + ivec2(2, 1), size);
    vec3 n = fsrIn(i0 + ivec2(0, 2), size), o = fsrIn(i0 + ivec2(1, 2), size);
    float bL = fsrLuma(b), cL = fsrLuma(c), eL = fsrLuma(e), fL = fsrLuma(f), gL = fsrLuma(g), hL = fsrLuma(h);
    float iL = fsrLuma(i), jL = fsrLuma(j), kL = fsrLuma(k), lL = fsrLuma(l), nL = fsrLuma(n), oL = fsrLuma(o);

    vec2 dir = vec2(0.0);
    float len = 0.0;
    fsrSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    fsrSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    fsrSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    fsrSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    float dirR = dot(dir, dir);
    dir = dirR < 1.0 / 32768.0 ? vec2(1.0, 0.0) : dir * inversesqrt(dirR);
    len = 0.5 * len;
    len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 - 0.29 * len;
    float clp = 1.0 / lob;

    vec3 aC = vec3(0.0);
    float aW = 0.0;
    fsrTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    fsrTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    fsrTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    fsrTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    fsrTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    fsrTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    fsrTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    fsrTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    fsrTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    fsrTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    fsrTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    fsrTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    vec3 mn4 = min(min(f, g), min(j, k)), mx4 = max(max(f, g), max(j, k));
    vec3 up = clamp(aC / max(aW, 1e-6), mn4, mx4); // deringing

    // RCAS-style sharpening: the lobe from the four nearest source texels' range.
    if (sharpness < 2.0) {
        vec3 hitMin = mn4 / max(4.0 * mx4, vec3(1e-5));
        vec3 hitMax = (vec3(1.0) - mx4) / min(4.0 * mn4 - 4.0, vec3(-1e-5));
        vec3 lobeRGB = max(-hitMin, hitMax);
        float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * exp2(-sharpness);
        vec3 cross4 = (f + g + j + k) * 0.25;
        up = (lobe * 4.0 * cross4 + up) / (4.0 * lobe + 1.0);
        up = clamp(up, vec3(0.0), vec3(0.999));
    }
    return fsrOut(up);
}
