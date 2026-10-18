// Inline ray queries through a kke::RayScene and how a hit looks (ENG-F02):
// shared by reflections (shaders/reflect.glsl) and refraction
// (shaders/translucent_refract.glsl). #define RAY_SET, RAY_TLAS,
// RAY_MATERIALS and RAY_PARAMS (and RAY_BUDGET for a ray count) first.
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
layout(set = RAY_SET, binding = RAY_TLAS) uniform accelerationStructureEXT standIns;
// kke::RayMaterial, per instance (its custom index).
struct RayMaterial {
    vec4 color;        // linear albedo
    uvec2 vertices;    // address, (0, 0): a stand-in with no vertex data
    uvec2 indices;
    uint stride, normalOffset, colorOffset, unused; // bytes; colorOffset 0xffffffff: no vertex colour
};
layout(std430, set = RAY_SET, binding = RAY_MATERIALS) readonly buffer Materials { RayMaterial materials[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Floats { float f[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Uints { uint u[]; };
#ifdef RAY_BUDGET
layout(std430, set = RAY_SET, binding = RAY_BUDGET) buffer Budget { uint raysCast; }; // reset to 0 each frame
#endif
layout(set = RAY_SET, binding = RAY_PARAMS) uniform Rays {
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

// The light the first thing along a world-space ray sends back (a = 1), or
// (0, 0, 0, 0) for nothing. A stand-in: its colour
// under the sky and a third of the sun (no normal: an average side). Real
// geometry: its interpolated normal and vertex colour, the sky and the sun,
// with the sun's shadow traced too in classic ray tracing.
vec4 traceWorld(vec3 o, vec3 d, float maxDist) {
#ifdef RAY_BUDGET
    if (rays.ambient.w < 0.5 && atomicAdd(raysCast, 1u) >= uint(rays.sunDirection.w)) return vec4(0.0); // over budget: the sky
#endif
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
