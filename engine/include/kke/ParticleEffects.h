#pragma once

#include "kke/Buffer.h"
#include "kke/rhi/Device.h"
#include "kke/GpuParticleRing.h"
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
    void clear();
    // Alive now: the CPU's particles and the GPU's.
    size_t count() const;
    size_t capacity() const { return m_sim.capacity(); }
    // The simulation: curves, colliders, deaths (kke/ParticleSim.h).
    ParticleSim& sim() { return m_sim; }

    // From renderTranslucent (once per view in split screen).
    void draw(const RenderContext& ctx);

    // Layers marked gpu (and ParticleSim::emitGpu) are moved by the GPU
    // (ENG-G18 part 4): each particle is written once, at birth, and
    // effect_gpu.vert works out where it is from then on. One that attracts,
    // has turbulence or collides is stepped by the GPU each frame instead
    // (ENG-F11, kke/ParticleStep.h), colliding with the last frame's depth.
    // On by default; off (or KKE_PARTICLES_CPU=1) they are ordinary CPU
    // particles.
    void setGpuParticles(bool on) { m_sim.setGpuBorn(on); }
    bool gpuParticles() const { return m_sim.gpuBorn(); }

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

    // A GPU particle as effect_gpu.vert reads it (std430), written once at birth.
    struct GpuParticle {
        glm::vec4 posBirth;     // xyz = where it was born, w = birth time
        glm::vec4 velLife;      // xyz = velocity at birth, w = life
        glm::vec4 colorOpacity; // rgb = colour, a = opacity
        glm::vec4 colorEnd;     // rgb = colour it fades to (x < 0: none), a = flake shape / ring flat (1)
        glm::vec4 motion;       // drag, rise, growth, spin
        glm::vec4 shape;        // radius, stretch (spark) or thickness (ring), flutter, floor
        glm::vec4 turn;         // angle, flip angle, flip, seed
        glm::vec4 swirl;        // xyz = origin, w = vortex
        glm::vec4 step;         // attract, turbulence, turbulence size, flags (1 = stepped, 2 = collides)
    };
    // A stepped particle's state (shaders/effect_step.glsl), on the GPU only.
    struct GpuStepped {
        glm::vec4 posBirth; // where it is now, the birth time this state belongs to
        glm::vec4 velRest;  // velocity, 1 when it came to rest on something
    };
    // One kind's GPU particles: the ring of slots, the CPU's copy of every
    // record, and per frame in flight its own buffer, written only while
    // that frame is recorded (its last use has finished), so no frame the
    // GPU is still drawing is ever written.
    struct GpuKind {
        GpuParticleRing ring;
        std::vector<GpuParticle> records;
        std::vector<uint32_t> pending[Renderer::kMaxFramesInFlight]; // slots this frame's buffer hasn't had yet
        std::unique_ptr<Buffer> buffers[Renderer::kMaxFramesInFlight];
        size_t bufferSlots[Renderer::kMaxFramesInFlight] = {};
        VkDescriptorSet sets[Renderer::kMaxFramesInFlight] = {};
        VkBuffer setBuffers[Renderer::kMaxFramesInFlight][2] = {}; // what each set points at now (records, state)
        // ENG-F11: the stepped particles' state, one per slot, kept between frames.
        std::unique_ptr<Buffer> state;
        size_t stateSlots = 0;
        bool stepped = false; // any particle of this kind was stepped since the last clear()
        VkDescriptorSet stepSets[Renderer::kMaxFramesInFlight] = {};
    };
    void takeBorn();
    // This frame's buffer of a kind's records up to date; false when it has none.
    bool syncGpu(Kind kind, uint32_t frame);
    void drawGpu(const RenderContext& ctx, Kind kind);
    Pipeline& gpuPipeline(Kind kind);
    // ENG-F11: steps the stepped particles before the 3D pass (Application::addFrameCompute).
    void step(VkCommandBuffer cmd);
    VkPipeline stepPipeline(bool multisampled);

    Application& m_app;
    ParticleSim m_sim;
    std::unique_ptr<Pipeline> m_pipelines[kKindCount]; // made the first time that kind is drawn
    // [frame in flight][view * kKindCount + kind]: each view sorts its own smoke back to front.
    struct Slot { std::unique_ptr<rhi::Buffer> buffer; size_t capacity = 0; };
    std::vector<Slot> m_slots[Renderer::kMaxFramesInFlight];
    std::vector<GpuInstance> m_scratch;
    mutable std::vector<uint32_t> m_order;
    GpuKind m_gpu[kKindCount];
    std::unique_ptr<Pipeline> m_gpuPipelines[kKindCount];
    std::unique_ptr<rhi::BindGroupLayout> m_gpuSetLayout; // set 2 of the GPU-stepped draw (made by the RHI device)
    std::vector<ParticleSim::Born> m_born;
    // ENG-F11: the step's pipeline (single-sampled depth, multisampled), its sets, its per-frame
    // uniforms, a one-slot state for kinds with none and a 1x1 depth for frames with none.
    VkDescriptorSetLayout m_stepSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_stepLayout = VK_NULL_HANDLE;
    VkPipeline m_stepPipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorPool m_stepPool = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> m_stepFrames[Renderer::kMaxFramesInFlight];
    std::unique_ptr<Buffer> m_noState;
    VkImage m_noDepth = VK_NULL_HANDLE;
    VmaAllocation m_noDepthAlloc = VK_NULL_HANDLE;
    VkImageView m_noDepthView = VK_NULL_HANDLE;
    VkSampler m_nearest = VK_NULL_HANDLE;
    float m_steppedAt = -1.0f; // the sim time of the last step
    bool m_wantDepth = false;
};

} // namespace kke
