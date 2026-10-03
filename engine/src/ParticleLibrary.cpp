#include "kke/ParticleLibrary.h"

#include "kke/DataFile.h"
#include "kke/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// The built-in effects. Glow, spark and ring colours are light (linear,
// above 1 blooms); smoke and flake colours are sRGB albedo. Written as the
// same YAML a game's own file would be (docs/PARTICLE_EFFECTS.md), so any
// of them is a starting point to copy.
constexpr const char* kBuiltIn = R"YAML(
fire:
  description: "Flames licking up, smoke above them"
  layers:
    - { kind: glow, rate: 40, duration: 1.5, speed: [0.3, 0.9], spread: 25, up: true, jitter: 0.15,
        colors: [[6, 2.6, 0.7]], colorEnd: [1.6, 0.25, 0.04], radius: [0.18, 0.3], growth: -0.12,
        life: [0.5, 0.9], drag: 2, rise: 2.2 }
    - { kind: smoke, rate: 6, duration: 1.5, delay: 0.3, speed: [0.5, 0.9], spread: 20, up: true, jitter: 0.1,
        colors: [[0.18, 0.17, 0.16]], colorEnd: [0.45, 0.45, 0.45], radius: [0.15, 0.25], growth: 0.5,
        opacity: 0.45, life: [1.6, 2.4], drag: 1.2, rise: 0.9 }
campfire:
  description: "A small fire that keeps burning, with embers and smoke"
  layers:
    - { kind: glow, rate: 30, speed: [0.2, 0.6], spread: 20, up: true, jitter: 0.12,
        colors: [[5, 2.2, 0.6]], colorEnd: [1.4, 0.2, 0.03], radius: [0.12, 0.22], growth: -0.1,
        life: [0.5, 0.8], drag: 2, rise: 1.8 }
    - { kind: spark, rate: 4, speed: [0.6, 1.4], spread: 25, up: true, jitter: 0.1,
        colors: [[5, 1.6, 0.3]], radius: [0.008, 0.014], life: [1.2, 2.2], drag: 0.4, rise: 1.2, stretch: 0.05 }
    - { kind: smoke, rate: 3, delay: 0.4, speed: [0.4, 0.7], spread: 15, up: true, jitter: 0.08,
        colors: [[0.35, 0.34, 0.33]], radius: [0.12, 0.2], growth: 0.35, opacity: 0.3, life: [2.5, 3.5], drag: 1, rise: 0.6 }
torch:
  description: "A flame on a stick (move() it along with the torch)"
  layers:
    - { kind: glow, rate: 35, speed: [0.2, 0.5], spread: 15, up: true, jitter: 0.03, inherit: 0.6,
        colors: [[6, 2.8, 0.8]], colorEnd: [1.5, 0.2, 0.03], radius: [0.06, 0.1], growth: -0.05,
        life: [0.3, 0.5], drag: 3, rise: 1.5 }
explosion:
  description: "A fireball, a shockwave, debris sparks and a column of smoke"
  layers:
    - { kind: glow, count: 40, speed: [1.5, 5], spread: 180, jitter: 0.3, colors: [[12, 6, 2]], colorEnd: [3, 0.5, 0.08],
        radius: [0.5, 0.9], growth: 1.2, life: [0.35, 0.7], drag: 4, rise: 2 }
    - { kind: ring, count: 1, speed: [0, 0], flat: true, colors: [[6, 3.5, 1.5]], radius: [0.2, 0.2], growth: 14,
        life: [0.45, 0.45], thickness: 0.18 }
    - { kind: spark, count: 50, speed: [6, 16], spread: 75, colors: [[9, 4.2, 1.2]], radius: [0.015, 0.025],
        life: [0.5, 1.2], drag: 0.5, rise: -9.81, stretch: 0.04 }
    - { kind: smoke, count: 26, delay: 0.1, speed: [0.8, 3], spread: 90, jitter: 0.6, colors: [[0.12, 0.11, 0.1]],
        colorEnd: [0.4, 0.4, 0.4], brightness: 0.15, radius: [0.5, 0.9], growth: 1.4, opacity: 0.7, life: [3, 4.5], drag: 1.5, rise: 1 }
    - { kind: flake, count: 18, speed: [4, 9], spread: 60, colors: [[0.2, 0.18, 0.16]], radius: [0.03, 0.07],
        life: [2.5, 3.5], drag: 0.3, rise: -9.81, spin: [-8, 8], flip: [-12, 12], shape: shard }
smoke_column:
  description: "Thick black smoke rising from something burning"
  layers:
    - { kind: smoke, rate: 10, speed: [0.8, 1.5], spread: 15, up: true, jitter: 0.25, colors: [[0.08, 0.08, 0.08]],
        colorEnd: [0.3, 0.3, 0.3], brightness: 0.15, radius: [0.35, 0.5], growth: 0.7, opacity: 0.6, life: [4, 6], drag: 0.8, rise: 0.8 }
steam:
  description: "White steam puffing up (a kettle, a vent, a hot spring)"
  layers:
    - { kind: smoke, rate: 14, speed: [1, 1.8], spread: 12, up: true, colors: [[0.95, 0.95, 0.97]], radius: [0.08, 0.14],
        growth: 0.45, opacity: 0.35, life: [1.2, 1.8], drag: 1.5, rise: 1.2 }
dust:
  description: "A puff of dust off the ground (a landing, a footstep, a falling crate)"
  layers:
    - { kind: smoke, count: 14, speed: [0.8, 2.2], spread: 80, colors: [[0.62, 0.55, 0.45]], brightness: 0.1,
        radius: [0.15, 0.3], growth: 0.6, opacity: 0.4, life: [1.2, 2], drag: 2.5, rise: 0.1 }
impact_dirt:
  description: "Earth and stones kicked up by a hit"
  layers:
    - { kind: flake, count: 22, speed: [2, 5], spread: 35, colors: [[0.36, 0.27, 0.18], [0.45, 0.38, 0.3]], radius: [0.02, 0.05],
        life: [1.5, 2.2], drag: 0.4, rise: -9.81, spin: [-6, 6], flip: [-10, 10], shape: shard }
    - { kind: smoke, count: 8, speed: [0.6, 1.6], spread: 40, colors: [[0.5, 0.42, 0.32]], radius: [0.12, 0.22],
        growth: 0.5, opacity: 0.45, life: [1, 1.6], drag: 2.5 }
sparks:
  description: "A shower of sparks off metal"
  layers:
    - { kind: spark, count: 36, speed: [3, 9], spread: 50, colors: [[9, 4.2, 1.2]], brightness: 0.4, radius: [0.012, 0.022],
        life: [0.25, 0.7], drag: 0.6, rise: -9.81, stretch: 0.035 }
welding:
  description: "Steady sparks and a blue-white glow (move() it along the seam)"
  layers:
    - { kind: glow, rate: 30, speed: [0, 0.05], spread: 180, colors: [[6, 7, 10]], radius: [0.06, 0.1], life: [0.05, 0.1] }
    - { kind: spark, rate: 90, speed: [2, 6], spread: 70, colors: [[9, 5, 1.6]], radius: [0.008, 0.014],
        life: [0.2, 0.6], drag: 0.6, rise: -9.81, stretch: 0.03 }
muzzle_flash:
  description: "A gun's flash, sparks and a wisp of smoke, along the normal"
  layers:
    - { kind: glow, count: 4, speed: [2, 6], spread: 8, colors: [[14, 9, 4]], radius: [0.12, 0.2], life: [0.04, 0.07], drag: 6 }
    - { kind: spark, count: 6, speed: [8, 14], spread: 12, colors: [[9, 5, 1.5]], radius: [0.01, 0.015], life: [0.08, 0.15], stretch: 0.02 }
    - { kind: smoke, count: 3, speed: [0.8, 1.5], spread: 15, colors: [[0.7, 0.7, 0.7]], radius: [0.05, 0.08], growth: 0.3,
        opacity: 0.35, life: [0.6, 1], drag: 3, rise: 0.3 }
wood_chips:
  description: "Splinters and dust from wood breaking"
  layers:
    - { kind: flake, count: 26, speed: [2, 5], spread: 70, colors: [[0.62, 0.45, 0.26], [0.5, 0.34, 0.18], [0.78, 0.62, 0.42]],
        radius: [0.02, 0.05], life: [2.5, 3.5], drag: 0.6, rise: -9.81, spin: [-10, 10], flip: [-14, 14], shape: shard }
    - { kind: smoke, count: 6, speed: [0.5, 1.2], spread: 60, colors: [[0.7, 0.6, 0.48]], radius: [0.1, 0.18], growth: 0.4,
        opacity: 0.3, life: [1, 1.5], drag: 2.5 }
stone_chips:
  description: "Grit and grey dust from stone breaking"
  layers:
    - { kind: flake, count: 24, speed: [2, 6], spread: 70, colors: [[0.55, 0.55, 0.53], [0.42, 0.41, 0.4]], radius: [0.015, 0.04],
        life: [2, 3], drag: 0.3, rise: -9.81, spin: [-8, 8], flip: [-10, 10], shape: shard }
    - { kind: smoke, count: 10, speed: [0.6, 1.8], spread: 70, colors: [[0.68, 0.67, 0.65]], radius: [0.15, 0.28], growth: 0.6,
        opacity: 0.4, life: [1.5, 2.2], drag: 2.5 }
glass_shatter:
  description: "Glinting slivers of glass"
  layers:
    - { kind: flake, count: 40, speed: [2, 6], spread: 80, colors: [[0.85, 0.93, 0.95], [0.7, 0.85, 0.9]], radius: [0.01, 0.03],
        opacity: 0.8, life: [1.5, 2.5], drag: 0.3, rise: -9.81, spin: [-12, 12], flip: [-20, 20], shape: shard }
splash:
  description: "A splash of water: droplets and a ring on the surface"
  layers:
    - { kind: flake, count: 40, speed: [2, 5], spread: 30, colors: [[0.75, 0.85, 0.95]], radius: [0.015, 0.03],
        opacity: 0.75, life: [0.8, 1.2], drag: 0.2, rise: -9.81, shape: disc }
    - { kind: ring, count: 1, speed: [0, 0], flat: true, colors: [[0.5, 0.6, 0.7]], radius: [0.1, 0.1], growth: 1.6,
        life: [1.2, 1.2], thickness: 0.12 }
    - { kind: smoke, count: 6, speed: [0.5, 1.5], spread: 35, colors: [[0.92, 0.95, 1]], radius: [0.08, 0.14], growth: 0.4,
        opacity: 0.3, life: [0.6, 1], drag: 2.5 }
rain:
  description: "Rain over a 20 m square (move() it with the camera)"
  layers:
    - { kind: spark, rate: 900, up: true, spread: 0, speed: [-9, -7], box: [10, 0.5, 10], colors: [[0.45, 0.5, 0.55]],
        brightness: 0.2, radius: [0.006, 0.009], life: [1.4, 1.4], drag: 0, rise: 0, stretch: 0.05 }
snow:
  description: "Snow drifting down over a 20 m square (move() it with the camera)"
  layers:
    - { kind: flake, rate: 260, up: true, spread: 0, speed: [-1.2, -0.8], box: [10, 0.5, 10], colors: [[0.97, 0.98, 1]],
        radius: [0.012, 0.025], life: [8, 8], drag: 1, rise: -0.4, flutter: 0.5, shape: disc }
leaves:
  description: "Autumn leaves tumbling down"
  layers:
    - { kind: flake, rate: 6, up: true, spread: 0, speed: [-0.6, -0.3], box: [3, 0.3, 3],
        colors: [[0.8, 0.45, 0.12], [0.65, 0.25, 0.08], [0.85, 0.65, 0.2], [0.45, 0.5, 0.15]],
        radius: [0.04, 0.07], life: [10, 10], drag: 2, rise: -1.2, spin: [-1.5, 1.5], flip: [-4, 4], flutter: 0.9, shape: shard }
confetti:
  description: "A burst of party confetti"
  layers:
    - { kind: flake, count: 120, speed: [4, 8], spread: 35, up: true,
        colors: [[0.95, 0.2, 0.3], [0.2, 0.6, 0.95], [0.98, 0.85, 0.2], [0.3, 0.85, 0.4], [0.75, 0.35, 0.9], [1, 0.55, 0.15]],
        radius: [0.02, 0.035], life: [5, 7], drag: 2.2, rise: -3, spin: [-6, 6], flip: [-12, 12], flutter: 0.6, shape: square }
fireworks:
  description: "A firework bursting into a ball of coloured stars"
  layers:
    - { kind: glow, count: 1, speed: [0, 0], colors: [[14, 12, 9]], radius: [1, 1], life: [0.12, 0.12] }
    - { kind: spark, count: 140, speed: [7, 10], spread: 180, colors: [[8, 1.6, 2.4], [2, 5, 9], [8, 6.4, 1.6], [2.4, 8, 3]],
        radius: [0.025, 0.035], life: [1.4, 2.2], drag: 1.2, rise: -2.5, stretch: 0.08 }
magic:
  description: "Sparkles swirling up and a soft glow (a spell, a pickup, a portal)"
  layers:
    - { kind: glow, rate: 40, speed: [0.3, 0.8], spread: 180, jitter: 0.4, colors: [[1.6, 2.8, 7], [4.5, 1.8, 7]],
        radius: [0.03, 0.06], life: [0.8, 1.4], drag: 1.5, rise: 0.8 }
    - { kind: ring, rate: 1.2, speed: [0, 0], colors: [[1.5, 2.5, 6]], radius: [0.05, 0.05], growth: 0.9, life: [1.2, 1.2],
        thickness: 0.08 }
heal:
  description: "Green sparkles rising round a character"
  layers:
    - { kind: glow, count: 40, up: true, spread: 10, speed: [0.6, 1.4], box: [0.4, 0.1, 0.4], colors: [[1.6, 6, 1.8]],
        radius: [0.03, 0.06], life: [1, 1.6], drag: 1, rise: 0.4 }
    - { kind: ring, count: 1, speed: [0, 0], flat: true, colors: [[1.2, 4, 1.4]], radius: [0.2, 0.2], growth: 1.4,
        life: [0.8, 0.8], thickness: 0.1 }
shockwave:
  description: "A ring of force across the ground"
  layers:
    - { kind: ring, count: 1, speed: [0, 0], flat: true, colors: [[3, 3.6, 5]], radius: [0.2, 0.2], growth: 10, life: [0.6, 0.6], thickness: 0.2 }
    - { kind: smoke, count: 24, speed: [3, 5], spread: 88, colors: [[0.6, 0.58, 0.55]], radius: [0.15, 0.25], growth: 0.8,
        opacity: 0.35, life: [0.8, 1.2], drag: 2.5 }
coins:
  description: "Gold coins bursting out and spinning to the ground"
  layers:
    - { kind: flake, count: 24, speed: [3, 6], spread: 30, up: true, colors: [[1, 0.78, 0.25]], radius: [0.035, 0.045],
        life: [3, 4], drag: 0.2, rise: -9.81, spin: [0, 0], flip: [-16, 16], shape: disc }
    - { kind: glow, count: 10, speed: [0.5, 1.5], spread: 180, colors: [[6, 4.5, 1.2]], radius: [0.03, 0.05], life: [0.3, 0.6], drag: 3 }
)YAML";

ParticleEffects::Kind kindOf(const std::string& s, bool& ok) {
    ok = true;
    if (s == "smoke") return ParticleEffects::Kind::Smoke;
    if (s == "spark") return ParticleEffects::Kind::Spark;
    if (s == "glow") return ParticleEffects::Kind::Glow;
    if (s == "flake") return ParticleEffects::Kind::Flake;
    if (s == "ring") return ParticleEffects::Kind::Ring;
    ok = false;
    return ParticleEffects::Kind::Smoke;
}

ParticleLibrary::Range rangeOf(const nlohmann::json& j, ParticleLibrary::Range fallback) {
    if (j.is_number()) return { j.get<float>(), j.get<float>() };
    if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) return { j[0].get<float>(), j[1].get<float>() };
    return fallback;
}

bool vec3Of(const nlohmann::json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (!j[static_cast<size_t>(i)].is_number()) return false;
        out[i] = j[static_cast<size_t>(i)].get<float>();
    }
    return true;
}

float number(const nlohmann::json& layer, const char* key, float fallback) {
    auto it = layer.find(key);
    return it != layer.end() && it->is_number() ? it->get<float>() : fallback;
}

bool parseLayer(const nlohmann::json& j, ParticleLibrary::Layer& l, std::string& error) {
    if (!j.is_object()) {
        error = "a layer must be a map";
        return false;
    }
    bool ok = true;
    l.kind = kindOf(j.value("kind", std::string("smoke")), ok);
    if (!ok) {
        error = "unknown kind '" + j.value("kind", std::string()) + "' (smoke, spark, glow, flake or ring)";
        return false;
    }
    l.count = number(j, "count", 0.0f);
    l.rate = number(j, "rate", 0.0f);
    if (l.count <= 0.0f && l.rate <= 0.0f) {
        error = "a layer needs a count (a burst) or a rate (a stream)";
        return false;
    }
    l.duration = number(j, "duration", 0.0f);
    l.delay = number(j, "delay", 0.0f);
    if (j.contains("speed")) l.speed = rangeOf(j["speed"], l.speed);
    l.spread = number(j, "spread", l.spread);
    l.up = j.value("up", false);
    l.inherit = number(j, "inherit", 0.0f);
    l.jitter = number(j, "jitter", 0.0f);
    if (j.contains("box") && !vec3Of(j["box"], l.box)) {
        error = "box must be [x, y, z]";
        return false;
    }
    if (j.contains("colors")) {
        l.colors.clear();
        for (const nlohmann::json& c : j["colors"]) {
            glm::vec3 v;
            if (!vec3Of(c, v)) {
                error = "colors must be a list of [r, g, b]";
                return false;
            }
            l.colors.push_back(v);
        }
        if (l.colors.empty()) l.colors.push_back(glm::vec3(0.8f));
    } else if (j.contains("color")) {
        glm::vec3 v;
        if (!vec3Of(j["color"], v)) {
            error = "color must be [r, g, b]";
            return false;
        }
        l.colors = { v };
    }
    if (j.contains("colorEnd") && !vec3Of(j["colorEnd"], l.colorEnd)) {
        error = "colorEnd must be [r, g, b]";
        return false;
    }
    l.brightness = number(j, "brightness", 0.0f);
    if (j.contains("radius")) l.radius = rangeOf(j["radius"], l.radius);
    l.growth = number(j, "growth", 0.0f);
    l.opacity = number(j, "opacity", 1.0f);
    if (j.contains("life")) l.life = rangeOf(j["life"], l.life);
    l.drag = number(j, "drag", l.drag);
    l.rise = number(j, "rise", 0.0f);
    if (j.contains("spin")) l.spin = rangeOf(j["spin"], l.spin);
    if (j.contains("flip")) l.flip = rangeOf(j["flip"], l.flip);
    l.flutter = number(j, "flutter", 0.0f);
    l.stretch = number(j, "stretch", l.stretch);
    l.thickness = number(j, "thickness", l.thickness);
    const std::string shape = j.value("shape", std::string("square"));
    if (shape == "square") l.shape = ParticleEffects::Shape::Square;
    else if (shape == "disc") l.shape = ParticleEffects::Shape::Disc;
    else if (shape == "shard") l.shape = ParticleEffects::Shape::Shard;
    else {
        error = "unknown shape '" + shape + "' (square, disc or shard)";
        return false;
    }
    l.flat = j.value("flat", false);
    return true;
}

} // namespace

ParticleLibrary::ParticleLibrary(ParticleEffects& fx, uint32_t seed) : m_fx(fx), m_rng(seed ? seed : 1u) {
    nlohmann::json builtIn;
    std::string error;
    if (!datafile::parse(kBuiltIn, datafile::Format::Yaml, builtIn, &error) || !add(builtIn, &error))
        log::get("Particles")->error("built-in particle effects: {}", error);
}

bool ParticleLibrary::load(const std::filesystem::path& file, std::string* error) {
    nlohmann::json data;
    std::string e;
    if (!datafile::loadPath(file, data, &e)) {
        if (error) *error = e;
        return false;
    }
    if (!add(data, &e)) {
        if (error) *error = file.string() + ": " + e;
        return false;
    }
    return true;
}

bool ParticleLibrary::parseEffects(const nlohmann::json& effects, std::map<std::string, Effect>& out, std::string* error) {
    if (!effects.is_object()) {
        if (error) *error = "expected a map of effect name -> effect";
        return false;
    }
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        const nlohmann::json& j = it.value();
        Effect e;
        std::string why;
        const nlohmann::json* layers = j.is_object() && j.contains("layers") ? &j["layers"] : nullptr;
        if (!layers || !layers->is_array() || layers->empty()) why = "needs a list of layers";
        if (j.is_object()) e.description = j.value("description", std::string());
        if (why.empty())
            for (const nlohmann::json& lj : *layers) {
                Layer l;
                if (!parseLayer(lj, l, why)) break;
                e.layers.push_back(std::move(l));
            }
        if (!why.empty()) {
            if (error) *error = "effect '" + it.key() + "': " + why;
            return false;
        }
        out[it.key()] = std::move(e);
    }
    return true;
}

bool ParticleLibrary::validate(const nlohmann::json& effects, std::string* error) {
    std::map<std::string, Effect> scratch;
    return parseEffects(effects, scratch, error);
}

const char* ParticleLibrary::builtInYaml() { return kBuiltIn; }

bool ParticleLibrary::add(const nlohmann::json& effects, std::string* error) {
    std::map<std::string, Effect> parsed;
    if (!parseEffects(effects, parsed, error)) return false; // all or nothing
    // Replacing one in use: emitters keep a pointer to the map's entry,
    // which stays; their per-layer counters are resized in update().
    for (auto& [name, e] : parsed) m_effects[name] = std::move(e);
    return true;
}

std::vector<std::string> ParticleLibrary::names() const {
    std::vector<std::string> out;
    for (const auto& [name, e] : m_effects) out.push_back(name);
    return out;
}

bool ParticleLibrary::has(const std::string& name) const { return m_effects.count(name) != 0; }

const ParticleLibrary::Effect* ParticleLibrary::effect(const std::string& name) const {
    auto it = m_effects.find(name);
    return it == m_effects.end() ? nullptr : &it->second;
}

std::string ParticleLibrary::description(const std::string& name) const {
    const Effect* e = effect(name);
    return e ? e->description : std::string();
}

bool ParticleLibrary::looping(const std::string& name) const {
    const Effect* e = effect(name);
    if (!e) return false;
    return std::any_of(e->layers.begin(), e->layers.end(), [](const Layer& l) { return l.rate > 0.0f && l.duration <= 0.0f; });
}

bool ParticleLibrary::play(const std::string& name, const Spot& spot) {
    const Effect* e = effect(name);
    if (!e) return false;
    // A one-shot emitter: lives until its last layer is done.
    float ends = 0.0f;
    for (const Layer& l : e->layers) ends = std::max(ends, l.delay + (l.rate > 0.0f ? (l.duration > 0.0f ? l.duration : 1.0f) : 0.0f));
    Emitter em;
    em.id = m_nextId++;
    em.effect = e;
    em.spot = spot;
    em.endsAt = ends;
    m_emitters.push_back(std::move(em));
    update(0.0f); // the bursts with no delay go out now
    return true;
}

ParticleLibrary::EmitterId ParticleLibrary::start(const std::string& name, const Spot& spot) {
    const Effect* e = effect(name);
    if (!e) return 0;
    Emitter em;
    em.id = m_nextId++;
    if (m_nextId == 0) m_nextId = 1;
    em.effect = e;
    em.spot = spot;
    m_emitters.push_back(std::move(em));
    return m_emitters.back().id;
}

void ParticleLibrary::move(EmitterId id, const Spot& spot) {
    for (Emitter& e : m_emitters)
        if (e.id == id) e.spot = spot;
}

void ParticleLibrary::stop(EmitterId id) {
    m_emitters.erase(std::remove_if(m_emitters.begin(), m_emitters.end(), [id](const Emitter& e) { return e.id == id; }), m_emitters.end());
}

void ParticleLibrary::stopAll() { m_emitters.clear(); }

float ParticleLibrary::random01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xffffffu) / static_cast<float>(0x1000000);
}

void ParticleLibrary::emitLayer(const Layer& l, const Spot& spot, int count) {
    const float s = spot.scale;
    const glm::vec3 axis = l.up ? glm::vec3(0.0f, 1.0f, 0.0f)
                                : (glm::length(spot.normal) > 1e-4f ? glm::normalize(spot.normal) : glm::vec3(0.0f, 1.0f, 0.0f));
    // Two directions across the axis, for the cone.
    const glm::vec3 helper = std::fabs(axis.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 tx = glm::normalize(glm::cross(helper, axis)), ty = glm::cross(axis, tx);
    const float cosMax = std::cos(glm::radians(std::clamp(l.spread, 0.0f, 180.0f)));
    for (int i = 0; i < count; ++i) {
        ParticleEffects::Particle p;
        p.kind = l.kind;
        // A direction in the cone, evenly over its solid angle.
        const float cz = 1.0f - random01() * (1.0f - cosMax);
        const float sz = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
        const float phi = random01() * 6.2831853f;
        const glm::vec3 dir = axis * cz + (tx * std::cos(phi) + ty * std::sin(phi)) * sz;
        glm::vec3 at = spot.position;
        if (l.jitter > 0.0f) {
            glm::vec3 r(random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f);
            at += r * (l.jitter * s);
        }
        if (l.box != glm::vec3(0.0f))
            at += glm::vec3(random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f) * l.box * s;
        p.position = at;
        p.velocity = dir * pick(l.speed) * s + spot.velocity * l.inherit;
        glm::vec3 color = l.colors[std::min(l.colors.size() - 1, static_cast<size_t>(random01() * static_cast<float>(l.colors.size())))];
        if (l.brightness > 0.0f) color *= 1.0f + l.brightness * (random01() * 2.0f - 1.0f);
        p.color = color;
        p.colorEnd = l.colorEnd;
        p.radius = pick(l.radius) * s;
        p.growth = l.growth * s;
        p.opacity = l.opacity;
        p.life = std::max(0.01f, pick(l.life));
        p.drag = l.drag;
        p.rise = l.rise;
        p.spin = pick(l.spin);
        p.flip = pick(l.flip);
        p.flutter = l.flutter * s;
        p.stretch = l.stretch;
        p.thickness = l.thickness;
        p.floor = spot.floor;
        p.shape = l.shape;
        p.flat = l.flat;
        p.angle = random01() * 6.2831853f;
        p.flipAngle = random01() * 6.2831853f;
        p.seed = random01();
        if (p.kind == ParticleEffects::Kind::Ring && l.flat) p.position.y = std::max(p.position.y, spot.floor + 0.02f);
        m_fx.emit(p);
    }
}

void ParticleLibrary::update(float dt, const glm::vec3& wind) {
    for (Emitter& em : m_emitters) {
        const std::vector<Layer>& layers = em.effect->layers;
        em.owed.resize(layers.size(), 0.0f);
        em.burst.resize(layers.size(), false);
        const float was = em.age;
        em.age += dt;
        for (size_t i = 0; i < layers.size(); ++i) {
            const Layer& l = layers[i];
            if (em.age < l.delay) continue;
            if (l.count > 0.0f && !em.burst[i]) {
                em.burst[i] = true;
                emitLayer(l, em.spot, static_cast<int>(std::lround(l.count)));
            }
            if (l.rate <= 0.0f) continue;
            // A stream: as many as are due since the last frame (fractions carried over).
            const float from = std::max(was, l.delay);
            float until = em.age;
            if (em.endsAt >= 0.0f || l.duration > 0.0f) {
                const float length = l.duration > 0.0f ? l.duration : 1.0f;
                if (em.endsAt >= 0.0f) until = std::min(until, l.delay + length);
            }
            if (until <= from) continue;
            em.owed[i] += l.rate * (until - from);
            const int n = static_cast<int>(em.owed[i]);
            if (n > 0) {
                em.owed[i] -= static_cast<float>(n);
                emitLayer(l, em.spot, n);
            }
        }
    }
    m_emitters.erase(std::remove_if(m_emitters.begin(), m_emitters.end(), [](const Emitter& e) { return e.endsAt >= 0.0f && e.age >= e.endsAt; }),
                     m_emitters.end());
    m_fx.update(dt, wind);
}

} // namespace kke
