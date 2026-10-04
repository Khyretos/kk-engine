// Spray, wakes, cannon smoke, fire and splinters (README.md "Effects").
// All kke::ParticleEffects (camera-facing puffs lit by the sun and the sky,
// sparks as hot streaks): foam is white smoke lying on the water, spray is
// white smoke thrown up that falls back, a broadside is a wall of white
// gun smoke drifting downwind. Splinters are little floating bodies.

#include "SeaDemoModule.h"

#include <algorithm>
#include <cmath>

namespace kke_sea {

namespace {
constexpr size_t kMaxSplinters = 160;
const glm::vec3 kFoam(0.95f, 0.97f, 1.0f);
} // namespace

glm::vec3 SeaDemoModule::wind() const {
    // Where the wind blows toward (as the waves travel), m/s.
    return glm::vec3(std::cos(m_windDir), 0.0f, std::sin(m_windDir)) * m_windSpeed;
}

// Something dropped in: a ring of spray, more the harder it hit.
void SeaDemoModule::splash(const glm::vec3& at, float strength) {
    if (!m_fx) return;
    const int n = std::min(40, static_cast<int>(strength * 5.0f));
    for (int i = 0; i < n; ++i) {
        const float a = random01() * 6.2831853f, r = 0.4f + random01() * 0.6f;
        kke::ParticleEffects::Particle p;
        p.position = at + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 0.3f;
        p.velocity = glm::vec3(std::cos(a) * r, 1.0f, std::sin(a) * r) * (1.5f + random01() * strength * 0.7f);
        p.color = kFoam;
        p.radius = 0.12f + random01() * 0.1f;
        p.growth = 0.4f;
        p.opacity = 0.7f;
        p.life = 1.1f;
        p.drag = 0.6f;
        p.rise = -9.0f; // water falls
        p.seed = random01();
        m_fx->emit(p);
    }
}

// A cannonball in the sea: a tall white column and a ring of foam.
void SeaDemoModule::bigSplash(const glm::vec3& at) {
    if (!m_fx) return;
    for (int i = 0; i < 46; ++i) {
        const float a = random01() * 6.2831853f, r = random01() * 0.35f;
        kke::ParticleEffects::Particle p;
        p.position = at + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
        p.velocity = glm::vec3(std::cos(a) * r * 4.0f, 7.0f + random01() * 9.0f, std::sin(a) * r * 4.0f);
        p.color = kFoam;
        p.radius = 0.35f + random01() * 0.35f;
        p.growth = 0.9f;
        p.opacity = 0.8f;
        p.life = 1.8f + random01() * 0.6f;
        p.drag = 0.5f;
        p.rise = -9.0f;
        p.seed = random01();
        m_fx->emit(p);
    }
    for (int i = 0; i < 14; ++i) {
        const float a = static_cast<float>(i) / 14.0f * 6.2831853f;
        kke::ParticleEffects::Particle p;
        p.position = at + glm::vec3(std::cos(a), 0.1f, std::sin(a)) * 0.8f;
        p.velocity = glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 2.5f;
        p.color = kFoam;
        p.radius = 0.6f;
        p.growth = 0.7f;
        p.opacity = 0.55f;
        p.life = 3.5f;
        p.drag = 1.5f;
        p.rise = 0.0f;
        p.seed = random01();
        m_fx->emit(p);
    }
}

// A gun going off: a flash, sparks, and a big puff of white powder smoke
// shot out of the port that hangs and drifts with the wind.
void SeaDemoModule::muzzle(const glm::vec3& at, const glm::vec3& dir, const glm::vec3& shipVel) {
    if (!m_fx) return;
    m_fx->sparks(at, dir, 16, 18.0f, shipVel);
    for (int i = 0; i < 3; ++i) {
        kke::ParticleEffects::Particle f;
        f.kind = kke::ParticleEffects::Kind::Spark;
        f.position = at + dir * 0.6f;
        f.velocity = dir * (25.0f + random01() * 10.0f) + shipVel;
        f.color = glm::vec3(9.0f, 4.5f, 1.4f);
        f.life = 0.12f;
        f.stretch = 0.05f;
        f.radius = 0.25f;
        m_fx->emit(f);
    }
    for (int i = 0; i < 9; ++i) {
        kke::ParticleEffects::Particle s;
        s.position = at + dir * (0.5f + random01() * 1.5f);
        s.velocity = dir * (6.0f + random01() * 9.0f) + shipVel * 0.6f + glm::vec3(random01() - 0.5f, random01() * 0.6f, random01() - 0.5f) * 1.5f;
        s.color = glm::vec3(0.86f + random01() * 0.08f);
        s.radius = 0.6f + random01() * 0.5f;
        s.growth = 0.9f;
        s.opacity = 0.75f;
        s.life = 5.0f + random01() * 3.0f;
        s.drag = 1.6f;
        s.rise = 0.15f;
        s.spin = random01() - 0.5f;
        s.seed = random01();
        m_fx->emit(s);
    }
}

// Splinters off a hit: planks that fly, fall and float for a while.
void SeaDemoModule::splinters(const glm::vec3& at, const glm::vec3& dir, int count) {
    size_t have = 0;
    for (const Floater& f : m_floaters) have += f.bit == Bit::Splinter ? 1u : 0u;
    for (int i = 0; i < count; ++i) {
        if (have >= kMaxSplinters) {
            for (size_t k = 0; k < m_floaters.size(); ++k)
                if (m_floaters[k].bit == Bit::Splinter) {
                    m_bodies.remove(m_floaters[k].body);
                    m_floaters.erase(m_floaters.begin() + static_cast<std::ptrdiff_t>(k));
                    break;
                }
        } else {
            ++have;
        }
        const glm::vec3 half(0.05f + random01() * 0.05f, 0.03f + random01() * 0.02f, 0.2f + random01() * 0.45f);
        Floater f;
        f.bit = Bit::Splinter;
        f.life = 18.0f + random01() * 10.0f;
        f.body = m_bodies.add(half, 450.0f, at - dir * 0.3f,
                              glm::angleAxis(random01() * 6.28f, glm::normalize(glm::vec3(random01() - 0.5f, random01(), random01() - 0.5f))));
        kke::FloatingBody& b = m_bodies.bodies()[f.body];
        b.pushesSmallBits = false;
        const glm::vec3 r(random01() - 0.5f, random01() * 0.8f + 0.3f, random01() - 0.5f);
        // Most fly back out the way the ball came in, some through.
        b.velocity = (random01() < 0.7f ? -dir : dir) * (3.0f + random01() * 6.0f) + r * 6.0f;
        b.angularVelocity = glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) * 14.0f;
        m_floaters.push_back(std::move(f));
    }
}

// Foam along the hull and in the wake, spray off the bow when it slams a
// wave, all scaled by speed.
void SeaDemoModule::wake(const Ship& s, float dt) {
    if (!m_fx || s.sinking) return;
    const ShipArt& art = m_library.art(s.cls);
    const glm::mat3 R = glm::mat3_cast(s.drawRot);
    const glm::vec3 fwd = R * glm::vec3(0, 0, 1), side = R * glm::vec3(1, 0, 0);
    glm::vec3 vel(0.0f);
    if (!s.remote) vel = m_bodies.bodies()[s.body].velocity;
    else vel = fwd * (s.sailShown * shipClasses()[static_cast<size_t>(s.cls)].topSpeed);
    const float speed = glm::length(glm::vec2(vel.x, vel.z));
    if (speed < 0.6f) return;
    const float L = art.hullHalf.z, B = art.hullHalf.x;
    const glm::vec3 centre = s.drawPos;
    const float rate = speed * (2.5f + L * 0.25f); // puffs per second
    float n = rate * dt;
    while (n > 0.0f) {
        if (random01() > n) break;
        n -= 1.0f;
        // Along the sides, from the bow back, and spreading behind the stern.
        const float t = random01();
        const float sgn = random01() < 0.5f ? -1.0f : 1.0f;
        glm::vec3 p = centre + fwd * (L * (1.0f - 2.0f * t)) + side * (sgn * B * (1.0f - 0.3f * t));
        p.y = m_waves.height({ p.x, p.z }, m_time) + 0.05f;
        kke::ParticleEffects::Particle f;
        f.position = p;
        f.velocity = side * (sgn * (0.8f + speed * 0.15f)) - fwd * (speed * 0.15f);
        f.color = kFoam;
        f.radius = 0.35f + B * 0.08f;
        f.growth = 0.5f + speed * 0.05f;
        f.opacity = 0.45f;
        f.life = 3.5f + random01() * 2.5f;
        f.drag = 0.9f;
        f.rise = 0.0f;
        f.seed = random01();
        m_fx->emit(f);
    }
    // The bow wave: spray when the bow digs into the water.
    const glm::vec3 bow = centre + fwd * (L * 1.05f);
    const float water = m_waves.height({ bow.x, bow.z }, m_time);
    const float dig = water - (bow.y - art.hullHalf.y * 0.6f);
    if (dig > 0.0f && speed > 2.0f && random01() < dt * speed * 4.0f) {
        for (int i = 0; i < 6; ++i) {
            const float sgn = random01() < 0.5f ? -1.0f : 1.0f;
            kke::ParticleEffects::Particle p;
            p.position = glm::vec3(bow.x, water, bow.z) + side * (sgn * B * 0.4f);
            p.velocity = side * (sgn * (1.5f + random01() * speed * 0.4f)) + fwd * speed * 0.6f + glm::vec3(0.0f, 2.0f + random01() * speed * 0.5f, 0.0f);
            p.color = kFoam;
            p.radius = 0.2f + random01() * 0.2f;
            p.growth = 0.6f;
            p.opacity = 0.7f;
            p.life = 1.2f;
            p.drag = 0.4f;
            p.rise = -9.0f;
            p.seed = random01();
            m_fx->emit(p);
        }
    }
}

// Where it burns: flames (hot sparks rising), embers, black smoke.
void SeaDemoModule::burn(Ship& s, float dt) {
    if (!m_fx || s.fires.empty()) return;
    s.fireTimer -= dt;
    if (s.fireTimer > 0.0f) return;
    s.fireTimer = 0.06f;
    const glm::mat4 m = shipMatrix(s);
    for (const glm::vec3& at : s.fires) {
        const glm::vec3 p = glm::vec3(m * glm::vec4(at, 1.0f));
        if (p.y < m_waves.height({ p.x, p.z }, m_time)) continue; // under water: out
        kke::ParticleEffects::Particle f;
        f.kind = kke::ParticleEffects::Kind::Spark;
        f.position = p + glm::vec3(random01() - 0.5f, 0.0f, random01() - 0.5f) * 0.8f;
        f.velocity = glm::vec3((random01() - 0.5f) * 0.6f, 2.0f + random01() * 2.0f, (random01() - 0.5f) * 0.6f);
        f.color = glm::vec3(6.0f, 2.4f, 0.5f);
        f.life = 0.5f + random01() * 0.4f;
        f.stretch = 0.12f;
        f.radius = 0.35f;
        m_fx->emit(f);
        kke::ParticleEffects::Particle sm;
        sm.position = p + glm::vec3(0.0f, 0.8f, 0.0f);
        sm.velocity = glm::vec3(0.0f, 1.2f, 0.0f);
        sm.color = glm::vec3(0.1f + random01() * 0.06f);
        sm.radius = 0.6f;
        sm.growth = 0.9f;
        sm.opacity = 0.6f;
        sm.life = 5.0f;
        sm.rise = 1.2f;
        sm.drag = 0.6f;
        sm.seed = random01();
        m_fx->emit(sm);
    }
}

// Splinters and broken masts fade out after a while (and sink first).
void SeaDemoModule::stepDebris(float dt) {
    for (size_t i = 0; i < m_floaters.size();) {
        Floater& f = m_floaters[i];
        if (f.bit == Bit::Thrown) {
            ++i;
            continue;
        }
        f.life -= dt;
        kke::FloatingBody& b = m_bodies.bodies()[f.body];
        if (f.life < 3.0f) b.mass *= 1.0f + dt * 1.5f; // waterlogged: down it goes
        if (f.life <= 0.0f || !b.alive) {
            for (kke::ModelModule::InstanceId id : f.instances) m_models->remove(id);
            m_bodies.remove(f.body);
            m_floaters.erase(m_floaters.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

} // namespace kke_sea
