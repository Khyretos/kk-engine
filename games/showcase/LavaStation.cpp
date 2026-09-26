#include "LavaStation.h"

#include "kke/Application.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace kke_showcase {

namespace {

constexpr uint8_t kLava = 0;
constexpr uint8_t kMelt = 1;
constexpr size_t kMaxParticles = 700;  // a quarter of melt_demo's: one station of many (docs/OPTIMIZATION.md rule 5)
constexpr float kRadius = 0.04f;     // coarser than melt_demo (0.03): 2.4x the volume a particle
constexpr float kPourRate = 50.0f;     // particles per second
constexpr float kLavaTemperature = 1200.0f;
constexpr float kNextAfter = 4.0f;     // seconds to look at the result before the next block

struct Preset {
    const char* name;
    float meltingPoint, startTemperature, heatCapacity, meltRate, conduction;
    glm::vec3 solidColor, liquidColor;
    kke::FluidMaterial liquid;
    bool incandescent;
};

// melt_demo's presets (tuned there by eye); chocolate left out, it reads
// like wax from a few metres away.
const Preset kPresets[] = {
    { "ice", 0.0f, -15.0f, 1.0f, 0.002f, 1.5f, { 0.78f, 0.9f, 1.0f }, { 0.25f, 0.5f, 0.85f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.01f; m.viscosityCold = 0.02f; m.hotTemperature = 100.0f; return m; }(), false },
    { "wax", 60.0f, 20.0f, 1.0f, 0.004f, 1.0f, { 0.95f, 0.9f, 0.75f }, { 0.98f, 0.93f, 0.7f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.05f; m.viscosityCold = 0.15f; m.hotTemperature = 120.0f; m.solidifyTemperature = 45.0f; return m; }(), false },
    { "aluminium", 660.0f, 20.0f, 1.5f, 0.002f, 4.0f, { 0.78f, 0.8f, 0.84f }, { 0.85f, 0.86f, 0.9f },
      [] { kke::FluidMaterial m; m.viscosityHot = 0.02f; m.viscosityCold = 0.12f; m.hotTemperature = 900.0f; m.solidifyTemperature = 600.0f; return m; }(), true },
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

float rand01(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
}

// Signed distance to an oriented box, with the outward normal.
float boxDistance(const kke::RigidWorld::BodyBox& b, const glm::vec3& p, glm::vec3& normal) {
    const glm::vec3 local = glm::inverse(b.rotation) * (p - b.center);
    const glm::vec3 q = glm::abs(local) - b.halfExtents;
    const float outside = glm::length(glm::max(q, glm::vec3(0.0f)));
    const float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
    glm::vec3 n(0.0f);
    if (outside > 0.0f) {
        n = glm::max(q, glm::vec3(0.0f)) * glm::sign(local) / outside;
    } else {
        const int axis = q.x > q.y ? (q.x > q.z ? 0 : 2) : (q.y > q.z ? 1 : 2);
        n[axis] = local[axis] < 0.0f ? -1.0f : 1.0f;
    }
    normal = b.rotation * n;
    return outside + inside;
}

} // namespace

LavaStation::LavaStation(kke::Application& app, const glm::vec3& center) : m_app(app), m_center(center) {
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_surface = std::make_unique<kke::FluidSurfaceRenderer>(app);
    m_surface->settings().blurWorldRadius = kRadius * 2.5f;
    m_surface->settings().depthFalloff = kRadius * 2.0f;
    m_blockMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    reset();
}

LavaStation::~LavaStation() = default;

const char* LavaStation::blockName() const { return kPresets[m_preset].name; }

void LavaStation::next() {
    m_preset = (m_preset + 1) % kPresetCount;
    reset();
}

void LavaStation::reset() {
    const Preset& p = kPresets[m_preset];
    kke::ParticleFluid::Params fp;
    fp.radius = kRadius;
    fp.boundsMin = m_center + glm::vec3(-kHalf, -1.0f, -kHalf);
    fp.boundsMax = m_center + glm::vec3(kHalf, 6.0f, kHalf);
    fp.groundY = m_center.y;
    fp.ambientTemperature = 20.0f;
    fp.coolingRate = 0.05f;
    fp.heatDiffusion = 3.0f;
    fp.friction = 0.02f;
    fp.substeps = 2; // the 1.5 m drop reaches ~6 m/s: 120 Hz keeps it from tunnelling
    m_fluid = std::make_unique<kke::ParticleFluid>(fp, kMaxParticles);
    kke::FluidMaterial lava;
    lava.viscosityHot = 0.03f;
    lava.viscosityCold = 0.15f;
    lava.hotTemperature = kLavaTemperature;
    lava.solidifyTemperature = 550.0f; // crusts over below this
    m_fluid->setMaterial(kLava, lava);
    m_fluid->setMaterial(kMelt, p.liquid);

    // A 0.5 m block in the basin's middle, 2.5 cm voxels.
    const float cell = 0.025f;
    m_block = std::make_unique<kke::MeltVolume>(glm::ivec3(24, 24, 24), m_center + glm::vec3(-0.3f, 0.0f, -0.3f), cell);
    kke::MeltMaterial& mm = m_block->material();
    mm.meltingPoint = p.meltingPoint;
    mm.heatCapacity = p.heatCapacity;
    mm.meltRate = p.meltRate;
    mm.conduction = p.conduction;
    mm.liquidMaterial = kMelt;
    mm.liquidTemperature = p.meltingPoint + 2.0f;
    mm.liquidHeatCapacity = 3.0f;
    m_block->fillBox(m_center + glm::vec3(-0.25f, 0.0f, -0.25f), m_center + glm::vec3(0.25f, 0.5f, 0.25f), p.startTemperature);
    kke::MeltVolume* block = m_block.get();
    m_fluid->addCollider([block](const glm::vec3& pos, glm::vec3& n) { return block->signedDistance(pos, n); });
    // Crates and characters in the basin: the nearest one's surface.
    m_fluid->addCollider([this](const glm::vec3& pos, glm::vec3& n) {
        float best = 1e9f;
        glm::vec3 bn(0.0f, 1.0f, 0.0f);
        for (const kke::RigidWorld::BodyBox& b : m_bodies) {
            glm::vec3 cn;
            const float d = boxDistance(b, pos, cn);
            if (d < best) { best = d; bn = cn; }
        }
        for (const Capsule& c : m_characters) {
            const glm::vec3 ab = c.b - c.a;
            const float t = std::clamp(glm::dot(pos - c.a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            const glm::vec3 d = pos - (c.a + ab * t);
            const float len = glm::length(d);
            if (len - c.r < best) { best = len - c.r; bn = len > 1e-5f ? d / len : glm::vec3(0.0f, 1.0f, 0.0f); }
        }
        n = bn;
        return best;
    });
    m_emitAccum = 0.0f;
    m_doneFor = 0.0f;
}

void LavaStation::fixedUpdate(float dt, const kke::RigidWorld& world, bool near) {
    if (!near) return; // frozen until someone comes to look
    const auto t0 = std::chrono::steady_clock::now();

    // What's in the basin this step (colliders read these).
    m_bodies.clear();
    world.bodiesInBox(m_center + glm::vec3(-kHalf, 0.0f, -kHalf), m_center + glm::vec3(kHalf, 2.0f, kHalf), m_bodies);
    m_bodies.erase(std::remove_if(m_bodies.begin(), m_bodies.end(), [](const kke::RigidWorld::BodyBox& b) { return b.mass <= 0.0f; }),
                   m_bodies.end()); // the basin itself and the floor: already the fluid's bounds
    m_characters.clear();
    for (kke::RigidWorld::CharacterId id : world.characterIds()) {
        const glm::vec3 feet = world.characterPosition(id);
        const float r = world.characterRadius(id), h = world.characterHeight(id);
        if (std::abs(feet.x - m_center.x) > kHalf + r || std::abs(feet.z - m_center.z) > kHalf + r) continue;
        m_characters.push_back({ feet + glm::vec3(0.0f, r, 0.0f), feet + glm::vec3(0.0f, std::max(r, h - r), 0.0f), r });
    }

    const bool full = m_fluid->size() >= m_fluid->capacity();
    if (!full && m_doneFor <= 0.0f) {
        // A stream ~8 cm wide from a spout 1.5 m up, off to one side of
        // the block so it runs down one face and pools around it.
        const glm::vec3 spout = m_center + glm::vec3(0.12f, 1.5f, 0.1f);
        m_emitAccum += kPourRate * dt;
        while (m_emitAccum >= 1.0f) {
            m_emitAccum -= 1.0f;
            const float a = rand01(m_rng) * 6.2831853f, r = std::sqrt(rand01(m_rng)) * 0.04f;
            const glm::vec3 p = spout + glm::vec3(std::cos(a) * r, rand01(m_rng) * 0.03f, std::sin(a) * r);
            if (!m_fluid->add(p, glm::vec3(0.0f, -1.5f, 0.0f), kLavaTemperature, kLava)) break;
        }
    }
    m_fluid->step(dt);
    m_block->step(dt, *m_fluid);
    // Block gone or no room for more lava: look at it a moment, then the
    // next block.
    if (full || m_block->solidFraction() < 0.03f) {
        m_doneFor += dt;
        if (m_doneFor > kNextAfter) next();
    }
    const auto t1 = std::chrono::steady_clock::now();
    m_stepMs = m_stepMs * 0.9 + std::chrono::duration<double, std::milli>(t1 - t0).count() * 0.1;
}

void LavaStation::update() {
    if (!m_block->rebuildMesh()) return;
    const Preset& p = kPresets[m_preset];
    const auto& pos = m_block->meshPositions();
    const auto& nrm = m_block->meshNormals();
    const auto& glow = m_block->meshGlow();
    std::vector<kke::Vertex> v(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        // Near melting: metal glows, everything else looks wet.
        const glm::vec3 color = p.incandescent ? p.solidColor : glm::mix(p.solidColor, p.liquidColor, glow[i] * 0.6f);
        v[i] = kke::Vertex{ pos[i], color, nrm[i], glm::vec2(p.incandescent ? glow[i] * 0.8f : 0.0f, 0.0f) };
    }
    m_blockMesh->upload(v, m_block->meshIndices());
}

// Particle colours from material and temperature (both drawing paths).
void LavaStation::prepass(const kke::PrepassContext& ctx) {
    const Preset& p = kPresets[m_preset];
    const auto& pos = m_fluid->positions();
    const auto& temp = m_fluid->temperatures();
    const auto& mat = m_fluid->materials();
    m_sphereScratch.resize(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        kke::SphereImpostorRenderer::Sphere& s = m_sphereScratch[i];
        s.center = pos[i];
        s.radius = kRadius * 1.25f;
        if (mat[i] == kLava) {
            // Glows while molten; below 550 C it's dark, rough crust.
            const float heat = std::clamp((temp[i] - 550.0f) / 650.0f, 0.0f, 1.0f);
            s.color = glm::mix(glm::vec3(0.1f, 0.085f, 0.075f), glm::vec3(0.35f, 0.08f, 0.02f), std::min(1.0f, heat * 3.0f));
            s.glow = heat;
            s.roughness = heat > 0.0f ? 0.3f : 0.95f;
        } else {
            s.color = p.liquidColor;
            s.glow = p.incandescent ? std::clamp((temp[i] - 550.0f) / 500.0f, 0.0f, 1.0f) : 0.0f;
            s.roughness = 0.15f;
        }
    }
    m_surfaceScratch.resize(m_sphereScratch.size());
    for (size_t i = 0; i < m_sphereScratch.size(); ++i) {
        const auto& s = m_sphereScratch[i];
        m_surfaceScratch[i] = { s.center, kRadius * 1.6f, s.color, s.glow };
    }
    m_surface->prepass(ctx, m_surfaceScratch);
}

void LavaStation::render(const kke::RenderContext& ctx) {
    const Preset& p = kPresets[m_preset];
    m_blockMesh->draw(ctx, glm::mat4(1.0f), p.incandescent ? 0.8f : 0.0f, p.incandescent ? 0.35f : 0.25f);
    // The smooth surface is screen-space, made for the one main view;
    // split screen draws the particles as spheres.
    if (ctx.viewCount > 1) m_spheres->draw(ctx, m_sphereScratch);
    else m_surface->draw(ctx);
}

void LavaStation::renderShadow(const kke::ShadowRenderContext& ctx) { m_blockMesh->drawShadow(ctx); }

} // namespace kke_showcase
