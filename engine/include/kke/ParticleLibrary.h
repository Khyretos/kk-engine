#pragma once

#include "kke/ParticleEffects.h"

#include <nlohmann/json_fwd.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kke {

// Ready-made particle effects by name: "explosion", "campfire", "dust",
// "wood_chips", "glass_shatter", "confetti", "snow", "rain", "magic",
// "shockwave" and more, played into a kke::ParticleEffects. Each effect is
// data (JSON or YAML, kke::datafile): a few layers, each a burst or a
// steady stream of one kind of particle with ranges for speed, size,
// life and colour. The built-in set is listed by names() and written out
// in docs/PARTICLE_EFFECTS.md; load() adds your own or replaces one.
//
//   m_fx = std::make_unique<kke::ParticleEffects>(app);
//   m_library = std::make_unique<kke::ParticleLibrary>(*m_fx);
//   m_library->play("explosion", { hit.point, hit.normal });             // a burst, once
//   auto fire = m_library->start("campfire", { glm::vec3(0) });           // keeps going
//   m_library->move(fire, { torch.position });                           // follow something
//   m_library->stop(fire);                                               // what's in the air finishes
//   m_library->update(dt, wind);                                         // every frame: emits, then m_fx->update
//   m_fx->draw(ctx);                                                     // renderTranslucent()
class ParticleLibrary {
public:
    explicit ParticleLibrary(ParticleEffects& fx, uint32_t seed = 0x2545f491u);

    // Where an effect plays.
    struct Spot {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};  // away from the surface it came off (bursts spray around it)
        glm::vec3 velocity{0.0f};            // of what it came from (a moving car's sparks, a running torch)
        float scale = 1.0f;                  // x speed, size and area: 2 = an effect twice as big
        float floor = 0.0f;                  // y of the ground: sparks bounce, flakes and chips land there
    };

    // Adds the effects in a data file (a map of name -> effect, JSON or
    // YAML), replacing any of the same name. False with `error` if it
    // can't be read or any effect in it is wrong (then none are added).
    bool load(const std::filesystem::path& file, std::string* error = nullptr);
    bool add(const nlohmann::json& effects, std::string* error = nullptr);
    // Checks a map of effects without adding them (a tool, a test, a mod
    // loader): false with `error` naming the effect and what's wrong.
    static bool validate(const nlohmann::json& effects, std::string* error = nullptr);
    // The built-in effects, as YAML: the same format load() reads.
    static const char* builtInYaml();

    std::vector<std::string> names() const;
    bool has(const std::string& name) const;
    std::string description(const std::string& name) const;
    bool looping(const std::string& name) const; // has a steady stream (start() it), not only bursts

    // Once: every burst layer fires, every stream layer runs for its
    // `duration` (or a second if it has none). False for an unknown name.
    bool play(const std::string& name, const Spot& spot);

    // Keeps going until stop(): bursts fire once, streams go on.
    using EmitterId = uint32_t;
    EmitterId start(const std::string& name, const Spot& spot); // 0 for an unknown name
    void move(EmitterId id, const Spot& spot);
    void stop(EmitterId id);
    void stopAll();
    size_t running() const { return m_emitters.size(); }

    // Emits for everything running, then moves every particle (fx.update).
    void update(float dt, const glm::vec3& wind = glm::vec3(0.0f));

    ParticleEffects& effects() { return m_fx; }

    struct Range {
        float min = 0.0f, max = 0.0f;
    };
    struct Layer {
        ParticleEffects::Kind kind = ParticleEffects::Kind::Smoke;
        float count = 0.0f;      // a burst of this many
        float rate = 0.0f;       // or a stream of this many a second
        float duration = 0.0f;   // a stream's length when played once (0: a second)
        float delay = 0.0f;      // s after the effect starts
        Range speed{ 1.0f, 2.0f };
        float spread = 30.0f;    // degrees around the spot's normal (180: every way)
        bool up = false;         // spray around world up, not the normal
        float inherit = 0.0f;    // x the spot's velocity
        float jitter = 0.0f;     // born up to this far from the spot (m)
        glm::vec3 box{0.0f};     // or anywhere in this box around it (half sizes, m): rain, snow
        std::vector<glm::vec3> colors{ glm::vec3(0.8f) }; // one picked at random
        glm::vec3 colorEnd{-1.0f};
        float brightness = 0.0f; // +- random brightness, 0..1
        Range radius{ 0.1f, 0.2f };
        float growth = 0.0f;
        float opacity = 1.0f;
        Range life{ 1.0f, 1.5f };
        float drag = 1.0f;
        float rise = 0.0f;
        Range spin{ 0.0f, 0.0f };
        Range flip{ 0.0f, 0.0f };
        float flutter = 0.0f;
        float stretch = 0.03f;
        float thickness = 0.15f;
        ParticleEffects::Shape shape = ParticleEffects::Shape::Square;
        bool flat = false;
    };
    struct Effect {
        std::string description;
        std::vector<Layer> layers;
    };
    const Effect* effect(const std::string& name) const;

private:
    static bool parseEffects(const nlohmann::json& effects, std::map<std::string, Effect>& out, std::string* error);
    struct Emitter {
        EmitterId id = 0;
        const Effect* effect = nullptr;
        Spot spot;
        float age = 0.0f;
        float endsAt = -1.0f;              // < 0: until stop()
        std::vector<float> owed;           // per layer: particles due but not yet emitted
        std::vector<bool> burst;           // per layer: burst fired
    };
    void emitLayer(const Layer& layer, const Spot& spot, int count);
    float random01();
    float pick(const Range& r) { return r.min + (r.max - r.min) * random01(); }

    ParticleEffects& m_fx;
    std::map<std::string, Effect> m_effects;
    std::vector<Emitter> m_emitters;
    EmitterId m_nextId = 1;
    uint32_t m_rng;
};

} // namespace kke
