#pragma once

#include "kke/Buffer.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/ParticleSim.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace kke {

class Application;

// Smoke, sparks, fire, flakes and rings: tyre smoke from a burnout, a cloud
// from a wrecked engine, dust, sparks off metal scraping a wall, flames,
// confetti, snow, wood chips, a shockwave. Simulated on the CPU (a few
// thousand at most: position, velocity, drag, rise, growth, fade) and
// drawn as camera-facing quads: smoke and flakes lit by the sun (shadowed)
// and the sky, sparks, glows and rings added as light. Draw it from
// renderTranslucent(), after everything opaque. kke::ParticleLibrary
// (kke/ParticleLibrary.h) plays ready-made effects ("explosion",
// "campfire", "confetti") from data files. See docs/PARTICLE_EFFECTS.md.
//
//   m_fx = std::make_unique<kke::ParticleEffects>(app);           // in init()
//   m_fx->smoke(wheelPos, carVelocity * 0.3f, {0.85f, 0.85f, 0.85f});
//   m_fx->sparks(hit.point, hit.normal, 20, 6.0f);
//   m_fx->update(dt, wind);                                        // in update()
//   m_fx->draw(ctx);                                               // in renderTranslucent()
class ParticleEffects {
public:
    explicit ParticleEffects(Application& app, size_t maxParticles = 6000);
    ~ParticleEffects();
    ParticleEffects(const ParticleEffects&) = delete;
    ParticleEffects& operator=(const ParticleEffects&) = delete;

    // The particle and its kinds live in kke::ParticleSim (the CPU half).
    using Kind = ParticleSim::Kind;
    static constexpr int kKindCount = ParticleSim::kKindCount;
    using Shape = ParticleSim::Shape;
    using Particle = ParticleSim::Particle;

    // Adds one (the oldest goes when full).
    void emit(const Particle& p) { m_sim.emit(p); }
    // A puff of smoke: `color` grey-white for tyres, near-black for an engine fire.
    void smoke(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float radius = 0.35f, float life = 2.2f,
               float opacity = 0.55f) {
        m_sim.smoke(position, velocity, color, radius, life, opacity);
    }
    // A burst of sparks off a surface (normal = away from it), `speed` m/s.
    void sparks(const glm::vec3& position, const glm::vec3& normal, int count, float speed, const glm::vec3& along = glm::vec3(0.0f)) {
        m_sim.sparks(position, normal, count, speed, along);
    }
    // Moves and ages every particle; `wind` (m/s) carries smoke with it.
    void update(float dt, const glm::vec3& wind = glm::vec3(0.0f)) { m_sim.update(dt, wind); }
    void clear() { m_sim.clear(); }
    size_t count() const { return m_sim.count(); }
    size_t capacity() const { return m_sim.capacity(); }
    // The simulation: curves, colliders, deaths (kke/ParticleSim.h).
    ParticleSim& sim() { return m_sim; }

    // From renderTranslucent (once per view in split screen).
    void draw(const RenderContext& ctx);

private:
    // One per particle, read per instance: effect.vert makes the quad's six
    // corners itself (ENG-G18: a seventh of the bytes the old six vertices took).
    struct GpuInstance {
        glm::vec3 center;
        glm::vec3 color;
        glm::vec4 params;   // radius, opacity, angle, seed
        glm::vec3 velocity; // sparks: the streak
        glm::vec2 extra;    // flake: (squash 0..1, shape); ring: (-1 flat / -2 facing, thickness)
    };
    void build(std::vector<GpuInstance>& out, Kind kind, const glm::vec3& eye) const;
    void drawBatch(const RenderContext& ctx, Kind kind, const std::vector<GpuInstance>& instances, int slot);
    Pipeline& pipeline(Kind kind);

    Application& m_app;
    ParticleSim m_sim;
    std::unique_ptr<Pipeline> m_pipelines[kKindCount]; // made the first time that kind is drawn
    // [frame in flight][view * kKindCount + kind]: each view sorts its own smoke back to front.
    struct Slot { std::unique_ptr<Buffer> buffer; size_t capacity = 0; };
    std::vector<Slot> m_slots[Renderer::kMaxFramesInFlight];
    std::vector<GpuInstance> m_scratch;
    mutable std::vector<uint32_t> m_order;
};

} // namespace kke
