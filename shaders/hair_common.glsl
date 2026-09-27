// Drawn hairs built on the GPU from the simulated guide strands
// (kke::HairRenderer, kke/HairRenderer.h): the CPU uploads only the guides'
// points each frame; every drawn hair is expanded here from its guide, the
// neighbour it leans towards and its offset. #define HAIR_SET before
// including (the descriptor set holding the two buffers).
#ifndef KKE_HAIR_COMMON_GLSL
#define KKE_HAIR_COMMON_GLSL

// Mirrors HairRenderer's HairGpu (std430, 64 bytes).
struct HairGpu {
    uvec4 idx;    // x = guide, y = the neighbour it leans to, z = frizz phase (float bits)
    vec4 offset;  // xyz = offset from the guide (head space), w = how much of the neighbour
    vec4 color;   // rgb = tint, a = its length (fraction of the guide's)
    vec4 coil;    // x = turns root to tip, y = phase, z = radius scale; plaits: z, w = where in its strand (HairStrands::Hair::rope)
};

layout(std430, set = HAIR_SET, binding = 0) readonly buffer Hairs { HairGpu hairs[]; };

// Mirrors HairRenderer's FrameHeader, then the guide points.
layout(std430, set = HAIR_SET, binding = 1) readonly buffer Frame {
    mat4 head;      // the head's rotation now (offsets turn with it)
    vec4 counts;    // x = points drawn per hair, y = vertices per guide strand, z = drawn width (m), w = a pixel's size 1 m away
    vec4 shape;     // x = clump, y = frizz (m), z = width in the shadow map (m), w = plait strands (0 = none)
    vec4 rootColor; // sRGB
    vec4 tipColor;  // sRGB, a = shine
    vec4 look;      // x = highlight shift, y = roughness, z = every z-th hair is drawn (level of detail), w = width scale for it
    vec4 coil;      // x = coil radius (plaits: half the braid's width; m, 0 = none), y = zig-zag, z = pulled straight at (x rest length), w = guide points
    vec4 headSphere; // xyz = the head's centre now, w = radius (0 = none): hairs stay out of it
    vec4 guides[];  // xyz, guide after guide, root first; then as many frames (HairStrands::frames)
} frame;

// Every drawn hair's points, built once a frame by hair_points.comp:
// xyz = the centre, w = its direction (packTangent's bits).
#ifdef HAIR_POINTS_WRITE
layout(std430, set = HAIR_SET, binding = 2) writeonly buffer Points { vec4 points[]; };
#else
layout(std430, set = HAIR_SET, binding = 2) readonly buffer Points { vec4 points[]; };
#endif

// A unit direction in 32 bits (octahedral, 16 bits a component).
uint packTangent(vec3 t) {
    t /= abs(t.x) + abs(t.y) + abs(t.z);
    vec2 o = t.z >= 0.0 ? t.xy : (1.0 - abs(t.yx)) * vec2(t.x >= 0.0 ? 1.0 : -1.0, t.y >= 0.0 ? 1.0 : -1.0);
    return packSnorm2x16(o);
}
vec3 unpackTangent(uint bits) {
    vec2 o = unpackSnorm2x16(bits);
    vec3 t = vec3(o, 1.0 - abs(o.x) - abs(o.y));
    float f = max(-t.z, 0.0);
    t.xy += vec2(t.x >= 0.0 ? -f : f, t.y >= 0.0 ? -f : f);
    return normalize(t);
}

#ifndef HAIR_POINTS_WRITE
// A drawn hair's point as hair_points.comp built it: the centre, the
// direction and s (0 root .. 1 tip).
void builtPoint(int hair, int point, out vec3 p, out vec3 t, out float s) {
    int pts = int(frame.counts.x + 0.5);
    int drawn = hair / int(frame.look.z + 0.5);
    vec4 v = points[drawn * pts + point];
    p = v.xyz;
    t = unpackTangent(floatBitsToUint(v.w));
    s = float(point) / float(pts - 1);
}
#endif

// Six vertices (two triangles) per segment of every hair.
void hairCorner(int vertexIndex, out int hair, out int point, out float side) {
    int pts = int(frame.counts.x + 0.5);
    int perHair = (pts - 1) * 6;
    hair = vertexIndex / perHair;
    int r = vertexIndex - hair * perHair;
    hair *= int(frame.look.z + 0.5);
    int seg = r / 6;
    int c = r - seg * 6;
    // corners 0 1 2 / 1 3 2 of the quad (point, point + 1) x (left, right)
    int corner = c < 3 ? c : (c == 3 ? 1 : (c == 4 ? 3 : 2));
    point = seg + (corner >> 1);
    side = (corner & 1) == 0 ? -1.0 : 1.0;
}

// A guide at f segments from its follicle (0 .. segments), on a
// Catmull-Rom curve through its points: curls stay round, not zig-zag.
vec3 guidePoint(uint g, float f) {
    int strand = int(frame.counts.y + 0.5);
    int segs = strand - 2;
    int k = clamp(int(f), 0, segs - 1);
    float t = clamp(f - float(k), 0.0, 1.0);
    int base = int(g) * strand + 1; // the follicle
    vec3 p0 = frame.guides[base + max(k - 1, 0)].xyz, p1 = frame.guides[base + k].xyz;
    vec3 p2 = frame.guides[base + k + 1].xyz, p3 = frame.guides[base + min(k + 2, segs)].xyz;
    float t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 + (3.0 * p1 - p0 - 3.0 * p2 + p3) * t3);
}

// Point i (0 = follicle) of a hair, and s (0 root .. 1 tip).
vec3 hairPoint(HairGpu h, int i, out float s) {
    int pts = int(frame.counts.x + 0.5);
    i = clamp(i, 0, pts - 1);
    s = float(i) / float(pts - 1);
    float f = s * h.color.a * (frame.counts.y - 2.0);
    vec3 p = mix(guidePoint(h.idx.x, f), guidePoint(h.idx.y, f), h.offset.w);
    p += mat3(frame.head) * h.offset.xyz * (1.0 - frame.shape.x * s);
    if (frame.shape.y > 0.0) {
        float w = uintBitsToFloat(h.idx.z) + float(i) * 2.1;
        p += frame.shape.y * s * vec3(sin(w), cos(w * 1.3), sin(w * 0.7 + 1.0));
    }
    return p;
}

vec3 across(vec3 t, vec3 toward) {
    vec3 side = cross(t, toward);
    if (dot(side, side) < 1e-12) side = cross(t, abs(t.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0));
    return normalize(side);
}

// A plait's offset across its line and its change along s
// (HairStrands::plaitOffset, which this mirrors).
vec2 plaitOffset(int strands, float width, float turns, float phase, vec2 rope, float s, out vec2 change) {
    float da = 6.28318531 * turns;
    float a = da * s + phase;
    float taper = 1.0 - 0.4 * smoothstep(0.94, 1.0, s);
    vec2 c = vec2(0.0), d = vec2(0.0);
    float strand = width;
    if (strands == 3) {
        float r = width / 1.45;
        c = r * vec2(sin(a), 0.5 * sin(2.0 * a));
        d = r * da * vec2(cos(a), cos(2.0 * a));
        strand = 0.45 * r;
    } else if (strands == 2) {
        float r = 0.5 * width;
        c = r * vec2(cos(a), sin(a));
        d = r * da * vec2(-sin(a), cos(a));
        strand = r;
    }
    float ra = a + rope.y, rr = strand * rope.x;
    change = (d + rr * da * vec2(-sin(ra), cos(ra))) * taper;
    return (c + rr * vec2(cos(ra), sin(ra))) * taper;
}

// A coil around the hair's line at s, or its place in a plait
// (HairStrands::coilOffset, which this mirrors): the offset and its
// change along s. t = the line's way.
vec3 coilOffset(HairGpu h, float s, vec3 t, out vec3 change) {
    change = vec3(0.0);
    if (frame.coil.x <= 0.0 || h.coil.x <= 0.0) return vec3(0.0);
    // The guide's frame there: across it, and how stretched.
    int strand = int(frame.counts.y + 0.5);
    int segs = strand - 2;
    float f = s * h.color.a * float(segs);
    int k = clamp(int(f), 0, segs - 1);
    int base = int(frame.coil.w + 0.5) + int(h.idx.x) * strand + 1;
    vec4 fr = mix(frame.guides[base + k], frame.guides[base + k + 1], clamp(f - float(k), 0.0, 1.0));
    int plait = int(frame.shape.w + 0.5);
    if (plait > 0) {
        vec3 pn = fr.xyz - t * dot(fr.xyz, t);
        pn = dot(pn, pn) > 1e-12 ? normalize(pn) : across(t, vec3(0.0, 1.0, 0.0));
        vec3 pb = cross(t, pn);
        vec2 dc;
        vec2 o = plaitOffset(plait, frame.coil.x, h.coil.x, h.coil.y, h.coil.zw, s, dc);
        change = pn * dc.x + pb * dc.y;
        return pn * o.x + pb * o.y;
    }
    // Pulled longer, a coil unwinds (the hair's own length stays).
    float pulled = frame.coil.z;
    float kk = max(fr.w, 1e-3);
    float unwind = pulled > 1.0001 ? kk * sqrt(max((pulled / kk) * (pulled / kk) - 1.0, 0.0)) / sqrt(pulled * pulled - 1.0) : 1.0;
    float grow = clamp(s * h.coil.x * 2.0, 0.0, 1.0);
    float r = frame.coil.x * h.coil.z * unwind * grow;
    vec3 n = fr.xyz - t * dot(fr.xyz, t);
    n = dot(n, n) > 1e-12 ? normalize(n) : across(t, vec3(0.0, 1.0, 0.0));
    vec3 b = cross(t, n);
    float da = 6.28318531 * h.coil.x;
    float a = da * s + h.coil.y;
    float ca = cos(a), sa = sin(a);
    vec3 spiral = n * ca + b * sa;
    float th = 0.25 * a;
    vec3 plane = n * cos(th) + b * sin(th), planeTurn = b * cos(th) - n * sin(th);
    float tri = asin(clamp(sa, -1.0, 1.0)) * 0.63661977;
    float z = clamp(frame.coil.y, 0.0, 1.0);
    vec3 spiralD = (b * ca - n * sa) * da;
    vec3 zigD = plane * ((ca >= 0.0 ? 1.0 : -1.0) * 0.63661977 * da) + planeTurn * (0.25 * da * tri);
    change = r * mix(spiralD, zigD, z);
    return r * mix(spiral, plane * tri, z);
}

// The hair's centre and direction at a corner.
void hairCentre(int hairIndex, int point, out vec3 p, out vec3 t, out float s) {
    HairGpu h = hairs[hairIndex];
    float s0, s1;
    p = hairPoint(h, point, s);
    vec3 a = hairPoint(h, point - 1, s0), b = hairPoint(h, point + 1, s1);
    vec3 d = (b - a) / max(s1 - s0, 1e-6);
    t = dot(d, d) > 1e-18 ? normalize(d) : vec3(0.0, -1.0, 0.0);
    vec3 change;
    p += coilOffset(h, s, t, change);
    d += change;
    t = dot(d, d) > 1e-18 ? normalize(d) : t;
    // Out of the head (a hair blending between guides that go round it
    // on either side would cut through it).
    vec3 fromHead = p - frame.headSphere.xyz;
    float l = length(fromHead);
    if (frame.headSphere.w > 0.0 && l < frame.headSphere.w && l > 1e-9) p = frame.headSphere.xyz + fromHead * (frame.headSphere.w / l);
}

#endif
