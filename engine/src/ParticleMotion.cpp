#include "kke/ParticleMotion.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// v' = a - k v from v0: where it is after t (x from 0) and how fast it goes.
// For k near 0 it is plain constant acceleration.
void dragged(float k, const glm::vec3& a, const glm::vec3& v0, float t, glm::vec3& x, glm::vec3& v) {
    if (k < 1e-4f) {
        v = v0 + a * t;
        x = v0 * t + 0.5f * a * t * t;
        return;
    }
    const float e = std::exp(-k * t);
    const glm::vec3 terminal = a / k;
    v = terminal + (v0 - terminal) * e;
    x = terminal * t + (v0 - terminal) * ((1.0f - e) / k);
}

} // namespace

ParticleMotion particleMotion(const ParticleSim::Particle& p, float t, const glm::vec3& wind) {
    using Kind = ParticleSim::Kind;
    ParticleMotion m;
    m.radius = p.radius;
    m.angle = p.angle;
    m.flipAngle = p.flipAngle;
    glm::vec3 x(0.0f);
    switch (p.kind) {
    case Kind::Smoke:
    case Kind::Glow:
        // Drifts toward the wind and rises; growth eases off at 0.6 a second.
        dragged(p.drag, p.drag * wind + glm::vec3(0.0f, p.rise, 0.0f), p.velocity, t, x, m.velocity);
        m.radius = p.radius + p.growth * (1.0f - std::exp(-0.6f * t)) / 0.6f;
        m.angle += p.spin * t;
        break;
    case Kind::Spark:
        dragged(p.drag, glm::vec3(0.0f, p.rise, 0.0f), p.velocity, t, x, m.velocity);
        break;
    case Kind::Flake: {
        // Across: toward the wind; down: half the drag and its own rise.
        glm::vec3 xa, va, xb, vb;
        dragged(p.drag, p.drag * glm::vec3(wind.x, 0.0f, wind.z), glm::vec3(p.velocity.x, 0.0f, p.velocity.z), t, xa, va);
        dragged(0.5f * p.drag, glm::vec3(0.0f, p.rise, 0.0f), glm::vec3(0.0f, p.velocity.y, 0.0f), t, xb, vb);
        x = xa + xb;
        m.velocity = va + vb;
        // The flutter's sway, integrated (ParticleSim: sin(2.3 t + seed 2 pi) across x, cos(1.7 t + seed 9.1) across z).
        const float px = p.seed * 6.2831853f, pz = p.seed * 9.1f;
        x.x += p.flutter * (std::cos(px) - std::cos(2.3f * t + px)) / 2.3f;
        x.z += p.flutter * (std::sin(1.7f * t + pz) - std::sin(pz)) / 1.7f;
        m.angle += p.spin * t;
        m.flipAngle += p.flip * t;
        break;
    }
    case Kind::Ring:
        m.velocity = p.velocity;
        x = p.velocity * t;
        m.radius = p.radius + p.growth * t;
        break;
    }
    m.position = p.position + x;
    if (p.vortex != 0.0f) {
        // Round the vertical line through origin, by the arc the swirl covers at this radius.
        const glm::vec2 r(m.position.x - p.origin.x, m.position.z - p.origin.z);
        const float a = p.vortex * t / std::max(glm::length(r), 0.05f);
        const float c = std::cos(a), s = std::sin(a);
        m.position.x = p.origin.x + r.x * c - r.y * s;
        m.position.z = p.origin.z + r.x * s + r.y * c;
        const glm::vec2 v(m.velocity.x, m.velocity.z);
        m.velocity.x = v.x * c - v.y * s;
        m.velocity.z = v.x * s + v.y * c;
    }
    const float rest = p.kind == Kind::Spark ? p.floor + 0.02f : p.floor + 1e-3f;
    if ((p.kind == Kind::Spark || p.kind == Kind::Flake) && m.position.y <= rest) {
        m.position.y = rest;
        m.landed = true;
    }
    return m;
}

} // namespace kke
