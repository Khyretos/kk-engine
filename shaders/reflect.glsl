// Reflections in screen space (ENG-F02, kke::ReflectionPass): one
// invocation per pixel of a half-resolution image. Where the finished frame
// shows a smooth lit surface (alpha >= 2: 2 + smoothness, written by the lit
// shaders through reflectionMark), the reflected ray is marched through the
// depth buffer in fixed steps; where it meets a surface, that surface's
// light is what the pixel reflects (a = 1), else nothing is written (a = 0)
// and the lit shaders keep the prefiltered sky. One ray, fixed steps: no
// noise, nothing blended over frames (docs/RENDERING_PRINCIPLES.md). The next
// frame's lit shaders read the result (screenReflectionAt, lighting_ubo.glsl).
// reflect.comp and reflect_ms.comp include it (DEPTH_MS: sample 0 of a
// multisampled depth); ray_reflect.comp and ray_reflect_ms.comp with
// RAY_QUERY too (KKE ray tracing): a ray the march didn't settle goes on
// through simple stand-ins of the scene (kke::RayScene), within a budget.

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D sceneLight; // this frame's scene light, alpha = the reflection mark
#ifdef DEPTH_MS
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D reflectOut;   // rgb = reflected light, a = 1 where found
layout(set = 0, binding = 3, r32f) uniform writeonly image2D reflectDepthOut; // view depth (m), 0 = nothing to reflect

#ifdef RAY_QUERY
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
layout(set = 0, binding = 4) uniform accelerationStructureEXT standIns;
// kke::RayMaterial, per instance (its custom index).
struct RayMaterial {
    vec4 color;        // linear albedo
    uvec2 vertices;    // address, (0, 0): a stand-in with no vertex data
    uvec2 indices;
    uint stride, normalOffset, colorOffset, unused; // bytes; colorOffset 0xffffffff: no vertex colour
};
layout(std430, set = 0, binding = 5) readonly buffer Materials { RayMaterial materials[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Floats { float f[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Uints { uint u[]; };
layout(std430, set = 0, binding = 6) buffer Budget { uint raysCast; };          // reset to 0 each frame
layout(set = 0, binding = 7) uniform Rays {
    mat4 viewToWorld;  // inverse view
    vec4 sunDirection; // xyz = towards the sun, w = ray budget this frame
    vec4 sunColor;     // rgb = colour x intensity
    vec4 ambient;      // rgb = the sky's average light, w = 1: classic ray tracing (no march, every smooth pixel)
} rays;

// base + offset bytes, as a 64-bit address in two halves.
uvec2 addressAt(uvec2 base, uint offset) {
    uint carry;
    uint lo = uaddCarry(base.x, offset, carry);
    return uvec2(lo, base.y + carry);
}
vec3 vec3At(uvec2 base, uint offset) {
    Floats f = Floats(addressAt(base, offset));
    return vec3(f.f[0], f.f[1], f.f[2]);
}

// Whether anything is between p and the sun (classic ray tracing's shadows).
bool sunBlocked(vec3 p) {
    rayQueryEXT s;
    rayQueryInitializeEXT(s, standIns, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, p, 0.02,
                          normalize(rays.sunDirection.xyz), 200.0);
    while (rayQueryProceedEXT(s)) {
    }
    return rayQueryGetIntersectionTypeEXT(s, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
}

// The light the first thing along a ray sends back. A stand-in: its colour
// under the sky and a third of the sun (no normal: an average side). Real
// geometry: its interpolated normal and vertex colour, the sky and the sun,
// with the sun's shadow traced too in classic ray tracing.
vec4 traceStandIns(vec3 viewOrigin, vec3 viewDir, float maxDist) {
    if (rays.ambient.w < 0.5 && atomicAdd(raysCast, 1u) >= uint(rays.sunDirection.w)) return vec4(0.0); // over budget: the sky
    vec3 o = (rays.viewToWorld * vec4(viewOrigin, 1.0)).xyz;
    vec3 d = normalize((rays.viewToWorld * vec4(viewDir, 0.0)).xyz);
    rayQueryEXT q;
    rayQueryInitializeEXT(q, standIns, gl_RayFlagsOpaqueEXT, 0xFF, o, 0.02, d, maxDist);
    while (rayQueryProceedEXT(q)) {
    }
    if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT) return vec4(0.0);
    RayMaterial m = materials[rayQueryGetIntersectionInstanceCustomIndexEXT(q, true)];
    vec3 albedo = m.color.rgb;
    if (m.vertices == uvec2(0u)) return vec4(albedo * (rays.ambient.rgb + 0.3 * rays.sunColor.rgb), 1.0);
    // The hit triangle's vertices, by index, and its point's barycentric weights.
    Uints idx = Uints(m.indices);
    uint tri = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true)) * 3u;
    uint i0 = idx.u[tri], i1 = idx.u[tri + 1u], i2 = idx.u[tri + 2u];
    vec2 b = rayQueryGetIntersectionBarycentricsEXT(q, true);
    vec3 w = vec3(1.0 - b.x - b.y, b.x, b.y);
    vec3 n = vec3At(m.vertices, i0 * m.stride + m.normalOffset) * w.x + vec3At(m.vertices, i1 * m.stride + m.normalOffset) * w.y
           + vec3At(m.vertices, i2 * m.stride + m.normalOffset) * w.z;
    if (m.colorOffset != 0xffffffffu) {
        vec3 c = vec3At(m.vertices, i0 * m.stride + m.colorOffset) * w.x + vec3At(m.vertices, i1 * m.stride + m.colorOffset) * w.y
               + vec3At(m.vertices, i2 * m.stride + m.colorOffset) * w.z;
        albedo *= pow(max(c, vec3(0.0)), vec3(2.2)); // sRGB vertex colour
    }
    mat4x3 toWorld = rayQueryGetIntersectionObjectToWorldEXT(q, true);
    n = normalize(mat3(toWorld) * n);
    if (dot(n, d) > 0.0) n = -n; // facing the ray
    vec3 p = o + d * rayQueryGetIntersectionTEXT(q, true) + n * 0.01;
    float sun = max(dot(n, normalize(rays.sunDirection.xyz)), 0.0);
    if (sun > 0.0 && rays.ambient.w > 0.5 && sunBlocked(p)) sun = 0.0; // the sun's shadow: classic only
    return vec4(albedo * (rays.ambient.rgb + sun * rays.sunColor.rgb), 1.0);
}
#endif

layout(push_constant) uniform Push {
    mat4 proj;      // the frame's projection (Vulkan's flipped Y)
    vec4 sizeLimits; // xy = full-resolution size, z = the smoothest-to-trace limit (1 - max roughness), w = max distance (m)
    vec4 march;      // x = steps, y = thickness (m)
} pc;

mat4 invProj; // inverse(pc.proj), once per invocation (main)

vec4 viewPos(ivec2 p) {
    float d = depthAt(p);
    if (d >= 1.0) return vec4(0.0);
    vec2 ndc = (vec2(p) + 0.5) / pc.sizeLimits.xy * 2.0 - 1.0;
    vec4 v = invProj * vec4(ndc, d, 1.0);
    return vec4(v.xyz / v.w, 1.0);
}

void main() {
    ivec2 half_ = ivec2(gl_GlobalInvocationID.xy);
    if (half_.x >= imageSize(reflectOut).x || half_.y >= imageSize(reflectOut).y) return;
    invProj = inverse(pc.proj);
    ivec2 full = ivec2(pc.sizeLimits.xy);
    ivec2 p = min(half_ * 2, full - 1);
    vec4 mark = texelFetch(sceneLight, p, 0);
    vec4 c = viewPos(p);
    float smoothness = mark.a - 2.0;
    if (c.w == 0.0 || smoothness < pc.sizeLimits.z) {
        imageStore(reflectOut, half_, vec4(0.0));
        imageStore(reflectDepthOut, half_, vec4(0.0));
        return;
    }
    // The normal from the depth around it (as screen_gi.glsl does).
    vec4 r = viewPos(min(p + ivec2(1, 0), full - 1)), l = viewPos(max(p - ivec2(1, 0), ivec2(0)));
    vec4 u = viewPos(max(p - ivec2(0, 1), ivec2(0))), dn = viewPos(min(p + ivec2(0, 1), full - 1));
    vec3 dx = (r.w == 0.0 || (l.w != 0.0 && abs(l.z - c.z) < abs(r.z - c.z))) ? c.xyz - l.xyz : r.xyz - c.xyz;
    vec3 dy = (dn.w == 0.0 || (u.w != 0.0 && abs(u.z - c.z) < abs(dn.z - c.z))) ? c.xyz - u.xyz : dn.xyz - c.xyz;
    vec3 N = normalize(cross(dy, dx));
    vec3 V = normalize(-c.xyz);
    if (dot(N, V) < 0.0) N = -N;
    vec3 R = reflect(-V, N);
    float depth = -c.z;

#ifdef RAY_QUERY
    if (rays.ambient.w > 0.5) {
        // Classic ray tracing: the ray itself, through the real geometry.
        imageStore(reflectOut, half_, traceStandIns(c.xyz + N * (0.01 * depth), R, 100.0));
        imageStore(reflectDepthOut, half_, vec4(depth));
        return;
    }
#endif
    // March along the ray's line on screen in equal steps (pixels, not
    // metres, so nothing between two samples is skipped near or far),
    // depth interpolated perspective-correctly; then four halvings.
    int steps = int(pc.march.x);
    float maxDist = pc.sizeLimits.w, thickness = pc.march.y;
    vec3 origin = c.xyz + N * (0.01 * depth);
    float reach = maxDist;
    if (R.z > 0.0) reach = min(reach, (-0.05 - origin.z) / R.z); // stop short of the camera plane
    vec3 end = origin + R * max(reach, 0.0);
    vec4 h0 = pc.proj * vec4(origin, 1.0), h1 = pc.proj * vec4(end, 1.0);
    vec2 s0 = (h0.xy / h0.w * 0.5 + 0.5) * pc.sizeLimits.xy, s1 = (h1.xy / h1.w * 0.5 + 0.5) * pc.sizeLimits.xy;
    float k0 = 1.0 / h0.w, k1 = 1.0 / h1.w; // 1 / view depth: linear on screen
    float prev = 0.0, hitT = -1.0;
    bool leftScreen = false;
    for (int k = 1; k <= steps; ++k) {
        float f = float(k) / float(steps);
        vec2 px = mix(s0, s1, f);
        if (px.x < 0.0 || px.y < 0.0 || px.x >= pc.sizeLimits.x || px.y >= pc.sizeLimits.y) {
            leftScreen = true; // what it meets next is off screen
            break;
        }
        float rayZ = -1.0 / mix(k0, k1, f);
        vec4 sp = viewPos(ivec2(px));
        if (sp.w == 0.0) { prev = f; continue; } // sky there: keep going
        float behind = sp.z - rayZ; // > 0: the ray is behind what the screen shows
        if (behind > 0.0 && behind < thickness) {
            float lo = prev, hi = f;
            for (int b = 0; b < 4; ++b) {
                float mid = 0.5 * (lo + hi);
                vec4 sm = viewPos(ivec2(mix(s0, s1, mid)));
                if (sm.w != 0.0 && sm.z - (-1.0 / mix(k0, k1, mid)) > 0.0) hi = mid; else lo = mid;
            }
            hitT = hi;
            break;
        }
        prev = f;
    }
    if (hitT < 0.0) {
#ifdef RAY_QUERY
        // Only rays that left the screen: one that stayed on it and met
        // nothing goes to the sky (stand-ins are coarser than what's drawn).
        imageStore(reflectOut, half_, leftScreen ? traceStandIns(origin, R, maxDist) : vec4(0.0));
#else
        imageStore(reflectOut, half_, vec4(0.0));
#endif
        imageStore(reflectDepthOut, half_, vec4(depth));
        return;
    }
    vec2 hp = mix(s0, s1, hitT);
    vec3 light = texelFetch(sceneLight, ivec2(clamp(hp, vec2(0.0), pc.sizeLimits.xy - 1.0)), 0).rgb;
    imageStore(reflectOut, half_, vec4(light, 1.0));
    imageStore(reflectDepthOut, half_, vec4(depth));
}
