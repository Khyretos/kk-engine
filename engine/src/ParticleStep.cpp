#include "kke/ParticleStep.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

using Kind = ParticleSim::Kind;

// The four waves (shaders/effect_step.comp's kWaves): a unit direction, the
// vector its curl turns around, and how fast it travels (per second).
struct Wave {
    glm::vec3 k, a;
    float speed;
};
const Wave kWaves[4] = {
    { glm::vec3(0.8f, 0.6f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 0.31f },
    { glm::vec3(0.0f, 0.8f, 0.6f), glm::vec3(1.0f, 0.0f, 0.0f), 0.47f },
    { glm::vec3(0.6f, 0.0f, -0.8f), glm::vec3(0.0f, 1.0f, 0.0f), 0.23f },
    { glm::vec3(-0.48f, 0.6f, 0.64f), glm::vec3(0.6f, 0.0f, 0.8f), 0.39f },
};

} // namespace

glm::vec3 particleTurbulence(const glm::vec3& position, float size, float time) {
    const glm::vec3 x = position / std::max(size, 0.01f);
    glm::vec3 u(0.0f);
    for (int i = 0; i < 4; ++i) {
        const Wave& w = kWaves[i];
        const float phase = 6.2831853f * (glm::dot(w.k, x) + w.speed * time) + 1.7f * static_cast<float>(i);
        u += std::cos(phase) * glm::cross(w.k, w.a);
    }
    return u * 0.25f;
}

void particleStep(StepParticle& p, const StepParams& s, float dt, const glm::vec3& wind, float time) {
    if (dt <= 0.0f) return;
    p.age += dt;
    switch (s.kind) {
    case Kind::Smoke:
    case Kind::Glow:
        p.velocity += (wind - p.velocity) * std::min(1.0f, s.drag * dt);
        p.velocity.y += s.rise * dt;
        break;
    case Kind::Flake: {
        if (p.resting || (p.position.y <= s.floor + 1e-3f && p.velocity.y <= 0.0f)) break;
        const glm::vec3 air = wind + glm::vec3(std::sin(p.age * 2.3f + s.seed * 6.2831853f), 0.0f, std::cos(p.age * 1.7f + s.seed * 9.1f)) * s.flutter;
        p.velocity.x += (air.x - p.velocity.x) * std::min(1.0f, s.drag * dt);
        p.velocity.z += (air.z - p.velocity.z) * std::min(1.0f, s.drag * dt);
        p.velocity.y *= std::max(0.0f, 1.0f - s.drag * 0.5f * dt);
        p.velocity.y += s.rise * dt;
        break;
    }
    case Kind::Ring:
        break;
    case Kind::Spark:
        p.velocity *= std::max(0.0f, 1.0f - s.drag * dt);
        p.velocity.y += s.rise * dt;
        break;
    }
    if (p.resting) return; // landed on something on screen (only the GPU's depth collisions do that)
    const bool onFloor = s.kind == Kind::Flake && p.velocity == glm::vec3(0.0f) && p.position.y <= s.floor + 1e-3f;
    if (s.turbulence != 0.0f && !onFloor)
        p.velocity += s.turbulence * particleTurbulence(p.position, s.turbulenceSize, time) * std::min(1.0f, std::max(s.drag, 0.5f) * dt);
    if (s.attract != 0.0f) {
        const glm::vec3 to = s.origin - p.position;
        const float d = glm::length(to);
        if (d > 1e-3f) p.velocity += to / d * s.attract * dt;
    }
    p.position += p.velocity * dt;
    if (s.vortex != 0.0f) {
        const glm::vec2 r(p.position.x - s.origin.x, p.position.z - s.origin.z);
        const float a = s.vortex * dt / std::max(glm::length(r), 0.05f);
        const float c = std::cos(a), si = std::sin(a);
        p.position.x = s.origin.x + r.x * c - r.y * si;
        p.position.z = s.origin.z + r.x * si + r.y * c;
        const glm::vec2 v(p.velocity.x, p.velocity.z);
        p.velocity.x = v.x * c - v.y * si;
        p.velocity.z = v.x * si + v.y * c;
    }
    if (s.kind == Kind::Spark && p.velocity.y < 0.0f && p.age > 0.05f && p.position.y < s.floor + 0.02f) {
        p.position.y = s.floor + 0.02f;
        p.velocity.y *= -0.35f;
        p.velocity.x *= 0.6f;
        p.velocity.z *= 0.6f;
    } else if (s.kind == Kind::Flake && p.velocity.y < 0.0f && p.position.y < s.floor + 1e-3f) {
        p.position.y = s.floor + 1e-3f;
        p.velocity = glm::vec3(0.0f);
    }
}

void particleBounce(StepParticle& p, ParticleSim::Kind kind, const glm::vec3& surface, const glm::vec3& normal) {
    p.position = surface + normal * 0.01f;
    const float into = glm::dot(p.velocity, normal);
    if (into >= 0.0f) return;
    if (kind == Kind::Flake && normal.y > 0.5f) {
        p.velocity = glm::vec3(0.0f);
        p.resting = true;
        return;
    }
    const glm::vec3 along = p.velocity - normal * into;
    const bool bounces = kind == Kind::Spark || kind == Kind::Glow || kind == Kind::Flake;
    p.velocity = along * (bounces ? 0.6f : 1.0f) - normal * into * (bounces ? 0.35f : 0.0f);
}

} // namespace kke
