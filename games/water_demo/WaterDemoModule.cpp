#include "WaterDemoModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Mesh.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <typeindex>

namespace kke_water {

namespace {

// The tank's inside: 3 x 1.5 m of floor, walls 1.1 m high. The water's own
// box goes higher (an invisible lid at 2 m) so a splash can rise over the
// walls' tops and fall back in.
const glm::vec3 kTankMin(-1.5f, 0.0f, -0.75f), kTankMax(1.5f, 1.1f, 0.75f);
constexpr float kWaterTop = 2.0f;
constexpr float kWall = 0.1f;
constexpr float kPaddleRest = 1.43f; // the paddle's centre (x) against the right wall
constexpr float kStroke = 0.8f;      // seconds a wave's push takes

float rand01(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}

void appendBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& color) {
    // Six faces, four corners each, wound counter-clockwise from outside.
    const glm::vec3 n[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& f : n) {
        // Two axes across the face; u x w = f, so the corners below go
        // counter-clockwise seen from outside (the front face).
        const glm::vec3 u = std::fabs(f.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(f, u);
        const glm::vec3 c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;
        const uint32_t base = static_cast<uint32_t>(v.size());
        const glm::vec2 corner[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const glm::vec2& k : corner) {
            const glm::vec3 p = c + h * (f + u * k.x + w * k.y);
            v.push_back(kke::Vertex{ p, color, f, glm::vec2(0.0f) });
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

} // namespace

std::vector<kke::ModuleDependency> WaterDemoModule::dependencies() const {
    return {
        { std::type_index(typeid(kke::RigidBodyModule)), true, "the crates and the paddle are Jolt bodies; they step before the water" },
    };
}

void WaterDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_bodies = app.getModule<kke::RigidBodyModule>();
    m_pool = std::make_unique<kke::TaskPool>(app.resourceBudget().workerThreads);
    // On a GPU with a queue for it (not a software one: KKE_MPM_GPU=1
    // forces it, =0 keeps it on the CPU).
    m_gpu = kke::MpmWaterGpu::create(app.device());
    // KKE_WATER_CELL=metres: the water's grid spacing. 5 cm on a GPU
    // (~77,000 particles), 7 cm on the CPU (~28,000).
    m_cell = m_gpu ? 0.05f : 0.07f;
    // Stiffer water bounces less (a dropped block rebounds 29% of its fall at 12 m/s, 20% at 40): the GPU has the room for twice the substeps.
    m_stiffness = m_gpu ? 24.0f : 12.0f;
    if (const char* e = std::getenv("KKE_WATER_CELL")) m_cell = std::clamp(static_cast<float>(std::atof(e)), 0.02f, 0.2f);
    if (const char* e = std::getenv("KKE_MPM_GPU_CHECK")) m_check = std::atoi(e) != 0;
    defineInput();
    m_surface = std::make_unique<kke::FluidSurfaceRenderer>(app);
    kke::FluidSurfaceRenderer::Settings& s = m_surface->settings();
    s.clearWater = true;
    s.particleVolume = m_cell * m_cell * m_cell / 8.0f;
    s.blurWorldRadius = m_cell * m_smoothing;
    s.depthFalloff = m_cell * 1.5f;
    // A tank is shallow: far more colour per metre than real water (whose
    // blue needs metres) so 30 cm of it reads as a pool's cyan.
    s.absorption = glm::vec3(2.0f, 0.55f, 0.35f);
    s.deepColor = glm::vec3(0.03f, 0.16f, 0.2f);
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_tank = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_cube = std::make_unique<kke::DynamicMeshRenderer>(app);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox(v, idx, glm::vec3(-0.5f), glm::vec3(0.5f), glm::vec3(1.0f));
    m_cube->upload(v, idx);
    buildTank();
    reset();
    buildPanel();
}

// The tank: floor, back and side walls (the front is left open to look
// in, but holds the water and the crates all the same), and the ground
// round it. Static Jolt boxes under the drawn ones, for the crates.
void WaterDemoModule::buildTank() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 tile(0.8f, 0.84f, 0.86f), stone(0.42f, 0.4f, 0.37f), ground(0.3f, 0.36f, 0.24f);
    auto wall = [&](const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& color, bool drawn) {
        if (drawn) appendBox(v, idx, lo, hi, color);
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Box;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = (hi - lo) * 0.5f;
        d.position = (lo + hi) * 0.5f;
        d.friction = 0.5f;
        m_bodies->world().add(d);
    };
    const glm::vec3 lo = kTankMin, hi = kTankMax;
    wall(glm::vec3(lo.x - kWall, -kWall, lo.z - kWall), glm::vec3(hi.x + kWall, 0.0f, hi.z + kWall), tile, true);                 // floor
    wall(glm::vec3(lo.x - kWall, 0.0f, lo.z - kWall), glm::vec3(hi.x + kWall, hi.y, lo.z), tile, true);                          // back
    wall(glm::vec3(lo.x - kWall, 0.0f, lo.z), glm::vec3(lo.x, hi.y, hi.z), stone, true);                                         // left
    wall(glm::vec3(hi.x, 0.0f, lo.z), glm::vec3(hi.x + kWall, hi.y, hi.z), stone, true);                                         // right
    wall(glm::vec3(lo.x - kWall, 0.0f, hi.z), glm::vec3(hi.x + kWall, kWaterTop, hi.z + kWall), stone, false);                   // front (open)
    wall(glm::vec3(lo.x - kWall, hi.y, lo.z - kWall), glm::vec3(hi.x + kWall, kWaterTop, lo.z), stone, false);                   // over the back,
    wall(glm::vec3(lo.x - kWall, hi.y, lo.z), glm::vec3(lo.x, kWaterTop, hi.z), stone, false);                                   // and the sides:
    wall(glm::vec3(hi.x, hi.y, lo.z), glm::vec3(hi.x + kWall, kWaterTop, hi.z), stone, false);                                   // the water's lid
    appendBox(v, idx, glm::vec3(-8.0f, -0.3f, -8.0f), glm::vec3(8.0f, -kWall, 8.0f), ground);
    m_tank->upload(v, idx);
}

void WaterDemoModule::clearCrates() {
    for (const Crate& c : m_crates) m_bodies->world().remove(c.body);
    m_crates.clear();
    if (m_paddle != kke::RigidWorld::kNoBody) m_bodies->world().remove(m_paddle);
    m_paddle = kke::RigidWorld::kNoBody;
}

void WaterDemoModule::reset() {
    clearCrates();
    kke::MpmWater::Params p;
    p.boundsMin = kTankMin;
    p.boundsMax = glm::vec3(kTankMax.x, kWaterTop, kTankMax.z);
    p.cellSize = m_cell;
    p.viscosity = m_viscosity;
    p.soundSpeed = m_stiffness;
    p.tension = m_cohesion;
    m_water = std::make_unique<kke::MpmWater>(p);
    m_water->setTaskPool(m_pool.get());
    m_water->setGpu(m_gpu);
    // The dam: a metre of water 0.8 m deep against the left wall.
    m_water->fill(glm::vec3(kTankMin.x, 0.0f, kTankMin.z), glm::vec3(-0.5f, 0.8f, kTankMax.z));
    // The paddle: a kinematic board at the right wall (makeWave pushes it).
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Box;
    d.motion = kke::RigidWorld::Motion::Kinematic;
    d.halfExtents = glm::vec3(0.04f, 0.45f, kTankMax.z - 0.02f);
    d.position = glm::vec3(kPaddleRest, 0.5f, 0.0f);
    m_paddle = m_bodies->world().add(d);
    m_waveTime = -1.0f;
    // Three wooden crates in the dry half, waiting for the wave.
    for (int i = 0; i < 3; ++i) dropCrate(false);
    m_steps = 0;
}

void WaterDemoModule::dropCrate(bool stone) {
    if (m_crates.size() >= 24) return; // MpmWater::kMaxBodies is far off; the scene just gets busy
    const float size = stone ? 0.2f : 0.18f + rand01(m_rng) * 0.12f;
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Box;
    d.halfExtents = glm::vec3(size * 0.5f, size * 0.5f, size * 0.5f * (stone ? 1.0f : 1.4f));
    d.position = glm::vec3(-0.2f + rand01(m_rng) * 1.3f, 1.4f + rand01(m_rng) * 0.3f, -0.4f + rand01(m_rng) * 0.8f);
    d.rotation = glm::angleAxis(rand01(m_rng) * 6.2831853f, glm::normalize(glm::vec3(rand01(m_rng), 1.0f, rand01(m_rng))));
    // Pine is about half as dense as water, granite two and a half times.
    d.density = stone ? 2500.0f : 450.0f + rand01(m_rng) * 200.0f;
    d.friction = 0.5f;
    d.restitution = 0.05f;
    const glm::vec3 color = stone ? glm::vec3(0.45f, 0.45f, 0.47f) : glm::mix(glm::vec3(0.62f, 0.42f, 0.22f), glm::vec3(0.8f, 0.6f, 0.34f), rand01(m_rng));
    m_crates.push_back({ m_bodies->world().add(d), d.halfExtents, color });
}

void WaterDemoModule::makeWave() {
    if (m_waveTime < 0.0f) m_waveTime = 0.0f;
}

void WaterDemoModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    const float dt = ctx.fixedDt;
    kke::RigidWorld& world = m_bodies->world();
    // The paddle: out and back in one smooth stroke (a kinematic body's
    // velocity is what the water feels).
    float x = kPaddleRest;
    if (m_waveTime >= 0.0f) {
        m_waveTime += dt;
        if (m_waveTime >= kStroke) m_waveTime = -1.0f;
        else x = kPaddleRest - 0.35f * std::sin(3.14159265f * m_waveTime / kStroke);
    }
    world.moveKinematic(m_paddle, glm::vec3(x, 0.5f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt);

    m_link.pull(world, *m_water);
    const auto t0 = std::chrono::steady_clock::now();
    // KKE_MPM_GPU_CHECK=1: every 120th step on both, compared.
    if (m_check && m_steps % 120 == 60) {
        m_lastCheck = m_water->checkGpuStep(dt);
        if (m_lastCheck.done)
            kke::log::get("MpmWaterGpu")->info("check: {} particles, GPU and CPU {:.5f} m apart at most ({:.6f} m on average); GPU {:.2f} ms, CPU {:.2f} ms",
                                               m_lastCheck.particles, m_lastCheck.maxDifference, m_lastCheck.meanDifference, m_lastCheck.gpuMs, m_lastCheck.cpuMs);
    } else {
        m_water->step(dt);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_stepMs = m_steps == 0 ? ms : m_stepMs * 0.9 + ms * 0.1;
    m_link.push(world, *m_water);

    if (++m_steps % 300 == 0) {
        const kke::MpmWater::Stats& st = m_water->stats();
        kke::log::get("WaterDemo")->info("water: {} particles, {:.2f} ms a step ({} substeps) on the {}; {} bodies in it; fastest {:.2f} m/s; {} GPU steps, {} late",
                                         m_water->size(), m_stepMs, st.substeps, m_water->onGpu() ? "GPU" : "CPU", m_water->bodies().size(), st.maxSpeed,
                                         st.gpuSteps, st.gpuLate);
    }
}

void WaterDemoModule::update(const kke::UpdateContext&) { readInput(); }

void WaterDemoModule::prepass(const kke::PrepassContext& ctx) {
    if (ctx.sceneCovered) return;
    const std::vector<glm::vec3>& x = m_water->positions();
    const std::vector<glm::vec3>& v = m_water->velocities();
    auto foam = [&](size_t i) {
        // Fast water is breaking up: white (a dam break's front runs at
        // 2 to 3 m/s; spray and a wave's crest faster).
        const float s = glm::length(v[i]);
        return std::clamp((s - 2.5f) / 2.5f, 0.0f, 1.0f);
    };
    if (m_smoothSurface) {
        m_surfaceScratch.resize(x.size());
        const float r = m_cell * 0.55f;
        for (size_t i = 0; i < x.size(); ++i) m_surfaceScratch[i] = { x[i], r, glm::vec3(0.55f, 0.75f, 0.85f), 0.0f, foam(i) };
        m_surface->prepass(ctx, m_surfaceScratch);
    } else {
        // Raw particles, coloured by speed: still blue to fast white.
        m_sphereScratch.resize(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            kke::SphereImpostorRenderer::Sphere& s = m_sphereScratch[i];
            s.center = x[i];
            s.radius = m_cell * 0.25f;
            s.color = glm::mix(glm::vec3(0.1f, 0.3f, 0.7f), glm::vec3(0.95f), foam(i));
            s.glow = 0.0f;
            s.roughness = 0.3f;
        }
    }
}

void WaterDemoModule::render(const kke::RenderContext& ctx) {
    m_tank->draw(ctx, glm::mat4(1.0f), 0.0f, 0.8f);
    const kke::RigidWorld& world = m_bodies->world();
    for (const Crate& c : m_crates) m_cube->draw(ctx, glm::scale(world.transform(c.body), c.halfExtents * 2.0f), 0.0f, 0.75f);
    // The paddle, dark wood.
    const glm::mat4 paddle = glm::scale(world.transform(m_paddle), glm::vec3(0.08f, 0.9f, (kTankMax.z - 0.02f) * 2.0f));
    m_cube->draw(ctx, paddle, 0.0f, 0.7f);
    if (!m_smoothSurface) m_spheres->draw(ctx, m_sphereScratch);
}

void WaterDemoModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_smoothSurface) m_surface->drawWater(ctx);
}

void WaterDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_tank->drawShadow(ctx);
    const kke::RigidWorld& world = m_bodies->world();
    for (const Crate& c : m_crates) m_cube->drawShadow(ctx, glm::scale(world.transform(c.body), c.halfExtents * 2.0f));
}

void WaterDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Water" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("water.wave", "Make a wave", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH);
    action("water.crate", "Drop a crate", SDL_SCANCODE_C, SDL_GAMEPAD_BUTTON_EAST);
    action("water.stone", "Drop a stone block", SDL_SCANCODE_V, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    action("water.reset", "Reset (the dam breaks again)", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_WEST);
    action("water.smooth", "Clear water / particles", SDL_SCANCODE_L, SDL_GAMEPAD_BUTTON_NORTH);
    in->commitDefaults();
}

void WaterDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    if (m.pressed("water.wave")) makeWave();
    if (m.pressed("water.crate")) dropCrate(false);
    if (m.pressed("water.stone")) dropCrate(true);
    if (m.pressed("water.reset")) reset();
    if (m.pressed("water.smooth")) m_smoothSurface = !m_smoothSurface;
}

void WaterDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Water");
    s.text("{water.wave} wave  {water.crate} crate  {water.stone} stone  {water.smooth} clear water / particles  {water.reset} reset");
    s.button("Make a wave", [this] { makeWave(); });
    s.button("Drop a crate", [this] { dropCrate(false); });
    s.button("Drop a stone block", [this] { dropCrate(true); });
    s.button("Break the dam again", [this] { reset(); });
    s.toggle("Clear water", &m_smoothSurface);
    s.slider("Thickness (viscosity)", &m_viscosity, 0.0f, 0.02f, "%.3f m2/s", [this] { m_water->setViscosity(m_viscosity); }, 0.001f);
    s.slider("Stiffness (sound speed)", &m_stiffness, 8.0f, 40.0f, "%.0f m/s", [this] { m_water->setSoundSpeed(m_stiffness); }, 1.0f);
    s.slider("Cohesion (pull)", &m_cohesion, 0.0f, 0.03f, "%.3f", [this] { m_water->setTension(m_cohesion); }, 0.001f);
    s.slider("Surface smoothing", &m_smoothing, 0.3f, 2.0f, "%.2f cells",
             [this] { if (m_surface) m_surface->settings().blurWorldRadius = m_cell * m_smoothing; }, 0.05f)
        .showIf([this] { return m_smoothSurface; });
    s.slider("Murk (red)", kke::DemoPanelModule::Ref<float>([this] { return m_surface ? &m_surface->settings().absorption.x : nullptr; }), 0.0f, 3.0f,
             "%.2f /m", {}, 0.05f)
        .showIf([this] { return m_smoothSurface; });
    s.slider("Refraction", kke::DemoPanelModule::Ref<float>([this] { return m_surface ? &m_surface->settings().refraction : nullptr; }), 0.0f, 0.1f, "%.3f",
             {}, 0.005f)
        .showIf([this] { return m_smoothSurface; });
    s.separator();
    s.text([this] {
        char buf[220];
        const kke::MpmWater::Stats& st = m_water->stats();
        std::snprintf(buf, sizeof(buf), "%zu particles, %.0f cm cells, on the %s: %.2f ms a step (%d substeps)", m_water->size(),
                      static_cast<double>(m_cell * 100.0f), m_water->onGpu() ? "GPU" : "CPU", m_stepMs, st.substeps);
        return std::string(buf);
    });
    s.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu crates, %zu bodies in the water. Fastest water %.1f m/s", m_crates.size(), m_water->bodies().size(),
                      static_cast<double>(m_water->stats().maxSpeed));
        return std::string(buf);
    });
    s.text([this] {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "GPU check: %zu particles %.2f mm apart at most (GPU %.2f ms, CPU %.2f ms)", m_lastCheck.particles,
                      static_cast<double>(m_lastCheck.maxDifference * 1000.0f), m_lastCheck.gpuMs, m_lastCheck.cpuMs);
        return std::string(buf);
    }).showIf([this] { return m_lastCheck.done; });
    s.note("MLS-MPM water (Hu et al. 2018): particles on a grid, no neighbour search. Wooden crates float about half under; the stone one sinks. "
           "The front of the tank is open to look in, but holds the water.");
}

} // namespace kke_water
