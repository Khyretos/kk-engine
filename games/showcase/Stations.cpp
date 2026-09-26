// kke_demo's stations (ACTION_PLAN.md 1.4, issue #12). The lava basin
// (LavaStation) and the pool: Jolt
// bodies float in a walled basin with kke::boxBuoyancy (Archimedes at
// sample points), on small Gerstner waves drawn by kke::OceanRenderer, the
// same waves the buoyancy reads, so what bobs matches what you see.

#include "ShowcaseModule.h"
#include "kke/Application.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

namespace kke_showcase {

namespace {
const glm::vec3 kPoolCenter(-18.0f, 0.0f, 20.0f);
const glm::vec2 kPoolHalf(4.0f, 4.0f); // inside the walls
constexpr float kPoolWall = 1.0f;      // high: vault over it
constexpr float kPoolWater = 0.75f;    // water level
constexpr float kWallThickness = 0.3f;
const glm::vec3 kLavaCenter(0.0f, 0.0f, 20.0f);
constexpr float kLavaWatchDistance = 18.0f; // closer than this, the lava runs
} // namespace

bool ShowcaseModule::inPool(const glm::vec3& p) const {
    return std::abs(p.x - kPoolCenter.x) < kPoolHalf.x && std::abs(p.z - kPoolCenter.z) < kPoolHalf.y && p.y < kPoolWater + 1.0f;
}

void ShowcaseModule::buildPool(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 stone(0.62f, 0.6f, 0.55f), tiles(0.25f, 0.45f, 0.55f);
    const float t = kWallThickness * 0.5f, h = kPoolWall * 0.5f;
    const glm::vec2 o = kPoolHalf + glm::vec2(t);
    addStaticBox({ kPoolCenter + glm::vec3(0.0f, h, -o.y), { o.x + t, h, t }, stone }, v, idx);
    addStaticBox({ kPoolCenter + glm::vec3(0.0f, h, o.y), { o.x + t, h, t }, stone }, v, idx);
    addStaticBox({ kPoolCenter + glm::vec3(-o.x, h, 0.0f), { t, h, o.y - t }, stone }, v, idx);
    addStaticBox({ kPoolCenter + glm::vec3(o.x, h, 0.0f), { t, h, o.y - t }, stone }, v, idx);
    addStaticBox({ kPoolCenter + glm::vec3(0.0f, 0.005f, 0.0f), { kPoolHalf.x, 0.005f, kPoolHalf.y }, tiles }, v, idx); // floor tiles
    // Small, calm waves: a breeze over the pool.
    m_poolWaves.setWind(1.5f, 0.6f, 0.3f);
    m_poolWaves.seaLevel = kPoolWater;
    // One grid cell per 20 cm, just inside the walls (the waves move the
    // surface sideways a little; the walls hide the edge).
    m_poolWater = std::make_unique<kke::OceanRenderer>(*m_app, 40, kPoolHalf.x * 2.0f - 0.2f);
}

void ShowcaseModule::spawnPoolFloaters(int& n) {
    struct Floater { glm::vec3 at, half; float density; };
    // Density decides: pine (450) rides high, oak (750) low, a raft of
    // light boards (300) stays flat on the waves, steel (7800) sinks.
    const Floater floaters[] = {
        { { -2.0f, 1.2f, -1.5f }, { 0.3f, 0.3f, 0.3f }, 450.0f },
        { { 1.5f, 1.5f, -2.0f }, { 0.3f, 0.3f, 0.3f }, 750.0f },
        { { 0.0f, 1.3f, 1.8f }, { 1.2f, 0.08f, 0.6f }, 300.0f },
        { { -2.2f, 1.4f, 2.0f }, { 0.9f, 0.1f, 0.12f }, 500.0f },
        { { 2.0f, 1.6f, 1.0f }, { 0.25f, 0.25f, 0.25f }, 7800.0f },
    };
    for (const Floater& f : floaters) {
        kke::RigidWorld::BodyDesc d;
        d.halfExtents = f.half;
        d.density = f.density;
        d.position = kPoolCenter + f.at;
        d.rotation = glm::angleAxis(glm::radians(17.0f * static_cast<float>(n)), glm::vec3(0, 1, 0));
        d.material = 2;
        m_crates.push_back({ m_rigid->world().add(d), d.halfExtents, n++ % 2 });
    }
}

void ShowcaseModule::drawPool(const kke::RenderContext& ctx) {
    // The grid centred on the pool (drawOcean snaps it to whole cells).
    if (m_poolWater) m_poolWater->drawOcean(ctx, m_poolWaves, m_poolTime, kPoolCenter);
}

// Every fixed step (after Jolt's): the water's pushes on what's in the
// pool, as impulses for the next step.
void ShowcaseModule::floatBodies(float dt) {
    m_poolTime += dt;
    kke::RigidWorld& w = m_rigid->world();
    m_poolBodies.clear();
    const glm::vec3 lo = kPoolCenter + glm::vec3(-kPoolHalf.x, -1.0f, -kPoolHalf.y);
    const glm::vec3 hi = kPoolCenter + glm::vec3(kPoolHalf.x, kPoolWater + 0.5f, kPoolHalf.y);
    w.bodiesInBox(lo, hi, m_poolBodies);
    kke::BuoyancySettings s;
    const float time = m_poolTime;
    const kke::OceanWaves& waves = m_poolWaves;
    const kke::WaterSurface surface = [&waves, time](float x, float z, glm::vec3& flow) {
        flow = waves.velocity(glm::vec2(x, z), time);
        return waves.height(glm::vec2(x, z), time);
    };
    for (const kke::RigidWorld::BodyBox& b : m_poolBodies) {
        if (b.mass <= 0.0f || !inPool(b.center)) continue;
        kke::BuoyancyBox box{ b.center, b.rotation, b.halfExtents, b.velocity, b.angularVelocity };
        if (kke::boxBuoyancy(box, s, surface, m_buoyancy) <= 0.0f) continue;
        for (const kke::BuoyancyPoint& p : m_buoyancy) w.addImpulse(b.id, p.force * dt, p.point);
    }
}

// The lava basin: dark stone walls around LavaStation's fluid bounds, a
// spout post, and the station itself.
void ShowcaseModule::buildLava(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 basalt(0.2f, 0.19f, 0.18f), floor(0.12f, 0.11f, 0.1f);
    const float t = kWallThickness * 0.5f, h = LavaStation::kWall * 0.5f, in = LavaStation::kHalf;
    const float o = in + t;
    addStaticBox({ kLavaCenter + glm::vec3(0.0f, h, -o), { o + t, h, t }, basalt }, v, idx);
    addStaticBox({ kLavaCenter + glm::vec3(0.0f, h, o), { o + t, h, t }, basalt }, v, idx);
    addStaticBox({ kLavaCenter + glm::vec3(-o, h, 0.0f), { t, h, o - t }, basalt }, v, idx);
    addStaticBox({ kLavaCenter + glm::vec3(o, h, 0.0f), { t, h, o - t }, basalt }, v, idx);
    addStaticBox({ kLavaCenter + glm::vec3(0.0f, 0.005f, 0.0f), { in, 0.005f, in }, floor }, v, idx);
    // The spout: an arm over the basin from a post outside the back wall.
    addStaticBox({ kLavaCenter + glm::vec3(0.12f, 1.0f, o + 0.4f), { 0.12f, 1.0f, 0.12f }, basalt }, v, idx);
    addStaticBox({ kLavaCenter + glm::vec3(0.12f, 1.62f, (o + 0.4f) * 0.5f + 0.05f), { 0.08f, 0.06f, (o + 0.4f) * 0.5f + 0.05f }, basalt }, v, idx);
    m_lava = std::make_unique<LavaStation>(*m_app, kLavaCenter);
}

bool ShowcaseModule::lavaWatched() const {
    auto near = [](const glm::vec3& p) { return glm::length(glm::vec2(p.x - kLavaCenter.x, p.z - kLavaCenter.z)) < kLavaWatchDistance; };
    if (m_app->views().empty()) return near(m_app->camera().position);
    for (const kke::Application::View& view : m_app->views())
        if (near(view.camera.position)) return true;
    return false;
}

} // namespace kke_showcase
