#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

// The CPU half of kke::ParticleEffects (ENG-G18): every particle's motion,
// life and death, with no GPU in sight, so it is tested on its own
// (tests/test_particle_sim.cpp) and kke::ParticleLibrary can play effects
// into it anywhere. ParticleEffects owns one and draws it.
//
// Beyond drag, rise, wind and a floor, a particle can swirl round its
// emitter (vortex), be pulled to it (attract, negative pushes), bounce off
// the world (collide, through a ray function the game sets: one ray per
// moving particle per frame, within a budget), follow curves over its life
// (size, colour, opacity), and hand its death to the library (onDeath: a
// firework's shell bursting).
class ParticleSim {
public:
    explicit ParticleSim(size_t maxParticles = 6000);

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
        // ENG-G18:
        glm::vec3 origin{0.0f};    // the emitter's spot: what vortex and attract act around
        float vortex = 0.0f;       // m/s of swirl round the vertical line through origin (positive: counter-clockwise from above)
        float attract = 0.0f;      // m/s^2 toward origin (negative pushes away)
        bool collide = false;      // bounces off (sparks, glows) or settles on (flakes) the world, through setCollider (GPU: the depth)
        // ENG-F11: swirling air (kke/ParticleStep.h's particleTurbulence): m/s, and the size of its eddies in m.
        float turbulence = 0.0f;
        float turbulenceSize = 2.0f;
        uint16_t curve = 0;        // 1-based index of an OverLife added with addCurve; 0: the kind's own fade
        uint16_t onDeath = 0;      // given back in deaths() when it dies of age (the library's sub-effect); 0: nothing
        float age = 0.0f;
        float angle = 0.0f;
        float flipAngle = 0.0f;
        float seed = 0.0f;         // 0..1, the puff's shape
    };

    // Curves over a particle's life, as keys evenly spaced from birth (first)
    // to death (last), linearly between them; an empty one changes nothing.
    // size multiplies the radius, color replaces color/colorEnd, opacity
    // multiplies the kind's own fade.
    struct OverLife {
        std::vector<float> size;
        std::vector<glm::vec3> color;
        std::vector<float> opacity;
    };
    // Returns the 1-based index to put in Particle::curve.
    uint16_t addCurve(const OverLife& curve);
    // nullptr for 0 or an index never added.
    const OverLife* curve(uint16_t index) const;
    static float sample(const std::vector<float>& keys, float t, float fallback);
    static glm::vec3 sample(const std::vector<glm::vec3>& keys, float t, const glm::vec3& fallback);

    // A ray from `from` to `to` against the game's world: true with the
    // first hit's point and surface normal. Without one, collide does
    // nothing. raysPerFrame caps the cost: past it, the rest wait their
    // turn (next frame starts where this one stopped).
    using RayFn = std::function<bool(const glm::vec3& from, const glm::vec3& to, glm::vec3& hit, glm::vec3& normal)>;
    void setCollider(RayFn ray, int raysPerFrame = 512);

    // Adds one (the oldest goes when full).
    void emit(const Particle& p);
    // A particle that may live on the GPU (ENG-G18 part 4): with setGpuBorn(true)
    // it is only recorded here, with its birth time, for kke::ParticleEffects
    // to write once into a GPU ring (its motion is closed-form, see
    // kke/ParticleMotion.h, or stepped on the GPU when it attracts, has
    // turbulence or collides, see kke/ParticleStep.h); otherwise it is
    // emit()ted like any other. It must not need the CPU: no onDeath or curve.
    void emitGpu(const Particle& p);
    void setGpuBorn(bool on) { m_gpuBorn = on; }
    bool gpuBorn() const { return m_gpuBorn; }
    struct Born {
        Particle particle;
        float time; // time() when it was born
    };
    static constexpr size_t kMaxBorn = 262144; // waiting to be handed over, at most
    // Hands over the particles emitGpu() recorded since the last call.
    void takeBorn(std::vector<Born>& out);
    // Seconds of update() so far, and the wind of the last one.
    float time() const { return m_time; }
    const glm::vec3& wind() const { return m_wind; }
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
    const std::vector<Particle>& particles() const { return m_particles; }

    // The particles with an onDeath that died of age in the last update().
    struct Death {
        glm::vec3 position, velocity;
        float floor;
        uint16_t onDeath;
    };
    const std::vector<Death>& deaths() const { return m_deaths; }

    float random01();

private:
    void collide(Particle& p, const glm::vec3& from);

    size_t m_max;
    std::vector<Particle> m_particles;
    std::vector<OverLife> m_curves;
    std::vector<Death> m_deaths;
    RayFn m_ray;
    int m_raysPerFrame = 512;
    size_t m_rayCursor = 0; // where this frame's ray budget starts
    bool m_gpuBorn = false;
    std::vector<Born> m_born;
    float m_time = 0.0f;
    glm::vec3 m_wind{0.0f};
    uint32_t m_rng = 0x9e3779b9u;
};

} // namespace kke
