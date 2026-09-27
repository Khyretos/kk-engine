#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kke::ai {

// Utility AI: the decision layer. Every action an agent could take gets a
// score in [0, 1]; the best one wins. A score is the product of
// "considerations", each one input (hunger, how close the threat is, ...)
// read through a response curve. This is the "Infinite Axis Utility
// System" as Dave Mark and Mike Lewis present it ("Building a Better
// Centaur", GDC 2015; "Improving AI Decision Modeling Through Utility
// Theory", GDC 2010), including its compensation factor so an action with
// many considerations isn't punished for having them.
//
// Why utility rather than a behaviour tree for animals: needs and fears
// are continuous ("a bit hungry, quite scared"), and "the one that wants
// it most wins" reads the same in Lua, in a node graph and to a child
// ("the sheep is more scared than hungry, so it runs"). Orders from a
// player (companions, platoons) are just actions that score 1 while the
// order stands. Unit-tested in tests/test_ai.cpp.

// A response curve maps an input in [0, 1] to a score in [0, 1].
// The standard shapes (Mark/Lewis): x is clamped to [0, 1] first.
struct Curve {
    enum class Kind : uint8_t {
        Linear,    // m * (x - c) + b
        Quadratic, // m * (x - c)^k + b
        Logistic,  // k / (1 + e^(-m * (x - c))) + b   (an S)
        Logit,     // the inverse of an S
        Step,      // 1 if x >= c else 0 (b and k ignored)
        Constant,  // b
    };
    Kind kind = Kind::Linear;
    float m = 1.0f; // slope
    float k = 1.0f; // exponent
    float c = 0.0f; // x shift
    float b = 0.0f; // y shift
    bool invert = false; // 1 - result

    float evaluate(float x) const;

    static Curve linear(float slope = 1.0f, float yShift = 0.0f) { return { Kind::Linear, slope, 1.0f, 0.0f, yShift, false }; }
    static Curve inverse() { Curve c = linear(); c.invert = true; return c; }
    static Curve quadratic(float exponent = 2.0f, bool invert = false) { return { Kind::Quadratic, 1.0f, exponent, 0.0f, 0.0f, invert }; }
    // S rising around `mid` with `steepness`.
    static Curve logistic(float mid = 0.5f, float steepness = 10.0f, bool invert = false) {
        return { Kind::Logistic, steepness, 1.0f, mid, 0.0f, invert };
    }
    static Curve step(float threshold = 0.5f, bool invert = false) { return { Kind::Step, 1.0f, 1.0f, threshold, 0.0f, invert }; }
    static Curve constant(float value) { return { Kind::Constant, 0.0f, 1.0f, 0.0f, value, false }; }
};

// Parses "linear", "inverse", "quadratic", "logistic", "logit", "step",
// "constant" (case-insensitive). False for anything else.
bool curveKindFromName(const std::string& name, Curve::Kind& out);
const char* curveKindName(Curve::Kind kind);

// Inputs are read by name from whatever describes the agent right now
// (AiWorld fills: hunger, thirst, tiredness, fear, threat, threat_near,
// curiosity, prey_near, has_order, ...). Unknown names read 0.
using InputFn = std::function<float(const std::string& input)>;

struct Consideration {
    std::string input;
    Curve curve;
};

struct UtilityAction {
    std::string name;     // "flee", "graze", ...
    std::string behavior; // what runs it (AiWorld behaviours); defaults to name
    float weight = 1.0f;  // multiplies the final score (priorities: orders 2, panic 1.5, idle 0.2 ...)
    std::vector<Consideration> considerations;
    // Once running, the action keeps a bonus so agents don't flicker
    // between two nearly equal choices (Mark's "momentum").
    float momentum = 0.15f;
    // At most this often (seconds; 0 = any time) may it be picked again after it ends.
    float cooldown = 0.0f;
};

// Product of the considerations with the compensation factor, times weight.
// An action with no considerations scores its weight.
float scoreAction(const UtilityAction& action, const InputFn& inputs);

struct Choice {
    int index = -1;    // into the action list; -1 = none scored above 0
    float score = 0.0f;
};

// Picks the best action. `current` (or -1) gets its momentum bonus;
// `blocked(i)` true skips an action (cooling down). `bonus` (one per
// action, or empty) is added to the score of actions that already score
// above 0: what a LearnedPolicy leans towards.
Choice chooseAction(const std::vector<UtilityAction>& actions, const InputFn& inputs, int current,
                    const std::function<bool(int)>& blocked = {}, const std::vector<float>& bonus = {});

// Every action's score (for debug overlays and the node editor's "why").
std::vector<float> scoreAll(const std::vector<UtilityAction>& actions, const InputFn& inputs);

} // namespace kke::ai
