// Damage and effects: hits dent the body where they land and hurt the car
// (engine power, bent wheels, at 0% totalled); metal on metal and on the
// walls sparks; sliding and spinning tyres smoke and leave marks; a hurt
// engine smokes, a wrecked one burns. The pit box mends it all. Drift
// points are here too: they come from sliding, and a hit loses them.

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>


#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace racing {

namespace {

constexpr size_t kMaxSkids = 3000;   // segments of tyre marks (the oldest go)
constexpr size_t kMaxDebris = 80;
constexpr float kDebrisLife = 14.0f; // s a bit of bodywork lies on the track

// A box as 24 vertices (flat-shaded faces).
void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

// Vertex normals again after a dent: each triangle's normal added to its
// corners (area-weighted by the cross product's length).
void recomputeNormals(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& idx, std::vector<glm::vec3>& n) {
    std::vector<glm::vec3> sum(p.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        if (a >= p.size() || b >= p.size() || c >= p.size()) continue;
        const glm::vec3 fn = glm::cross(p[b] - p[a], p[c] - p[a]);
        sum[a] += fn;
        sum[b] += fn;
        sum[c] += fn;
    }
    for (size_t i = 0; i < p.size() && i < n.size(); ++i) {
        const float len = glm::length(sum[i]);
        if (len > 1e-9f) n[i] = sum[i] / len;
    }
}

} // namespace

// The first big crash shouldn't be the one that builds the bits it throws
// and loads its sounds: all of that now, while the grid's being made.
void RacingModule::warmUpCrashes() {
    debrisMesh(glm::vec3(0.06f));
    debrisMesh(glm::vec3(0.55f, 0.65f, 0.7f));
    for (const Car& c : m_cars) debrisMesh(c.color);
    if (!m_audio) return;
    for (uint32_t v = 0; v < 4; ++v)
        for (uint32_t material : { kke::AudioMaterialTable::Metal, kke::AudioMaterialTable::Stone, kke::AudioMaterialTable::Wood, kke::AudioMaterialTable::Dirt })
            for (float level : { 0.1f, 0.3f, 0.55f, 1.0f }) m_audio->impacts().get(material, level, v);
}

int RacingModule::debrisMesh(const glm::vec3& color) {
    for (size_t i = 0; i < m_debrisColors.size(); ++i)
        if (glm::length(m_debrisColors[i] - color) < 0.02f) return static_cast<int>(i);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox(glm::vec3(0.0f), glm::vec3(0.5f), color, v, idx);
    auto mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    mesh->upload(v, idx);
    m_debrisMeshes.push_back(std::move(mesh));
    m_debrisColors.push_back(color);
    return static_cast<int>(m_debrisColors.size()) - 1;
}

void RacingModule::clearEffects() {
    if (m_fx) m_fx->clear();
    clearLooseWheels();
    kke::RigidWorld& w = m_rigid->world();
    for (Debris& d : m_debris) w.remove(d.body);
    m_debris.clear();
    m_skids.clear();
    m_skidHead = 0;
    m_skidsChanged = true;
    for (auto& trails : m_trails)
        for (SkidTrail& t : trails) t.on = false;
}

void RacingModule::handleContacts() {
    if (m_phase == Phase::Lobby) return;
    std::unordered_map<kke::RigidWorld::BodyId, size_t> carOf;
    for (size_t i = 0; i < m_cars.size(); ++i) carOf[m_cars[i].body] = i;
    auto isTrack = [this](kke::RigidWorld::BodyId b) { return std::find(m_trackBodies.begin(), m_trackBodies.end(), b) != m_trackBodies.end(); };
    for (const kke::RigidWorld::Contact& ct : m_rigid->frameContacts()) {
        const auto a = carOf.find(ct.a), b = carOf.find(ct.b);
        const bool carA = a != carOf.end(), carB = b != carOf.end();
        if (!carA && !carB) continue;
        // Debris doesn't hurt; the ground only when a car comes down on its roof.
        const kke::RigidWorld::BodyId other = carA ? ct.b : ct.a;
        const bool byCar = carA && carB;
        if (!byCar && !isTrack(other)) continue;
        if (!byCar && other == m_trackBodies[0] && ct.speed < 6.0f) continue;
        // Jolt's normal points from a to b: a is pushed back along -normal.
        if (carA) hitCar(m_cars[a->second], ct.point, ct.normal, ct.speed, carB ? &m_cars[b->second] : nullptr);
        if (carB) hitCar(m_cars[b->second], ct.point, -ct.normal, ct.speed, carA ? &m_cars[a->second] : nullptr);
    }
}

// `into` points into the car (the way the hit pushed it).
void RacingModule::hitCar(Car& c, const glm::vec3& point, const glm::vec3& into, float speed, Car* by) {
    const bool byCar = by != nullptr;
    if (speed < 2.5f) return;
    // Sparks and a scrape whoever's machine it is; the damage is the owner's.
    m_fx->sparks(point, -into, static_cast<int>(std::min(6.0f + speed * 1.6f, 60.0f)), 3.0f + speed * 0.35f, c.velocity * 0.6f);
    if (c.remote || c.totalled) return;
    if (m_damage == 0 || m_phase != Phase::Racing) return;
    if (c.hitCooldown > 0.0f && speed < 9.0f) return;
    c.hitCooldown = 0.2f;
    // Drift events are about style: a tap on the wall costs the combo, not the car.
    const float factor = (m_damage == 2 ? 2.2f : 1.0f) * (event() == Event::Drift ? 0.35f : 1.0f);
    const glm::mat4 inv = glm::inverse(c.xf);
    const glm::vec3 local = glm::vec3(inv * glm::vec4(point, 1.0f));
    float hurt = std::pow(std::max(0.0f, speed - 3.0f), 1.35f) * 0.85f * factor * (byCar ? 0.8f : 1.0f);
    // The derby: the engine's in the front, so that's where a hit hurts;
    // the boot is a crumple zone (why derby drivers ram backwards).
    if (event() == Event::Derby) {
        const float along = (local.z - c.art->boundsMin.z) / std::max(c.art->boundsMax.z - c.art->boundsMin.z, 0.5f); // 0 rear .. 1 front
        // Derby cars are stripped and braced (glass out, cage in): they take
        // about twice the knocks, so a bout lasts minutes, not seconds.
        hurt *= 0.55f * (along > 0.75f ? 1.4f : along < 0.25f ? 0.6f : 1.0f);
    }
    c.health = std::max(0.0f, c.health - hurt);
    ++c.hits;
    if (event() == Event::Derby) derbyHit(c, by, into, hurt);
    // The dent: deeper the harder, where it landed, the way it was pushed.
    const glm::vec3 dir = glm::normalize(glm::vec3(inv * glm::vec4(into, 0.0f)));
    const float depth = std::clamp((speed - 2.5f) * 0.016f * factor, 0.01f, 0.22f);
    dent(c, local, dir, depth);
    netHit(c, local, dir, depth);
    // A corner hit bends that wheel: less grip there, and the car pulls.
    for (int wh = 0; wh < 4; ++wh)
        if (glm::length(local - c.art->wheelCenter[wh]) < 0.9f && speed > 6.0f) {
            c.bent[wh] = std::min(1.0f, c.bent[wh] + (speed - 6.0f) * 0.025f * factor);
            damageWheel(c, wh, speed * std::sqrt(factor), -into * 2.5f);
        }
    if (speed > 11.0f) spawnDebris(c, point, -into * 2.0f + c.velocity * 0.5f, 2 + static_cast<int>(speed / 10.0f));
    // Drift: a hit ends the combo, points and all.
    if (c.driftChain > 0.0f) {
        c.note = fmt::format("Crashed: {:.0f} points lost", c.driftChain);
        c.noteTime = 2.0f;
        c.driftChain = 0.0f;
        c.driftCombo = 1.0f;
        c.driftHold = 0.0f;
        c.drifting = false;
    }
    applyDamage(c);
    if (c.health <= 0.0f && !c.totalled) {
        c.totalled = true;
        c.note = "TOTALLED";
        c.noteTime = 5.0f;
        c.outAt = m_raceClock;
        kke::log::get(name())->info("{} is totalled ({} hits, the last at {:.0f} km/h)", c.name, c.hits, speed * 3.6f);
        if (c.seat >= 0) tone(static_cast<int>(kke::Earcon::Error), 0.9f);
        if (event() == Event::Derby) knockOut(c, "");
    }
}

// Pushes the body's vertices near the hit in, the most at the point
// itself, fading out over a radius that grows with the depth.
void RacingModule::dent(Car& c, const glm::vec3& localPoint, const glm::vec3& localDir, float depth) {
    if (crumple(c, localPoint, localDir, depth)) return; // FEMFX works it out (Crumple.cpp)
    const CarArt& art = *c.art;
    if (c.dented.empty()) {
        c.dented = art.positions;
        c.dentedNormals = art.normals;
    }
    const float radius = 0.45f + depth * 2.5f;
    const glm::vec3 mid = (art.boundsMin + art.boundsMax) * 0.5f;
    for (size_t p = 0; p < c.dented.size(); ++p) {
        bool moved = false;
        for (glm::vec3& v : c.dented[p]) {
            const float d = glm::length(v - localPoint);
            if (d >= radius) continue;
            const float f = (1.0f - d / radius) * (1.0f - d / radius);
            glm::vec3 next = v + localDir * (depth * f);
            // Never through the middle of the car (a panel folds, it doesn't pass the seats).
            const glm::vec3 fromMid = next - mid, wasMid = v - mid;
            for (int axis = 0; axis < 3; ++axis)
                if (fromMid[axis] * wasMid[axis] < 0.0f) next[axis] = mid[axis] + wasMid[axis] * 0.1f;
            v = next;
            moved = true;
        }
        if (moved && p < art.indices.size()) recomputeNormals(c.dented[p], art.indices[p], c.dentedNormals[p]);
    }
    c.dentsChanged = true;
}

// Damage as the car feels it: less power as it's hurt, and each bent
// wheel less grip.
void RacingModule::applyDamage(Car& c) {
    if (!c.vehicle) return;
    kke::RigidWorld& w = m_rigid->world();
    w.setVehiclePower(c.vehicle, c.totalled ? 0.0f : 0.5f + 0.5f * c.health / 100.0f);
    for (int wh = 0; wh < 4; ++wh) w.setVehicleWheelGrip(c.vehicle, wh, 1.0f - 0.5f * c.bent[wh]);
}

void RacingModule::repairCar(Car& c, float amount) {
    const bool wasHurt = c.health < 100.0f || !c.dented.empty();
    c.health = std::min(100.0f, c.health + amount);
    // Straight wheels as it comes back; the bodywork at the end.
    const float mend = amount / 100.0f;
    for (float& b : c.bent) b = std::max(0.0f, b - mend * 2.0f);
    if (c.health >= 100.0f && !c.dented.empty()) {
        resetShell(c);
        c.dented.clear();
        c.dentedNormals.clear();
        c.dentsChanged = true;
    }
    if (c.health >= 100.0f) refitWheels(c); // new tyres, the wheels back on
    if (c.health > 0.0f) c.totalled = false;
    if (wasHurt) applyDamage(c);
}

void RacingModule::spawnDebris(const Car& c, const glm::vec3& point, const glm::vec3& push, int count) {
    kke::RigidWorld& w = m_rigid->world();
    for (int i = 0; i < count; ++i) {
        if (m_debris.size() >= kMaxDebris) {
            w.remove(m_debris.front().body);
            m_debris.erase(m_debris.begin());
        }
        Car& rng = const_cast<Car&>(c);
        const float r = random01(rng);
        Debris d;
        const int kind = i % 3; // a panel in the car's paint, black trim, glass
        d.half = kind == 0 ? glm::vec3(0.18f + 0.1f * r, 0.02f, 0.12f + 0.08f * r) : kind == 1 ? glm::vec3(0.12f, 0.04f, 0.08f) : glm::vec3(0.05f, 0.02f, 0.05f);
        d.mesh = debrisMesh(kind == 0 ? c.color : kind == 1 ? glm::vec3(0.06f) : glm::vec3(0.55f, 0.65f, 0.7f));
        kke::RigidWorld::BodyDesc b;
        b.halfExtents = d.half;
        b.position = point + glm::vec3(random01(rng) - 0.5f, 0.3f + 0.3f * random01(rng), random01(rng) - 0.5f) * 0.6f;
        b.velocity = push + glm::vec3(random01(rng) - 0.5f, 1.0f + 2.0f * random01(rng), random01(rng) - 0.5f) * 3.0f;
        b.angularVelocity = glm::vec3(random01(rng) - 0.5f, random01(rng) - 0.5f, random01(rng) - 0.5f) * 20.0f;
        b.density = kind == 2 ? 2500.0f : 1200.0f;
        b.friction = 0.6f;
        b.material = kind == 2 ? kke::AudioMaterialTable::Glass : kind == 1 ? kke::AudioMaterialTable::Plastic : kke::AudioMaterialTable::Metal;
        d.body = w.add(b);
        m_debris.push_back(d);
    }
}

void RacingModule::updateDebris(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    for (size_t i = m_debris.size(); i-- > 0;) {
        m_debris[i].age += dt;
        if (m_debris[i].age > kDebrisLife || w.position(m_debris[i].body).y < -5.0f) {
            w.remove(m_debris[i].body);
            m_debris.erase(m_debris.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

// Scrapes: a car running along a wall, or two cars side by side and
// touching, send a stream of sparks (contacts only report the first touch).
void RacingModule::updateScrapes(float dt) {
    if (!m_track || m_phase == Phase::Lobby) return;
    const Track& t = *m_track;
    for (Car& c : m_cars) {
        c.scrapeTimer -= dt;
        const float speed = glm::length(c.velocity);
        if (speed < 4.0f || c.scrapeTimer > 0.0f) continue;
        const float hw = carHalfWidthOf(c);
        const Track::Sample f = t.at(c.where.s);
        const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
        const glm::vec3 pos = carPosition(c) + glm::vec3(0.0f, 0.4f, 0.0f);
        glm::vec3 at(0.0f), normal(0.0f);
        bool scrape = false;
        if (c.where.u - hw < t.minU() + 0.1f) { // the outside wall, on the right
            at = pos - leftFlat * hw;
            normal = leftFlat;
            scrape = true;
        } else if (c.where.u + hw > t.maxU() + 0.3f) { // the inside wall
            at = pos + leftFlat * hw;
            normal = -leftFlat;
            scrape = true;
        }
        if (!scrape) {
            // Door to door with another car.
            for (const Car& o : m_cars) {
                if (&o == &c || &o < &c) continue;
                const glm::vec3 d = carPosition(o) - carPosition(c);
                if (glm::dot(d, d) > 36.0f) continue;
                const float side = glm::dot(d, carLeft(c)), along = glm::dot(d, carForward(c));
                const float len = c.art ? (c.art->boundsMax.z - c.art->boundsMin.z) * 0.5f : 2.4f;
                if (std::fabs(along) < len * 1.5f && std::fabs(side) < hw + carHalfWidthOf(o) + 0.08f && glm::length(c.velocity - o.velocity) > 1.5f) {
                    at = carPosition(c) + d * 0.5f + glm::vec3(0.0f, 0.45f, 0.0f);
                    normal = glm::normalize(carLeft(c) * (side > 0.0f ? -1.0f : 1.0f));
                    scrape = true;
                    break;
                }
            }
        }
        if (!scrape) continue;
        c.scrapeTimer = 0.035f;
        m_fx->sparks(at, normal + glm::vec3(0.0f, 0.4f, 0.0f), 4, 3.0f + speed * 0.1f, c.velocity * 0.7f);
        if (!c.remote && m_damage > 0 && m_phase == Phase::Racing && !c.totalled) {
            c.health = std::max(0.0f, c.health - (m_damage == 2 ? 3.0f : 1.2f) * 0.035f * speed / 20.0f);
            if (c.health <= 0.0f) hitCar(c, at, -normal, 12.0f, nullptr);
        }
        if (static_cast<int>(m_clock * 5.0f) != static_cast<int>((m_clock - dt) * 5.0f)) sound(at, kke::AudioMaterialTable::Metal, 0.15f + speed * 0.005f);
    }
}

// Tyre smoke and marks; smoke and fire from a hurt engine.
void RacingModule::updateEffects(Car& c, float dt) {
    if (!m_track) return;
    const size_t index = static_cast<size_t>(&c - m_cars.data());
    const glm::mat4 xf = c.xf;
    const glm::vec3 up = carUp(c), left = carLeft(c), fwd = carForward(c);
    const float speed = carSpeed(c);
    for (int wh = 0; wh < 4; ++wh) {
        float intensity = 0.0f;
        glm::vec3 at(0.0f);
        bool contact = false;
        if (c.remote) {
            contact = true;
            intensity = (c.net.smoke >> wh) & 1u ? 0.8f : 0.0f;
            at = glm::vec3(xf * glm::vec4(c.art->wheelCenter[wh] - glm::vec3(0.0f, c.art->wheelRadius, 0.0f), 1.0f));
        } else if (static_cast<size_t>(wh) < c.state.wheels.size()) {
            const kke::VehicleWheelState& ws = c.state.wheels[static_cast<size_t>(wh)];
            contact = ws.contact && !((c.detached >> wh) & 1u);
            at = ws.contactPoint;
            wheelEffects(c, wh, dt);
            // A bare rim sparks instead; loose ground throws dust and stones.
            if (ws.condition == kke::TyreCondition::Rim || loose(groundOf(ws.groundMaterial))) contact = false;
            // Spinning (a burnout, a launch) or locked: the tyre's surface
            // slides along the road; sideways: a slip angle well past the
            // grip's peak (a few degrees is just cornering).
            const float surface = ws.angularVelocity * c.art->wheelRadius;
            const float spin = std::fabs(surface - speed);
            const float slide = std::fabs(speed) > 4.0f ? std::fabs(ws.lateralSlip) : 0.0f;
            intensity = std::clamp((spin - 4.0f) / 8.0f, 0.0f, 1.0f) + std::clamp((slide - 9.0f) / 16.0f, 0.0f, 1.0f);
            // Hot rubber smokes more (past ~140 C a burnout billows).
            if (intensity > 0.0f) intensity += std::clamp((ws.surfaceTemp - 140.0f) / 120.0f, 0.0f, 0.5f);
            intensity = std::min(intensity, 1.0f);
        }
        SkidTrail& trail = m_trails[index][static_cast<size_t>(wh)];
        if (!contact || intensity < 0.2f) {
            trail.on = false;
            continue;
        }
        c.wheelSmoke[wh] -= dt;
        if (c.wheelSmoke[wh] <= 0.0f) {
            c.wheelSmoke[wh] = 0.045f / intensity;
            const glm::vec3 drift = -c.velocity * 0.12f + up * 0.6f + left * ((random01(c) - 0.5f) * 1.2f) - fwd * (random01(c) * 0.8f);
            m_fx->smoke(at + up * 0.25f, drift, glm::vec3(0.86f, 0.86f, 0.88f), 0.3f + 0.35f * intensity, 2.2f + intensity, 0.3f + 0.3f * intensity);
        }
        addSkid(index, wh, at, up, intensity);
    }
    // The engine: grey smoke under 45%, black under 20%, fire when it's gone.
    if (c.health < 45.0f || c.totalled) {
        c.smokeTimer -= dt;
        if (c.smokeTimer <= 0.0f) {
            const float bad = 1.0f - c.health / 45.0f;
            c.smokeTimer = c.totalled ? 0.05f : 0.16f - 0.08f * bad;
            const glm::vec3 hood(0.0f, c.art->boundsMax.y * 0.75f, c.art->boundsMax.z * 0.6f);
            const glm::vec3 at = glm::vec3(xf * glm::vec4(hood, 1.0f));
            const glm::vec3 color = glm::mix(glm::vec3(0.6f), glm::vec3(0.08f), std::clamp(bad * 1.3f, 0.0f, 1.0f));
            m_fx->smoke(at, glm::vec3(0.0f, 1.5f, 0.0f) - c.velocity * 0.2f, color, 0.35f + 0.3f * bad, 2.5f + 2.0f * bad, 0.45f + 0.3f * bad);
            if (c.totalled && random01(c) < 0.5f) m_fx->sparks(at, glm::vec3(0.0f, 1.0f, 0.0f), 3, 2.5f);
        }
    }
}

// A tyre mark from where this wheel's last one ended to here, darker the
// harder it slides.
void RacingModule::addSkid(size_t car, int wheel, const glm::vec3& at, const glm::vec3& up, float darkness) {
    SkidTrail& trail = m_trails[car][static_cast<size_t>(wheel)];
    if (!trail.on || glm::length(at - trail.last) > 3.0f) {
        trail.on = true;
        trail.last = at;
        return;
    }
    if (glm::length(at - trail.last) < 0.35f) return;
    Skid s{ trail.last, at, up, std::clamp(darkness, 0.2f, 1.0f) };
    if (m_skids.size() < kMaxSkids) {
        m_skids.push_back(s);
    } else {
        m_skids[m_skidHead] = s;
        m_skidHead = (m_skidHead + 1) % kMaxSkids;
    }
    trail.last = at;
    m_skidsChanged = true;
}

void RacingModule::rebuildSkids() {
    m_skidsChanged = false;
    m_skidRebuild = 0.2f;
    if (!m_skidMesh) return;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    v.reserve(m_skids.size() * 4);
    idx.reserve(m_skids.size() * 6);
    for (const Skid& s : m_skids) {
        const glm::vec3 along = s.b - s.a;
        if (glm::dot(along, along) < 1e-6f) continue;
        const glm::vec3 side = glm::normalize(glm::cross(s.up, along)) * 0.13f;
        const glm::vec3 lift = s.up * 0.015f;
        const glm::vec3 color = glm::mix(glm::vec3(0.16f, 0.16f, 0.18f), glm::vec3(0.035f), s.dark);
        const uint32_t base = static_cast<uint32_t>(v.size());
        v.push_back({ s.a - side + lift, color, s.up, glm::vec2(0.0f) });
        v.push_back({ s.a + side + lift, color, s.up, glm::vec2(0.0f) });
        v.push_back({ s.b + side + lift, color, s.up, glm::vec2(0.0f) });
        v.push_back({ s.b - side + lift, color, s.up, glm::vec2(0.0f) });
        // Counter-clockwise seen from above.
        if (glm::dot(glm::cross(side, along), s.up) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
    if (!v.empty()) m_skidMesh->upload(v, idx);
}

// Drift points: sliding at an angle, fast, adds to a chain that grows a
// combo the longer it goes on; a second's grip banks it, a hit loses it.
void RacingModule::updateDrift(Car& c, float dt) {
    if (event() != Event::Drift || m_phase != Phase::Racing || c.finished || c.totalled) {
        c.drifting = false;
        return;
    }
    glm::vec3 v = c.velocity, f = carForward(c);
    v.y = f.y = 0.0f;
    const float speed = glm::length(v);
    int grounded = 0;
    for (const kke::VehicleWheelState& w : c.state.wheels) grounded += w.contact ? 1 : 0;
    float angle = 0.0f;
    if (speed > 7.0f && glm::length(f) > 1e-3f) angle = glm::degrees(std::acos(std::clamp(glm::dot(v / speed, glm::normalize(f)), -1.0f, 1.0f)));
    c.driftAngle = angle;
    c.drifting = grounded >= 3 && angle >= 12.0f && angle <= 80.0f && carSpeed(c) > 5.0f;
    if (c.drifting) {
        c.driftHold += dt;
        c.driftCombo = std::min(5.0f, 1.0f + std::floor(c.driftHold / 2.0f));
        c.driftChain += angle * speed * dt * 0.25f * c.driftCombo;
        c.driftGrace = 1.0f;
    } else if (c.driftChain > 0.0f) {
        c.driftGrace -= dt;
        if (c.driftGrace <= 0.0f) {
            c.driftScore += c.driftChain;
            c.driftBest = std::max(c.driftBest, c.driftChain);
            c.note = fmt::format("+{:.0f}", c.driftChain);
            c.noteTime = 1.6f;
            c.driftChain = 0.0f;
            c.driftCombo = 1.0f;
            c.driftHold = 0.0f;
        }
    }
}

} // namespace racing
