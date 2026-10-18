// Ray-traced sun shadows (ENG-F07, kke::ShadowRayPass; kke/ShadowRays.h):
// after the frame, a ray towards the sun from each surface the setting
// traces, through the frame's kke::RayScene. Writes how lit it is and its
// view depth, for the next frame's lit shaders (lighting_ubo.glsl
// shadowRayAt). #include after #version, DEPTH_MS and the extensions.
layout(local_size_x = 8, local_size_y = 8) in;

#ifdef DEPTH_MS
layout(set = 0, binding = 0) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D shadowOut; // r = lit 0..1, g = view depth (m), 0 = not traced

#define RAY_SET 0
#define RAY_TLAS 2
#define RAY_MATERIALS 3
#define RAY_PARAMS 4
#include "ray_trace.glsl"

layout(push_constant) uniform Push {
    mat4 invProj; // inverse of the frame's projection (Vulkan's flipped Y)
    vec4 limits;  // xy = size in pixels, z = KKE's nearest traced distance, w = the farthest (m)
} pc;

vec4 viewPos(ivec2 p) {
    float d = depthAt(p);
    if (d >= 1.0) return vec4(0.0);
    vec2 ndc = (vec2(p) + 0.5) / pc.limits.xy * 2.0 - 1.0;
    vec4 v = pc.invProj * vec4(ndc, d, 1.0);
    return vec4(v.xyz / v.w, 1.0);
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 full = ivec2(pc.limits.xy);
    if (p.x >= full.x || p.y >= full.y) return;
    vec4 c = viewPos(p);
    float dist = length(c.xyz);
    // kke::shadowRayTraced: classic traces everything in reach, KKE only past the shadow map.
    bool classic = rays.ambient.w > 0.5;
    if (c.w == 0.0 || dist > pc.limits.w || (!classic && dist < pc.limits.z)) {
        imageStore(shadowOut, p, vec4(0.0));
        return;
    }
    // The normal from the depth around it (as reflect.glsl does), to start the ray just off the surface.
    vec4 r = viewPos(min(p + ivec2(1, 0), full - 1)), l = viewPos(max(p - ivec2(1, 0), ivec2(0)));
    vec4 u = viewPos(max(p - ivec2(0, 1), ivec2(0))), dn = viewPos(min(p + ivec2(0, 1), full - 1));
    vec3 dx = (r.w == 0.0 || (l.w != 0.0 && abs(l.z - c.z) < abs(r.z - c.z))) ? c.xyz - l.xyz : r.xyz - c.xyz;
    vec3 dy = (dn.w == 0.0 || (u.w != 0.0 && abs(u.z - c.z) < abs(dn.z - c.z))) ? c.xyz - u.xyz : dn.xyz - c.xyz;
    vec3 N = normalize(cross(dy, dx));
    if (dot(N, -c.xyz) < 0.0) N = -N;
    vec3 worldPos = (rays.viewToWorld * vec4(c.xyz, 1.0)).xyz;
    vec3 worldN = normalize(mat3(rays.viewToWorld) * N);
    vec3 sun = normalize(rays.sunDirection.xyz);
    // Facing away from the sun: in its own shadow, no ray needed.
    float lit = dot(worldN, sun) <= 0.0 ? 0.0 : (sunBlocked(worldPos + worldN * (0.02 + 0.002 * dist)) ? 0.0 : 1.0);
    imageStore(shadowOut, p, vec4(lit, -c.z, 0.0, 0.0));
}
