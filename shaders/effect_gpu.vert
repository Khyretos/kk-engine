#version 450
#extension GL_GOOGLE_include_directive : require
// Particle effects the GPU moves (kke::ParticleEffects, ENG-G18 part 4):
// each particle was written once, when it was born, into a ring of these
// records; this works out where it is now in closed form, from its birth
// and the time, with no state kept between frames. The same formulas run
// on the CPU in engine/src/ParticleMotion.cpp (tested against the stepped
// simulation): change both together. One instance per ring slot; a slot
// whose particle is not alive draws nothing.

#include "lighting_ubo.glsl"
#include "effect_quad.glsl"

struct GpuParticle {
    vec4 posBirth;     // xyz = where it was born, w = birth time (s)
    vec4 velLife;      // xyz = velocity at birth, w = life (s)
    vec4 colorOpacity; // rgb = colour, a = opacity
    vec4 colorEnd;     // rgb = colour it fades to (x < 0: none), a = flake shape / ring flat (1)
    vec4 motion;       // drag, rise, growth, spin
    vec4 shape;        // radius, stretch (spark) or thickness (ring), flutter, floor
    vec4 turn;         // angle, flip angle, flip, seed
    vec4 swirl;        // xyz = origin, w = vortex
};
layout(std430, set = 2, binding = 0) readonly buffer Particles { GpuParticle particles[]; };

layout(push_constant) uniform Frame {
    vec4 timeWind; // x = the sim's time (s), yzw = wind (m/s)
    vec4 kind;     // x = kke::ParticleSim::Kind of this ring
} frame;

const int kSmoke = 0;
const int kSpark = 1;
const int kGlow = 2;
const int kFlake = 3;
const int kRing = 4;

// v' = a - k v from v0: offset after t, and velocity.
void dragged(float k, vec3 a, vec3 v0, float t, out vec3 x, out vec3 v) {
    if (k < 1e-4) {
        v = v0 + a * t;
        x = v0 * t + 0.5 * a * t * t;
        return;
    }
    float e = exp(-k * t);
    vec3 terminal = a / k;
    v = terminal + (v0 - terminal) * e;
    x = terminal * t + (v0 - terminal) * ((1.0 - e) / k);
}

void main() {
    int kind = int(frame.kind.x + 0.5);
    GpuParticle p = particles[gl_InstanceIndex];
    float t = frame.timeWind.x - p.posBirth.w;
    float life = p.velLife.w;
    if (t < 0.0 || t >= life) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside the view: nothing drawn
        return;
    }
    vec3 wind = frame.timeWind.yzw;
    float drag = p.motion.x, rise = p.motion.y, growth = p.motion.z, spin = p.motion.w;
    float radius = p.shape.x;
    float angle = p.turn.x, flipAngle = p.turn.y;
    vec3 x = vec3(0.0), v = p.velLife.xyz;
    if (kind == kSmoke || kind == kGlow) {
        dragged(drag, drag * wind + vec3(0.0, rise, 0.0), p.velLife.xyz, t, x, v);
        radius += growth * (1.0 - exp(-0.6 * t)) / 0.6;
        angle += spin * t;
    } else if (kind == kSpark) {
        dragged(drag, vec3(0.0, rise, 0.0), p.velLife.xyz, t, x, v);
    } else if (kind == kFlake) {
        vec3 xa, va, xb, vb;
        dragged(drag, drag * vec3(wind.x, 0.0, wind.z), vec3(p.velLife.x, 0.0, p.velLife.z), t, xa, va);
        dragged(0.5 * drag, vec3(0.0, rise, 0.0), vec3(0.0, p.velLife.y, 0.0), t, xb, vb);
        x = xa + xb;
        v = va + vb;
        float px = p.turn.w * 6.2831853, pz = p.turn.w * 9.1;
        x.x += p.shape.z * (cos(px) - cos(2.3 * t + px)) / 2.3;
        x.z += p.shape.z * (sin(1.7 * t + pz) - sin(pz)) / 1.7;
        angle += spin * t;
        flipAngle += p.turn.z * t;
    } else {
        x = p.velLife.xyz * t;
        radius += growth * t;
    }
    vec3 pos = p.posBirth.xyz + x;
    if (p.swirl.w != 0.0) {
        vec2 r = pos.xz - p.swirl.xz;
        float a = p.swirl.w * t / max(length(r), 0.05);
        float c = cos(a), s = sin(a);
        pos.xz = p.swirl.xz + vec2(r.x * c - r.y * s, r.x * s + r.y * c);
        v.xz = vec2(v.x * c - v.z * s, v.x * s + v.z * c);
    }
    if (kind == kSpark && pos.y <= p.shape.w + 0.02) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // landed: rain hitting the ground is gone
        return;
    }
    if (kind == kFlake) pos.y = max(pos.y, p.shape.w + 1e-3);

    // How it looks at this point of its life: the CPU's ParticleEffects::build().
    float u = clamp(t / life, 0.0, 1.0);
    float fade = 1.0;
    vec3 color = p.colorOpacity.rgb;
    vec2 extra = vec2(0.0);
    if (kind == kSmoke) {
        fade = min(1.0, u * 8.0) * (1.0 - u) * (1.0 - u);
    } else if (kind == kSpark) {
        fade = 1.0 - u * u;
        color *= 1.0 - 0.7 * u;
    } else if (kind == kGlow) {
        fade = min(1.0, u * 12.0) * (1.0 - u);
    } else if (kind == kFlake) {
        fade = 1.0 - max(0.0, u - 0.75) * 4.0;
        extra = vec2(max(0.08, abs(cos(flipAngle))), p.colorEnd.a);
    } else {
        fade = (1.0 - u) * (1.0 - u);
        extra = vec2(p.colorEnd.a > 0.5 ? -1.0 : -2.0, p.shape.y);
    }
    if (p.colorEnd.x >= 0.0) color = mix(color, p.colorEnd.rgb, u) * (kind == kSpark ? 1.0 - 0.7 * u : 1.0);
    vec3 streak = kind == kSpark ? v * p.shape.y + vec3(0.0, 1e-3, 0.0) : vec3(0.0);
    effectQuad(pos, color, vec4(radius, p.colorOpacity.a * fade, angle, p.turn.w), streak, extra, kCorners[gl_VertexIndex]);
}
