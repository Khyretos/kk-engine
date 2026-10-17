#include "kke/ParticleSim.h"

#include <algorithm>
#include <cmath>

namespace kke {

ParticleSim::ParticleSim(size_t maxParticles) : m_max(std::max<size_t>(16, maxParticles)) { m_particles.reserve(m_max); }

float ParticleSim::random01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xffffffu) / static_cast<float>(0x1000000);
}

uint16_t ParticleSim::addCurve(const OverLife& curve) {
    m_curves.push_back(curve);
    return static_cast<uint16_t>(m_curves.size());
}

const ParticleSim::OverLife* ParticleSim::curve(uint16_t index) const {
    return index >= 1 && index <= m_curves.size() ? &m_curves[index - 1] : nullptr;
}

float ParticleSim::sample(const std::vector<float>& keys, float t, float fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0];
    const float x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(keys.size() - 1);
    const size_t i = std::min(static_cast<size_t>(x), keys.size() - 2);
    return keys[i] + (keys[i + 1] - keys[i]) * (x - static_cast<float>(i));
}

glm::vec3 ParticleSim::sample(const std::vector<glm::vec3>& keys, float t, const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0];
    const float x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(keys.size() - 1);
    const size_t i = std::min(static_cast<size_t>(x), keys.size() - 2);
    return glm::mix(keys[i], keys[i + 1], x - static_cast<float>(i));
}

void ParticleSim::setCollider(RayFn ray, int raysPerFrame) {
    m_ray = std::move(ray);
    m_raysPerFrame = std::max(0, raysPerFrame);
}

void ParticleSim::emit(const Particle& p) {
    if (m_particles.size() >= m_max) {
        // Full: the oldest makes room (smoke that's nearly gone anyway).
        auto oldest = std::max_element(m_particles.begin(), m_particles.end(),
                                       [](const Particle& a, const Particle& b) { return a.age / a.life < b.age / b.life; });
        *oldest = p;
        return;
    }
    m_particles.push_back(p);
}

void ParticleSim::emitGpu(const Particle& p) {
    if (!m_gpuBorn) {
        emit(p);
        return;
    }
    if (m_born.size() >= kMaxBorn) return; // nothing drew them for a long while (a covered scene): they'd be gone anyway
    m_born.push_back({ p, m_time });
}

void ParticleSim::takeBorn(std::vector<Born>& out) {
    out.clear();
    out.swap(m_born);
}

void ParticleSim::smoke(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float radius, float life, float opacity) {
    Particle p;
    p.kind = Kind::Smoke;
    p.position = position;
    const glm::vec3 jitter(random01() - 0.5f, random01() * 0.5f, random01() - 0.5f);
    p.velocity = velocity + jitter * 0.8f;
    p.color = color * (0.92f + 0.08f * random01());
    p.radius = radius * (0.8f + 0.4f * random01());
    p.growth = radius * (1.4f + random01());
    p.opacity = opacity;
    p.life = life * (0.8f + 0.4f * random01());
    p.spin = (random01() - 0.5f) * 1.2f;
    p.angle = random01() * 6.2831853f;
    p.seed = random01();
    emit(p);
}

void ParticleSim::sparks(const glm::vec3& position, const glm::vec3& normal, int count, float speed, const glm::vec3& along) {
    const glm::vec3 n = glm::length(normal) > 1e-4f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind = Kind::Spark;
        p.position = position;
        // A cone around the normal, dragged along the scrape.
        glm::vec3 d(random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f);
        d = glm::normalize(n * 1.2f + d * 0.9f + glm::vec3(1e-4f));
        p.velocity = d * speed * (0.4f + 0.8f * random01()) + along;
        const float heat = 0.6f + 0.4f * random01();
        p.color = glm::vec3(9.0f, 4.2f, 1.2f) * heat;
        p.radius = 0.012f + 0.01f * random01();
        p.growth = 0.0f;
        p.opacity = 1.0f;
        p.life = 0.25f + 0.45f * random01();
        p.drag = 0.6f;
        p.rise = -9.81f;
        p.stretch = 0.035f;
        emit(p);
    }
}

void ParticleSim::update(float dt, const glm::vec3& wind) {
    m_deaths.clear();
    if (dt <= 0.0f) return;
    m_time += dt;
    m_wind = wind;
    std::vector<glm::vec3> before;
    if (m_ray) before.resize(m_particles.size());
    for (size_t i = 0; i < m_particles.size(); ++i) {
        Particle& p = m_particles[i];
        if (m_ray) before[i] = p.position;
        p.age += dt;
        switch (p.kind) {
        case Kind::Smoke:
        case Kind::Glow:
            // Drifts toward the wind's speed, rises while hot, spreads.
            p.velocity += (wind - p.velocity) * std::min(1.0f, p.drag * dt);
            p.velocity.y += p.rise * dt;
            p.radius += p.growth * dt;
            p.growth *= std::max(0.0f, 1.0f - 0.6f * dt);
            p.angle += p.spin * dt;
            break;
        case Kind::Flake: {
            if (p.position.y <= p.floor + 1e-3f && p.velocity.y <= 0.0f) break; // resting on the ground: stays, fades
            // Falls at its own pace, carried by the wind, swaying.
            const glm::vec3 air = wind + glm::vec3(std::sin(p.age * 2.3f + p.seed * 6.2831853f), 0.0f,
                                                   std::cos(p.age * 1.7f + p.seed * 9.1f)) * p.flutter;
            p.velocity.x += (air.x - p.velocity.x) * std::min(1.0f, p.drag * dt);
            p.velocity.z += (air.z - p.velocity.z) * std::min(1.0f, p.drag * dt);
            p.velocity.y *= std::max(0.0f, 1.0f - p.drag * 0.5f * dt);
            p.velocity.y += p.rise * dt;
            p.angle += p.spin * dt;
            p.flipAngle += p.flip * dt;
            break;
        }
        case Kind::Ring:
            p.radius += p.growth * dt;
            break;
        case Kind::Spark:
            p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
            p.velocity.y += p.rise * dt;
            break;
        }
        if (p.attract != 0.0f) {
            const glm::vec3 to = p.origin - p.position;
            const float d = glm::length(to);
            if (d > 1e-3f) p.velocity += to / d * p.attract * dt;
        }
        p.position += p.velocity * dt;
        if (p.vortex != 0.0f) {
            // Turned round the vertical line through origin: the radius stays,
            // the velocity turns with it (a swirl, not a spiral outwards).
            const glm::vec2 r(p.position.x - p.origin.x, p.position.z - p.origin.z);
            const float a = p.vortex * dt / std::max(glm::length(r), 0.05f);
            const float c = std::cos(a), s = std::sin(a);
            p.position.x = p.origin.x + r.x * c - r.y * s;
            p.position.z = p.origin.z + r.x * s + r.y * c;
            const glm::vec2 v(p.velocity.x, p.velocity.z);
            p.velocity.x = v.x * c - v.y * s;
            p.velocity.z = v.x * s + v.y * c;
        }
        // Sparks bounce off the ground they were born near and flakes
        // settle on it (cheap: no collision queries).
        if (p.kind == Kind::Spark && p.velocity.y < 0.0f && p.age > 0.05f && p.position.y < p.floor + 0.02f) {
            p.position.y = p.floor + 0.02f;
            p.velocity.y *= -0.35f;
            p.velocity.x *= 0.6f;
            p.velocity.z *= 0.6f;
        } else if (p.kind == Kind::Flake && p.velocity.y < 0.0f && p.position.y < p.floor + 1e-3f) {
            p.position.y = p.floor + 1e-3f;
            p.velocity = glm::vec3(0.0f);
            p.flipAngle = 0.0f; // lies flat: drawn face-on
            p.flip = 0.0f;
            p.spin = 0.0f;
        }
    }
    if (m_ray && !m_particles.empty()) {
        // One ray per colliding particle from where it was to where it is,
        // at most m_raysPerFrame; the next frame carries on after the last.
        int budget = m_raysPerFrame;
        const size_t n = m_particles.size();
        size_t k = 0;
        for (; k < n && budget > 0; ++k) {
            const size_t i = (m_rayCursor + k) % n;
            Particle& p = m_particles[i];
            if (!p.collide || p.position == before[i]) continue;
            --budget;
            collide(p, before[i]);
        }
        m_rayCursor = (m_rayCursor + k) % n;
    }
    for (const Particle& p : m_particles)
        if (p.onDeath && p.age >= p.life) m_deaths.push_back({ p.position, p.velocity, p.floor, p.onDeath });
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.age >= p.life; }),
                      m_particles.end());
}

void ParticleSim::collide(Particle& p, const glm::vec3& from) {
    glm::vec3 hit, normal;
    if (!m_ray(from, p.position, hit, normal)) return;
    const float len = glm::length(normal);
    normal = len > 1e-6f ? normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    p.position = hit + normal * 0.01f;
    const float into = glm::dot(p.velocity, normal);
    if (into >= 0.0f) return;
    if (p.kind == Kind::Flake && normal.y > 0.5f) {
        // Settles where it landed (a ledge, a roof) like on its floor.
        p.floor = p.position.y - 1e-3f;
        p.velocity = glm::vec3(0.0f);
        p.flipAngle = 0.0f;
        p.flip = 0.0f;
        p.spin = 0.0f;
        return;
    }
    const glm::vec3 along = p.velocity - normal * into;
    const bool bounces = p.kind == Kind::Spark || p.kind == Kind::Glow || p.kind == Kind::Flake;
    p.velocity = along * (bounces ? 0.6f : 1.0f) - normal * into * (bounces ? 0.35f : 0.0f); // smoke slides along
}

void ParticleSim::clear() {
    m_particles.clear();
    m_deaths.clear();
    m_born.clear();
}

} // namespace kke
