#pragma once

#include "kke/Buffer.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
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

    // Smoke: soft lit puffs (alpha). Spark: hot streaks along their
    // velocity (added light). Glow: soft round light (flames, embers, magic,
    // a muzzle flash). Flake: a small lit card that tumbles and flutters
    // (confetti, snow, leaves, wood chips, glass glints) and comes to rest
    // on its floor. Ring: an expanding ring of light (a shockwave, a
    // portal), facing the camera or lying flat.
    enum class Kind : uint8_t { Smoke, Spark, Glow, Flake, Ring };
    static constexpr int kKindCount = 5;
    enum class Shape : uint8_t { Square, Disc, Shard }; // a flake's outline
    struct Particle {
        Kind kind = Kind::Smoke;
        glm::vec3 position{0.0f}, velocity{0.0f};
        glm::vec3 color{0.8f};     // smoke, flake: sRGB albedo; spark, glow, ring: linear light (HDR, e.g. 6, 3, 1)
        glm::vec3 colorEnd{-1.0f}; // the colour it fades to over its life (negative: stays `color`): fire goes yellow -> red
        float radius = 0.4f;       // m at birth
        float growth = 1.0f;       // m/s the radius grows (smoke spreads)
        float opacity = 0.6f;      // at birth; fades to 0 over its life
        float life = 2.0f;         // s
        float drag = 1.2f;         // 1/s: how fast it slows in the air
        float rise = 0.6f;         // m/s^2 up (hot smoke; negative falls); sparks use gravity instead
        float spin = 0.0f;         // rad/s (smoke)
        float stretch = 0.03f;     // sparks: streak length in seconds of travel
        float flip = 0.0f;         // flakes: rad/s it tumbles over (a card seen edge-on, then face-on)
        float flutter = 0.0f;      // flakes: m/s of side-to-side sway while it falls (leaves, snow, confetti)
        float thickness = 0.15f;   // rings: the band's width, as a fraction of the radius
        float floor = 0.0f;        // y of the ground: sparks bounce off it, flakes come to rest on it
        Shape shape = Shape::Square;
        bool flat = false;         // rings: lie flat on the ground instead of facing the camera
        float age = 0.0f;
        float angle = 0.0f;
        float flipAngle = 0.0f;
        float seed = 0.0f;         // 0..1, the puff's shape
    };

    // Adds one (the oldest goes when full).
    void emit(const Particle& p);
    // A puff of smoke: `color` grey-white for tyres, near-black for an engine fire.
    void smoke(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float radius = 0.35f, float life = 2.2f,
               float opacity = 0.55f);
    // A burst of sparks off a surface (normal = away from it), `speed` m/s.
    void sparks(const glm::vec3& position, const glm::vec3& normal, int count, float speed, const glm::vec3& along = glm::vec3(0.0f));
    // Moves and ages every particle; `wind` (m/s) carries smoke with it.
    void update(float dt, const glm::vec3& wind = glm::vec3(0.0f));
    void clear();
    size_t count() const { return m_particles.size(); }
    size_t capacity() const { return m_max; }

    // From renderTranslucent (once per view in split screen).
    void draw(const RenderContext& ctx);

private:
    struct GpuVertex {
        glm::vec3 center;
        glm::vec3 color;
        glm::vec4 params;   // radius, opacity, angle, seed
        glm::vec3 velocity; // sparks: the streak
        glm::vec2 corner;
        glm::vec2 extra;    // flake: (squash 0..1, shape); ring: (-1 flat / -2 facing, thickness)
    };
    void build(std::vector<GpuVertex>& out, Kind kind, const glm::vec3& eye) const;
    void drawBatch(const RenderContext& ctx, Kind kind, const std::vector<GpuVertex>& vertices, int slot);
    Pipeline& pipeline(Kind kind);

    Application& m_app;
    size_t m_max;
    std::vector<Particle> m_particles;
    std::unique_ptr<Pipeline> m_pipelines[kKindCount]; // made the first time that kind is drawn
    // [frame in flight][view * kKindCount + kind]: each view sorts its own smoke back to front.
    struct Slot { std::unique_ptr<Buffer> buffer; size_t capacity = 0; };
    std::vector<Slot> m_slots[Renderer::kMaxFramesInFlight];
    std::vector<GpuVertex> m_scratch;
    mutable std::vector<uint32_t> m_order;
    uint32_t m_rng = 0x9e3779b9u;
    float random01();
};

} // namespace kke
