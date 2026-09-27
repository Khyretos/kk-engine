// Drawn hairs built on the GPU from the simulated guide strands
// (kke::HairRenderer, kke/HairRenderer.h): the CPU uploads only the guides'
// points each frame; every drawn hair is expanded here from its guide, the
// neighbour it leans towards and its offset. #define HAIR_SET before
// including (the descriptor set holding the two buffers).
#ifndef KKE_HAIR_COMMON_GLSL
#define KKE_HAIR_COMMON_GLSL

// Mirrors HairRenderer's HairGpu (std430, 48 bytes).
struct HairGpu {
    uvec4 idx;    // x = guide, y = the neighbour it leans to, z = frizz phase (float bits)
    vec4 offset;  // xyz = offset from the guide (head space), w = how much of the neighbour
    vec4 color;   // rgb = tint, a = its length (fraction of the guide's)
};

layout(std430, set = HAIR_SET, binding = 0) readonly buffer Hairs { HairGpu hairs[]; };

// Mirrors HairRenderer's FrameHeader, then the guide points.
layout(std430, set = HAIR_SET, binding = 1) readonly buffer Frame {
    mat4 head;      // the head's rotation now (offsets turn with it)
    vec4 counts;    // x = points drawn per hair, y = vertices per guide strand, z = drawn width (m), w = a pixel's size 1 m away
    vec4 shape;     // x = clump, y = frizz (m), z = width in the shadow map (m)
    vec4 rootColor; // sRGB
    vec4 tipColor;  // sRGB, a = shine
    vec4 look;      // x = highlight shift, y = roughness, z = every z-th hair is drawn (level of detail), w = width scale for it
    vec4 guides[];  // xyz, guide after guide, root first
} frame;

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

// The hair's centre and direction at a corner.
void hairCentre(int hairIndex, int point, out vec3 p, out vec3 t, out float s) {
    HairGpu h = hairs[hairIndex];
    float s0, s1;
    p = hairPoint(h, point, s);
    vec3 a = hairPoint(h, point - 1, s0), b = hairPoint(h, point + 1, s1);
    t = b - a;
    t = dot(t, t) > 1e-18 ? normalize(t) : vec3(0.0, -1.0, 0.0);
}

vec3 across(vec3 t, vec3 toward) {
    vec3 side = cross(t, toward);
    if (dot(side, side) < 1e-12) side = cross(t, abs(t.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0));
    return normalize(side);
}

#endif
