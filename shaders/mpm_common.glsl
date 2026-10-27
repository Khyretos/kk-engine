// kke::MpmWater on the GPU (kke::MpmWaterGpu, engine/src/MpmWaterGpu.cpp):
// what the four passes share. The CPU's copy is engine/src/MpmWater.cpp;
// keep the two the same.
#ifndef KKE_MPM_COMMON_GLSL
#define KKE_MPM_COMMON_GLSL

// Per particle, 5 vec4: position + volume J, velocity, then the velocity
// gradient C by columns.
layout(std430, set = 0, binding = 0) buffer Particles { vec4 particles[]; };
// Per node, 4 ints: momentum (then velocity, as float bits) and mass,
// in fixed point (kGridScale) while particles write them.
layout(std430, set = 0, binding = 1) buffer Grid { int grid[]; };
// Per body, 8 vec4 (MpmWaterGpu's packBody): position + shape (1 = sphere),
// rotation (x, y, z, w), half extents + 1 / mass, velocity + friction,
// angular velocity, 1 / inertia (body axes), and the step's velocity
// changes so far (linear, angular).
layout(std430, set = 0, binding = 2) buffer Bodies { vec4 bodies[]; };
// Per body, 8 ints: this substep's impulse and angular impulse (kImpulseScale).
layout(std430, set = 0, binding = 3) buffer Impulses { int impulses[]; };
// The step's result for the CPU: the fastest particle's speed (float
// bits), then per particle position + J and velocity.
layout(std430, set = 0, binding = 4) buffer Out { uint maxSpeedBits; uint pad0, pad1, pad2; vec4 outParticles[]; };

layout(push_constant) uniform Push {
    vec4 origin;   // xyz = grid node (0, 0, 0), w = cell size
    vec4 wallLo;   // xyz = the tank's low corner, w = 1 / cell size
    vec4 wallHi;   // xyz = its high corner, w = substep (s)
    vec4 gravity;  // xyz, w = sound speed squared
    ivec4 dims;    // xyz = nodes per axis, w = particles
    vec4 misc;     // x = viscosity, y = tension (the most pull, as 1 - J), z = 1 - wall friction, w = speed cap
    vec4 misc2;    // x = particle mass (kg), y = bodies, z = 1: write Out (the step's last substep)
} pc;

const float kGridScale = 524288.0;   // MpmWaterGpu::kGridScale (2^19)
const float kImpulseScale = 16384.0; // MpmWaterGpu::kImpulseScale (2^14)
const float kBodyBand = 0.5;         // engine/src/MpmWater.cpp's

// The invocation's index over a 2D dispatch of 64-wide groups (65,535
// groups along x at most; the CPU spreads bigger counts over y).
uint flatIndex() { return (gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x) * 64u + gl_LocalInvocationID.x; }

int toFixed(float v, float scale) { return int(floor(v * scale + 0.5)); }

int nodeIndex(ivec3 c) { return (c.z * pc.dims.y + c.y) * pc.dims.x + c.x; }

vec3 weightsOf(float fx) {
    float a = 1.5 - fx, b = fx - 1.0, c = fx - 0.5;
    return vec3(0.5 * a * a, 0.75 - b * b, 0.5 * c * c);
}

vec3 quatRotate(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
vec4 quatConj(vec4 q) { return vec4(-q.xyz, q.w); }
mat3 quatMatrix(vec4 q) {
    return mat3(quatRotate(q, vec3(1, 0, 0)), quatRotate(q, vec3(0, 1, 0)), quatRotate(q, vec3(0, 0, 1)));
}

// Signed distance to body b (negative inside) and the outward normal.
float bodyDistance(int b, vec3 p, out vec3 normal) {
    vec4 posShape = bodies[b * 8 + 0];
    vec4 q = bodies[b * 8 + 1];
    vec3 halfSize = bodies[b * 8 + 2].xyz;
    vec3 r = p - posShape.xyz;
    if (posShape.w > 0.5) {
        float len = length(r);
        normal = len > 1e-6 ? r / len : vec3(0.0, 1.0, 0.0);
        return len - halfSize.x;
    }
    vec3 local = quatRotate(quatConj(q), r);
    vec3 d = abs(local) - halfSize;
    vec3 o = max(d, vec3(0.0));
    float outside = length(o);
    vec3 n;
    if (outside > 0.0) {
        n = o / outside * sign(local);
    } else {
        int axis = d.x > d.y ? (d.x > d.z ? 0 : 2) : (d.y > d.z ? 1 : 2);
        n = vec3(0.0);
        n[axis] = local[axis] < 0.0 ? -1.0 : 1.0;
    }
    normal = quatRotate(q, n);
    return outside + min(max(d.x, max(d.y, d.z)), 0.0);
}

#endif
