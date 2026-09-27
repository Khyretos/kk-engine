#include "MeltDemoModule.h"

#include "kke/Application.h"
#include "kke/Mesh.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace kke_melt {

namespace {

constexpr uint8_t kLava = 0;
constexpr uint8_t kMelt = 1;
constexpr size_t kMaxParticles = 3000;   // the liquid budget (docs/OPTIMIZATION.md rule 5)
constexpr float kParticleRadius = 0.03f;
const glm::vec3 kSpout(0.0f, 1.5f, 0.0f);

// Values tuned by eye to "feel" right in a ~1 minute pour, not measured
// from real materials (relative melting points and behaviour are real).
const MeltDemoModule::BlockPreset kPresets[] = {
    { "Ice (melts to water)", 0.0f, -15.0f, 1.0f, 0.002f, 1.5f, { 0.78f, 0.9f, 1.0f }, { 0.25f, 0.5f, 0.85f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.01f; m.viscosityCold = 0.02f; m.hotTemperature = 100.0f; return m; }(), false },
    { "Wax (softens, re-hardens)", 60.0f, 20.0f, 1.0f, 0.004f, 1.0f, { 0.95f, 0.9f, 0.75f }, { 0.98f, 0.93f, 0.7f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.05f; m.viscosityCold = 0.15f; m.hotTemperature = 120.0f; m.solidifyTemperature = 45.0f; return m; }(), false },
    { "Chocolate", 35.0f, 18.0f, 1.0f, 0.006f, 1.0f, { 0.36f, 0.2f, 0.12f }, { 0.3f, 0.16f, 0.09f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.06f; m.viscosityCold = 0.15f; m.hotTemperature = 80.0f; m.solidifyTemperature = 25.0f; return m; }(), false },
    { "Aluminium (glows, then melts)", 660.0f, 20.0f, 1.5f, 0.002f, 4.0f, { 0.78f, 0.8f, 0.84f }, { 0.85f, 0.86f, 0.9f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.02f; m.viscosityCold = 0.12f; m.hotTemperature = 900.0f; m.solidifyTemperature = 600.0f; return m; }(), true },
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

float rand01(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}

} // namespace

void MeltDemoModule::init(kke::Application& app) {
    m_app = &app;
    defineInput();
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_surface = std::make_unique<kke::FluidSurfaceRenderer>(app);
    m_surface->settings().blurWorldRadius = kParticleRadius * 2.5f;
    m_surface->settings().depthFalloff = kParticleRadius * 2.0f;
    m_blockMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(app);
    // Ground: a 6 m slab top at y = 0, dark stone.
    const glm::vec3 g(0.22f, 0.22f, 0.24f);
    const float s = 3.0f;
    std::vector<kke::Vertex> gv = { { { -s, 0, -s }, g, { 0, 1, 0 }, { 0, 0 } }, { { s, 0, -s }, g, { 0, 1, 0 }, { 0, 0 } },
                                    { { s, 0, s }, g, { 0, 1, 0 }, { 0, 0 } }, { { -s, 0, s }, g, { 0, 1, 0 }, { 0, 0 } } };
    m_ground->upload(gv, { 0, 2, 1, 0, 3, 2 });
    // KKE_MELT_PRESET=0..3 picks the starting block (screenshots, sharing).
    if (const char* e = std::getenv("KKE_MELT_PRESET")) m_preset = std::clamp(std::atoi(e), 0, kPresetCount - 1);
    reset();
    buildPanel();
}

void MeltDemoModule::reset() {
    const BlockPreset& p = kPresets[m_preset];
    kke::ParticleFluid::Params fp;
    fp.radius = kParticleRadius;
    fp.boundsMin = glm::vec3(-2.8f, -1.0f, -2.8f);
    fp.boundsMax = glm::vec3(2.8f, 10.0f, 2.8f);
    fp.ambientTemperature = 20.0f;
    fp.coolingRate = 0.05f;       // slow cooling to air; the block is the big heat sink
    fp.heatDiffusion = 3.0f;
    fp.friction = 0.02f;          // liquids slide; high contact friction made piles stand with vertical walls
    fp.substeps = 2;              // a 1.5 m drop reaches ~6 m/s: 120 Hz keeps it from tunnelling
    m_fluid = std::make_unique<kke::ParticleFluid>(fp, kMaxParticles);
    kke::FluidMaterial lava;
    // XSPH factors stay small (<= ~0.15): large values average every
    // particle's velocity with its neighbours so hard that a whole blob
    // moves like one rigid body — a column of lava could never slump.
    lava.viscosityHot = 0.03f;
    lava.viscosityCold = 0.15f;
    lava.hotTemperature = 1200.0f;
    lava.solidifyTemperature = 550.0f; // crusts over below this
    m_fluid->setMaterial(kLava, lava);
    m_fluid->setMaterial(kMelt, p.liquid);

    // A 0.5 m block on the ground, 2.5 cm voxels (20^3 inside a 24^3 grid).
    const float cell = 0.025f;
    m_block = std::make_unique<kke::MeltVolume>(glm::ivec3(24, 24, 24), glm::vec3(-0.3f, 0.0f, -0.3f), cell);
    kke::MeltMaterial& mm = m_block->material();
    mm.meltingPoint = p.meltingPoint;
    mm.heatCapacity = p.heatCapacity;
    mm.meltRate = p.meltRate;
    mm.conduction = p.conduction;
    mm.liquidMaterial = kMelt;
    mm.liquidTemperature = p.meltingPoint + 2.0f;
    // Lava holds far more heat than its thin contact layer passes on; at
    // 1.0 every drop crusted on touch and sealed the block in a black shell.
    // Tuned headless in simulated time: ice ~60% left at 5 s, ~8% at 25 s.
    mm.liquidHeatCapacity = 3.0f;
    m_block->fillBox(glm::vec3(-0.25f, 0.0f, -0.25f), glm::vec3(0.25f, 0.5f, 0.25f), p.startTemperature);
    kke::MeltVolume* block = m_block.get();
    m_fluid->addCollider([block](const glm::vec3& pos, glm::vec3& n) { return block->signedDistance(pos, n); });
    m_emitAccum = 0.0f;
}

void MeltDemoModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    const float dt = ctx.fixedDt;
    if (m_pouring) {
        m_emitAccum += m_pourRate * dt;
        while (m_emitAccum >= 1.0f) {
            m_emitAccum -= 1.0f;
            // A stream ~8 cm wide leaving the spout at 1.5 m/s.
            float a = rand01(m_rng) * 6.2831853f, r = std::sqrt(rand01(m_rng)) * 0.04f;
            glm::vec3 p = kSpout + glm::vec3(std::cos(a) * r, rand01(m_rng) * 0.03f, std::sin(a) * r);
            if (!m_fluid->add(p, glm::vec3(0.0f, -1.5f, 0.0f), m_lavaTemperature, kLava)) break;
        }
    }
    auto t0 = std::chrono::steady_clock::now();
    m_fluid->step(dt);
    auto t1 = std::chrono::steady_clock::now();
    m_block->step(dt, *m_fluid);
    auto t2 = std::chrono::steady_clock::now();
    // Smoothed timings for the panel.
    m_fluidMs = m_fluidMs * 0.9 + std::chrono::duration<double, std::milli>(t1 - t0).count() * 0.1;
    m_meltMs = m_meltMs * 0.9 + std::chrono::duration<double, std::milli>(t2 - t1).count() * 0.1;
}

void MeltDemoModule::update(const kke::UpdateContext&) {
    readInput();
    if (m_block->rebuildMesh()) {
        const BlockPreset& p = kPresets[m_preset];
        const auto& pos = m_block->meshPositions();
        const auto& nrm = m_block->meshNormals();
        const auto& glow = m_block->meshGlow();
        std::vector<kke::Vertex> v(pos.size());
        for (size_t i = 0; i < pos.size(); ++i) {
            // Near melting: metal glows; everything else looks wet (tinted
            // toward its liquid colour).
            glm::vec3 color = p.incandescent ? p.solidColor : glm::mix(p.solidColor, p.liquidColor, glow[i] * 0.6f);
            v[i] = kke::Vertex{ pos[i], color, nrm[i], glm::vec2(p.incandescent ? glow[i] * 0.8f : 0.0f, 0.0f) };
        }
        m_blockMesh->upload(v, m_block->meshIndices());
        m_meshTris = m_block->meshIndices().size() / 3;
    }
}

void MeltDemoModule::render(const kke::RenderContext& ctx) {
    m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    const BlockPreset& p = kPresets[m_preset];
    m_blockMesh->draw(ctx, glm::mat4(1.0f), p.incandescent ? 0.8f : 0.0f, p.incandescent ? 0.35f : 0.25f);

    if (m_smoothSurface) m_surface->draw(ctx);
    else m_spheres->draw(ctx, m_sphereScratch);
}

// Particle colours from material and temperature, shared by both drawing
// paths (smooth surface and raw spheres).
void MeltDemoModule::prepass(const kke::PrepassContext& ctx) {
    if (ctx.sceneCovered) return; // only feeds render()
    const BlockPreset& p = kPresets[m_preset];
    const auto& pos = m_fluid->positions();
    const auto& temp = m_fluid->temperatures();
    const auto& mat = m_fluid->materials();
    m_sphereScratch.resize(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        kke::SphereImpostorRenderer::Sphere& s = m_sphereScratch[i];
        s.center = pos[i];
        s.radius = kParticleRadius * 1.25f; // a little overlap reads as one liquid, not marbles
        if (mat[i] == kLava) {
            // Glow only while molten (above the 550 C solid point); below
            // it the particle is rock and drawn as dark, rough crust.
            float heat = std::clamp((temp[i] - 550.0f) / 650.0f, 0.0f, 1.0f);
            s.color = glm::mix(glm::vec3(0.1f, 0.085f, 0.075f), glm::vec3(0.35f, 0.08f, 0.02f), std::min(1.0f, heat * 3.0f));
            s.glow = heat;
            s.roughness = heat > 0.0f ? 0.3f : 0.95f;
        } else {
            s.color = p.liquidColor;
            s.glow = p.incandescent ? std::clamp((temp[i] - 550.0f) / 500.0f, 0.0f, 1.0f) : 0.0f;
            s.roughness = 0.15f;
        }
    }
    if (!m_smoothSurface) return;
    m_surfaceScratch.resize(m_sphereScratch.size());
    for (size_t i = 0; i < m_sphereScratch.size(); ++i) {
        const auto& s = m_sphereScratch[i];
        m_surfaceScratch[i] = { s.center, kParticleRadius * 1.6f, s.color, s.glow };
    }
    m_surface->prepass(ctx, m_surfaceScratch);

}

void MeltDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) { m_blockMesh->drawShadow(ctx); }

void MeltDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Melt" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("melt.pour", "Pour lava on / off", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH);
    action("melt.reset", "Reset", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_WEST);
    action("melt.smooth", "Smooth liquid surface", SDL_SCANCODE_L, SDL_GAMEPAD_BUTTON_NORTH);
    action("melt.block", "Next block", SDL_SCANCODE_B, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    in->commitDefaults();
}

// Keys and pad buttons (actions, rebindable in the input file).
void MeltDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    if (m.pressed("melt.pour")) m_pouring = !m_pouring;
    if (m.pressed("melt.reset")) reset();
    if (m.pressed("melt.smooth")) m_smoothSurface = !m_smoothSurface;
    if (m.pressed("melt.block")) {
        m_preset = (m_preset + 1) % kPresetCount;
        reset();
    }
}

// The settings (RmlUi, kke::DemoPanelModule): a controller opens them with
// View, the keyboard with F3, the mouse just clicks.
void MeltDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Melt");
    s.text("{melt.pour} pour  {melt.block} next block  {melt.smooth} smooth surface  {melt.reset} reset");
    std::vector<std::string> names;
    for (const BlockPreset& p : kPresets) names.push_back(p.name);
    s.choice("Block", &m_preset, names, [this] { reset(); });
    s.toggle("Pour lava", &m_pouring);
    s.toggle("Smooth liquid surface", &m_smoothSurface);
    s.slider("Smoothing", kke::DemoPanelModule::Ref<float>([this] { return m_surface ? &m_surface->settings().blurWorldRadius : nullptr; }), 0.01f,
             0.2f, "%.2f m", {}, 0.01f)
        .showIf([this] { return m_smoothSurface; });
    s.slider("Pour rate", &m_pourRate, 30.0f, 400.0f, "%.0f drops/s", {}, 10.0f);
    s.slider("Lava temperature", &m_lavaTemperature, 800.0f, 1400.0f, "%.0f C", {}, 20.0f);
    s.button("Reset", [this] { reset(); });
    s.separator();
    s.text([this] {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Block left: %.0f%%. Liquid: %zu / %zu particles", static_cast<double>(m_block->solidFraction() * 100.0f),
                      m_fluid->size(), m_fluid->capacity());
        return std::string(buf);
    });
    s.text("Liquid budget full: reset to pour again").showIf([this] { return m_fluid && m_fluid->size() >= m_fluid->capacity(); });
    s.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Fluid %.2f ms, melt %.2f ms per step. Block surface: %zu triangles", m_fluidMs, m_meltMs, m_meshTris);
        return std::string(buf);
    });
    s.note("Lava crusts over below 550 C. Ice turns to water that chills the lava; wax and chocolate harden again as they cool; "
           "aluminium glows before it melts.");
}

} // namespace kke_melt
