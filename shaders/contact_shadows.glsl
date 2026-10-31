// Contact shadows (ENG-F16, kke::ContactShadowPass; kke/ContactShadows.h):
// after the frame, a short march towards the sun through its depth from
// each pixel. Writes how lit it is (r) and its view depth (g, 0 = not
// marched) for the next frame's lit shaders (lighting_ubo.glsl
// contactShadowAt). The loop mirrors kke::contactShadowMarch.
// #include after #version, DEPTH_MS and the extensions.
layout(local_size_x = 8, local_size_y = 8) in;

#ifdef DEPTH_MS
layout(set = 0, binding = 0) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D contactOut;

layout(push_constant) uniform Push {
    mat4 proj;    // the frame's projection (Vulkan's flipped Y)
    vec4 toSun;   // xyz = towards the sun in view space, w = metres marched
    vec4 limits;  // xy = size in pixels, z = fade start, w = farthest marched (m)
    vec4 march;   // x = steps, y = thickness (m)
} pc;

mat4 invProj; // inverse(pc.proj), once per pixel

vec3 viewAt(ivec2 p, float d) {
    vec2 ndc = (vec2(p) + 0.5) / pc.limits.xy * 2.0 - 1.0;
    vec4 v = invProj * vec4(ndc, d, 1.0);
    return v.xyz / v.w;
}

// The view depth (positive metres) drawn at a view-space point's pixel; 0 off screen or sky.
float sceneDepthAt(vec3 v) {
    vec4 c = pc.proj * vec4(v, 1.0);
    if (c.w <= 0.0) return 0.0;
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return 0.0;
    ivec2 q = ivec2(uv * pc.limits.xy);
    float d = depthAt(q);
    if (d >= 1.0) return 0.0;
    return -viewAt(q, d).z;
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= int(pc.limits.x) || p.y >= int(pc.limits.y)) return;
    invProj = inverse(pc.proj);
    float d = depthAt(p);
    if (d >= 1.0) {
        imageStore(contactOut, p, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    vec3 pos = viewAt(p, d);
    float dist = length(pos);
    if (dist > pc.limits.w) {
        imageStore(contactOut, p, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    int steps = max(1, int(pc.march.x));
    float stepLen = pc.toSun.w / float(steps);
    // Interleaved gradient noise: a different first step per pixel, so the bands blur into grain.
    float jitter = fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715))));
    // The normal from the depth around it (as shadow_rays.glsl does): start the march just off the
    // surface along it, so a curved body doesn't shade itself from its own depth steps.
    ivec2 full = ivec2(pc.limits.xy);
    ivec2 pr = min(p + ivec2(1, 0), full - 1), pl = max(p - ivec2(1, 0), ivec2(0));
    ivec2 pu = max(p - ivec2(0, 1), ivec2(0)), pd = min(p + ivec2(0, 1), full - 1);
    vec3 r = viewAt(pr, depthAt(pr)), l = viewAt(pl, depthAt(pl)), u = viewAt(pu, depthAt(pu)), dn = viewAt(pd, depthAt(pd));
    vec3 dx = abs(l.z - pos.z) < abs(r.z - pos.z) ? pos - l : r - pos;
    vec3 dy = abs(u.z - pos.z) < abs(dn.z - pos.z) ? pos - u : dn - pos;
    vec3 N = normalize(cross(dy, dx));
    if (dot(N, -pos) < 0.0) N = -N;
    // Facing away from the sun: the lighting's N.L already darkens it.
    if (dot(N, pc.toSun.xyz) <= 0.0) {
        imageStore(contactOut, p, vec4(1.0, -pos.z, 0.0, 0.0));
        return;
    }
    vec3 start = pos + N * (0.015 + 0.002 * dist) + pc.toSun.xyz * 0.01;
    float lit = 1.0;
    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + jitter) * stepLen;
        vec3 q = start + pc.toSun.xyz * t;
        float rayDepth = -q.z;
        if (rayDepth <= 0.0) break;
        float scene = sceneDepthAt(q);
        if (scene <= 0.0) continue;
        float inFront = rayDepth - scene;
        if (inFront > 0.01 + 0.002 * rayDepth && inFront < pc.march.y) {
            lit = float(i) / float(steps) * 0.6;
            break;
        }
    }
    // kke::contactShadowFade
    float keep = dist <= pc.limits.z ? 1.0 : clamp(1.0 - (dist - pc.limits.z) / max(pc.limits.w - pc.limits.z, 0.001), 0.0, 1.0);
    imageStore(contactOut, p, vec4(mix(1.0, lit, keep), -pos.z, 0.0, 0.0));
}
