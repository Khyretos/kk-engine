// GPU particles that need a step each frame (ENG-F11, kke::ParticleEffects;
// the CPU copy and the tests' reference is engine/src/ParticleStep.cpp):
// pulled to their emitter, carried by turbulence, or bouncing off whatever
// was on screen last frame (its depth). Each particle's record was written
// once at birth (shaders/effect_gpu.vert reads the same); its state here
// is where it is now. A slot whose record is newer than its state starts
// over from the record. #include after #version and the DEPTH_MS define.
layout(local_size_x = 64) in;

struct GpuParticle {
    vec4 posBirth;     // xyz = where it was born, w = birth time (s)
    vec4 velLife;      // xyz = velocity at birth, w = life (s)
    vec4 colorOpacity;
    vec4 colorEnd;
    vec4 motion;       // drag, rise, growth, spin
    vec4 shape;        // radius, stretch or thickness, flutter, floor
    vec4 turn;         // angle, flip angle, flip, seed
    vec4 swirl;        // xyz = origin, w = vortex
    vec4 step;         // attract, turbulence, turbulence size, flags (1 = stepped, 2 = collides)
};
struct Stepped {
    vec4 posBirth; // xyz = where it is now, w = the birth time this state belongs to
    vec4 velRest;  // xyz = velocity, w = 1 when it came to rest on something
};
layout(std430, set = 0, binding = 0) readonly buffer Records { GpuParticle records[]; };
layout(std430, set = 0, binding = 1) buffer States { Stepped states[]; };
#ifdef DEPTH_MS
layout(set = 0, binding = 2) uniform sampler2DMS lastDepth;
float depthAt(ivec2 p) { return texelFetch(lastDepth, p, 0).r; }
#else
layout(set = 0, binding = 2) uniform sampler2D lastDepth;
float depthAt(ivec2 p) { return texelFetch(lastDepth, p, 0).r; }
#endif
layout(set = 0, binding = 3) uniform Frame {
    mat4 viewProj;    // last frame's camera (the depth's)
    mat4 invViewProj;
    vec4 timeStep;    // x = the sim's time, y = seconds since the last step, z = 1 when the depth is there
    vec4 wind;        // xyz
    vec4 screen;      // xy = the depth's size
} frame;
layout(push_constant) uniform Push {
    vec4 kindCount; // x = kke::ParticleSim::Kind, y = slots
} pc;

const int kSmoke = 0;
const int kSpark = 1;
const int kGlow = 2;
const int kFlake = 3;

// kke::particleTurbulence: four travelling waves of curl, divergence free.
const vec3 kWaveK[4] = vec3[4](vec3(0.8, 0.6, 0.0), vec3(0.0, 0.8, 0.6), vec3(0.6, 0.0, -0.8), vec3(-0.48, 0.6, 0.64));
const vec3 kWaveA[4] = vec3[4](vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.6, 0.0, 0.8));
const float kWaveSpeed[4] = float[4](0.31, 0.47, 0.23, 0.39);
vec3 turbulence(vec3 position, float size, float time) {
    vec3 x = position / max(size, 0.01);
    vec3 u = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float phase = 6.2831853 * (dot(kWaveK[i], x) + kWaveSpeed[i] * time) + 1.7 * float(i);
        u += cos(phase) * cross(kWaveK[i], kWaveA[i]);
    }
    return u * 0.25;
}

vec3 worldAt(ivec2 p) {
    vec2 ndc = (vec2(p) + 0.5) / frame.screen.xy * 2.0 - 1.0;
    vec4 w = frame.invViewProj * vec4(ndc, depthAt(p), 1.0);
    return w.xyz / w.w;
}

// kke::particleBounce
void bounce(int kind, inout vec3 pos, inout vec3 vel, inout float rest, vec3 surface, vec3 n) {
    pos = surface + n * 0.01;
    float into = dot(vel, n);
    if (into >= 0.0) return;
    if (kind == kFlake && n.y > 0.5) {
        vel = vec3(0.0);
        rest = 1.0;
        return;
    }
    vec3 along = vel - n * into;
    bool bounces = kind == kSpark || kind == kGlow || kind == kFlake;
    vel = along * (bounces ? 0.6 : 1.0) - n * into * (bounces ? 0.35 : 0.0);
}

// Behind what was on screen at this point last frame, and near it: bounce off it, its normal from the
// depth around it (turned toward where the particle came from).
void collide(int kind, vec3 from, inout vec3 pos, inout vec3 vel, inout float rest) {
    vec4 clip = frame.viewProj * vec4(pos, 1.0);
    if (clip.w <= 0.0) return;
    vec3 ndc = clip.xyz / clip.w;
    if (any(greaterThan(abs(ndc.xy), vec2(0.999)))) return;
    ivec2 p = ivec2((ndc.xy * 0.5 + 0.5) * frame.screen.xy);
    if (ndc.z <= depthAt(p)) return; // in front of the surface there
    vec3 surface = worldAt(p);
    float thick = 0.25 + length(pos - from) * 2.0;
    if (distance(pos, surface) > thick) return; // far behind it: hidden, not hitting it
    ivec2 last = ivec2(frame.screen.xy) - 1;
    vec3 n = cross(worldAt(min(p + ivec2(1, 0), last)) - surface, worldAt(min(p + ivec2(0, 1), last)) - surface);
    if (dot(n, n) < 1e-12) return;
    n = normalize(n);
    if (dot(n, from - surface) < 0.0) n = -n;
    bounce(kind, pos, vel, rest, surface, n);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(pc.kindCount.y)) return;
    GpuParticle r = records[i];
    int flags = int(r.step.w + 0.5);
    float time = frame.timeStep.x;
    if ((flags & 1) == 0 || time - r.posBirth.w >= r.velLife.w || time < r.posBirth.w) return; // closed form, dead, or not born
    Stepped s = states[i];
    float dt = frame.timeStep.y;
    if (s.posBirth.w != r.posBirth.w) {
        s.posBirth = r.posBirth; // born since the last step: starts from its record
        s.velRest = vec4(r.velLife.xyz, 0.0);
        dt = time - r.posBirth.w;
    }
    int kind = int(pc.kindCount.x + 0.5);
    vec3 pos = s.posBirth.xyz, vel = s.velRest.xyz, from = pos;
    float rest = s.velRest.w;
    float age = time - r.posBirth.w;
    float drag = r.motion.x, rise = r.motion.y, flutter = r.shape.z, floorY = r.shape.w, seed = r.turn.w;
    vec3 wind = frame.wind.xyz;
    if (dt > 0.0 && rest < 0.5) {
        // kke::particleStep
        if (kind == kSmoke || kind == kGlow) {
            vel += (wind - vel) * min(1.0, drag * dt);
            vel.y += rise * dt;
        } else if (kind == kFlake) {
            if (!(pos.y <= floorY + 1e-3 && vel.y <= 0.0)) {
                vec3 air = wind + vec3(sin(age * 2.3 + seed * 6.2831853), 0.0, cos(age * 1.7 + seed * 9.1)) * flutter;
                vel.x += (air.x - vel.x) * min(1.0, drag * dt);
                vel.z += (air.z - vel.z) * min(1.0, drag * dt);
                vel.y *= max(0.0, 1.0 - drag * 0.5 * dt);
                vel.y += rise * dt;
            }
        } else if (kind == kSpark) {
            vel *= max(0.0, 1.0 - drag * dt);
            vel.y += rise * dt;
        }
        bool onFloor = kind == kFlake && vel == vec3(0.0) && pos.y <= floorY + 1e-3;
        if (r.step.y != 0.0 && !onFloor) vel += r.step.y * turbulence(pos, r.step.z, time) * min(1.0, max(drag, 0.5) * dt);
        if (r.step.x != 0.0) {
            vec3 to = r.swirl.xyz - pos;
            float d = length(to);
            if (d > 1e-3) vel += to / d * r.step.x * dt;
        }
        pos += vel * dt;
        if (r.swirl.w != 0.0) {
            vec2 rr = pos.xz - r.swirl.xz;
            float a = r.swirl.w * dt / max(length(rr), 0.05);
            float c = cos(a), sn = sin(a);
            pos.xz = r.swirl.xz + vec2(rr.x * c - rr.y * sn, rr.x * sn + rr.y * c);
            vel.xz = vec2(vel.x * c - vel.z * sn, vel.x * sn + vel.z * c);
        }
        if (kind == kSpark && vel.y < 0.0 && age > 0.05 && pos.y < floorY + 0.02) {
            pos.y = floorY + 0.02;
            vel.y *= -0.35;
            vel.xz *= 0.6;
        } else if (kind == kFlake && vel.y < 0.0 && pos.y < floorY + 1e-3) {
            pos.y = floorY + 1e-3;
            vel = vec3(0.0);
        }
        if ((flags & 2) != 0 && frame.timeStep.z > 0.5) collide(kind, from, pos, vel, rest);
    }
    states[i] = Stepped(vec4(pos, s.posBirth.w), vec4(vel, rest));
}
