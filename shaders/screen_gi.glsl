// Screen-space global illumination with a visibility bitmask (ENG-F01):
// one invocation per pixel of the half-resolution GI image. The GPU copy of
// kke::screenGi (engine/src/ScreenGi.cpp, tested there): change both
// together. Fixed slice directions and sample distances, the same for every
// pixel and frame: no noise, nothing to blend over frames
// (docs/RENDERING_PRINCIPLES.md). screen_gi.comp and screen_gi_ms.comp
// include it, the latter reading sample 0 of a multisampled depth.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D sceneLight; // this frame's scene light (single sample)
#ifdef DEPTH_MS
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D giOut;   // rgb = bounce, a = visibility
layout(set = 0, binding = 3, r32f) uniform writeonly image2D giDepthOut; // view depth (m), 0 = sky

layout(push_constant) uniform Push {
    mat4 invProj;   // the frame's projection (Vulkan's flipped Y), inverted
    vec4 sizeRadius; // xy = full-resolution size, z = radius (m), w = thickness (m)
    vec4 counts;     // x = slices, y = steps, z = focal length in pixels (full resolution)
} pc;

const float PI = 3.14159265;

// View-space position of full-resolution pixel p (its centre), or w = 0 for the sky.
vec4 viewPos(ivec2 p) {
    float d = depthAt(p);
    if (d >= 1.0) return vec4(0.0);
    vec2 ndc = (vec2(p) + 0.5) / pc.sizeRadius.xy * 2.0 - 1.0;
    vec4 v = pc.invProj * vec4(ndc, d, 1.0);
    return vec4(v.xyz / v.w, 1.0);
}

uint sectors(float t0, float t1) {
    if (t1 <= t0) return 0u;
    int i0 = clamp(int(floor(t0 * 32.0)), 0, 32);
    int i1 = clamp(int(ceil(t1 * 32.0)), 0, 32);
    if (i1 <= i0) return 0u;
    uint run = i1 - i0 == 32 ? 0xffffffffu : (1u << uint(i1 - i0)) - 1u;
    return run << uint(i0);
}

float sectorOf(float a, float n) { return 0.5 + 0.5 * sin(clamp(a - n, -0.5 * PI, 0.5 * PI)); }

void main() {
    ivec2 half_ = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = imageSize(giOut);
    if (half_.x >= outSize.x || half_.y >= outSize.y) return;
    ivec2 full = ivec2(pc.sizeRadius.xy);
    ivec2 p = min(half_ * 2, full - 1);
    vec4 c = viewPos(p);
    if (c.w == 0.0) {
        imageStore(giOut, half_, vec4(0.0, 0.0, 0.0, 1.0));
        imageStore(giDepthOut, half_, vec4(0.0));
        return;
    }
    // The normal from the depth around it: on each axis the side nearer in depth (no smearing across edges).
    vec4 r = viewPos(min(p + ivec2(1, 0), full - 1)), l = viewPos(max(p - ivec2(1, 0), ivec2(0)));
    vec4 u = viewPos(max(p - ivec2(0, 1), ivec2(0))), dn = viewPos(min(p + ivec2(0, 1), full - 1));
    vec3 dx = (r.w == 0.0 || (l.w != 0.0 && abs(l.z - c.z) < abs(r.z - c.z))) ? c.xyz - l.xyz : r.xyz - c.xyz;
    vec3 dy = (dn.w == 0.0 || (u.w != 0.0 && abs(u.z - c.z) < abs(dn.z - c.z))) ? c.xyz - u.xyz : dn.xyz - c.xyz;
    vec3 normal = normalize(cross(dy, dx));
    if (dot(normal, -c.xyz) < 0.0) normal = -normal;

    float depth = -c.z;
    vec3 P = c.xyz + normal * (0.002 * depth);
    vec3 V = normalize(-P);
    float radius = pc.sizeRadius.z, thickness = pc.sizeRadius.w;
    int slices = int(pc.counts.x), steps = int(pc.counts.y);
    float radiusPx = radius * pc.counts.z / depth;
    vec3 bounce = vec3(0.0);
    float visible = 0.0, weight = 0.0;
    for (int i = 0; i < slices; ++i) {
        float phi = (float(i) + 0.5) * PI / float(slices);
        // View space has y up; the image has y down (the flipped projection).
        vec3 dir = vec3(cos(phi), sin(phi), 0.0);
        vec3 side = dir - V * dot(dir, V);
        if (length(side) < 1e-5) continue;
        vec3 T = normalize(side);
        vec3 axis = cross(V, T);
        vec3 np = normal - axis * dot(normal, axis);
        float len = length(np);
        if (len < 1e-4) continue;
        float n = atan(dot(np, T), dot(np, V));
        uint occluded = 0u;
        vec3 light = vec3(0.0);
        for (int s = 0; s < 2; ++s) {
            float sgn = s == 0 ? 1.0 : -1.0;
            for (int k = 0; k < steps; ++k) {
                float f = float(k + 1) / float(steps);
                float dist = max(float(k + 1), radiusPx * f * f);
                ivec2 q = ivec2(floor(vec2(p) + vec2(sgn * dir.x, -sgn * dir.y) * dist + 0.5));
                if (q.x < 0 || q.y < 0 || q.x >= full.x || q.y >= full.y) break;
                vec4 sp = viewPos(q);
                if (sp.w == 0.0) continue;
                vec3 front = sp.xyz - P;
                if (length(front) > radius) continue;
                float tf = sectorOf(atan(dot(front, T), dot(front, V)), n);
                vec3 back = sp.xyz + normalize(sp.xyz) * thickness - P;
                float tb = sectorOf(atan(dot(back, T), dot(back, V)), n);
                uint bits = sectors(min(tf, tb), max(tf, tb));
                uint fresh = bits & ~occluded;
                if (fresh != 0u) light += texelFetch(sceneLight, q, 0).rgb * (float(bitCount(fresh)) / 32.0);
                occluded |= bits;
            }
        }
        bounce += light * len;
        visible += (1.0 - float(bitCount(occluded)) / 32.0) * len;
        weight += len;
    }
    vec4 gi = weight > 0.0 ? vec4(bounce / weight, visible / weight) : vec4(0.0, 0.0, 0.0, 1.0);
    imageStore(giOut, half_, gi);
    imageStore(giDepthOut, half_, vec4(depth));
}
