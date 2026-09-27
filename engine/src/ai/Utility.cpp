#include "kke/ai/Utility.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kke::ai {

namespace {
float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
} // namespace

float Curve::evaluate(float xIn) const {
    const float x = clamp01(xIn);
    float y = 0.0f;
    switch (kind) {
    case Kind::Linear: y = m * (x - c) + b; break;
    case Kind::Quadratic: y = m * std::pow(std::max(x - c, 0.0f), k) + b; break;
    case Kind::Logistic: y = k / (1.0f + std::exp(-m * (x - c))) + b; break;
    case Kind::Logit: {
        // logit(x) = log(x / (1 - x)), scaled back into [0, 1] around c.
        const float xe = std::clamp(x, 0.001f, 0.999f);
        y = (std::log(xe / (1.0f - xe)) / std::max(m, 0.001f)) + c + b;
        break;
    }
    case Kind::Step: y = x >= c ? 1.0f : 0.0f; break;
    case Kind::Constant: y = b; break;
    }
    y = clamp01(y);
    return invert ? 1.0f - y : y;
}

bool curveKindFromName(const std::string& nameIn, Curve::Kind& out) {
    std::string name = nameIn;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    if (name == "linear" || name == "inverse") out = Curve::Kind::Linear;
    else if (name == "quadratic") out = Curve::Kind::Quadratic;
    else if (name == "logistic") out = Curve::Kind::Logistic;
    else if (name == "logit") out = Curve::Kind::Logit;
    else if (name == "step") out = Curve::Kind::Step;
    else if (name == "constant") out = Curve::Kind::Constant;
    else return false;
    return true;
}

const char* curveKindName(Curve::Kind kind) {
    switch (kind) {
    case Curve::Kind::Linear: return "linear";
    case Curve::Kind::Quadratic: return "quadratic";
    case Curve::Kind::Logistic: return "logistic";
    case Curve::Kind::Logit: return "logit";
    case Curve::Kind::Step: return "step";
    case Curve::Kind::Constant: return "constant";
    }
    return "linear";
}

float scoreAction(const UtilityAction& action, const InputFn& inputs) {
    if (action.considerations.empty()) return std::max(action.weight, 0.0f);
    float score = 1.0f;
    for (const Consideration& c : action.considerations) {
        score *= c.curve.evaluate(inputs ? inputs(c.input) : 0.0f);
        if (score <= 0.0f) return 0.0f;
    }
    // Compensation (Mark, "Building a Better Centaur"): multiplying many
    // numbers below 1 drags the score down just for having more of them.
    const float modification = 1.0f - 1.0f / float(action.considerations.size());
    const float makeUp = (1.0f - score) * modification;
    score = score + makeUp * score;
    return std::max(score * action.weight, 0.0f);
}

std::vector<float> scoreAll(const std::vector<UtilityAction>& actions, const InputFn& inputs) {
    std::vector<float> scores;
    scores.reserve(actions.size());
    for (const UtilityAction& a : actions) scores.push_back(scoreAction(a, inputs));
    return scores;
}

Choice chooseAction(const std::vector<UtilityAction>& actions, const InputFn& inputs, int current,
                    const std::function<bool(int)>& blocked) {
    Choice best;
    for (int i = 0; i < int(actions.size()); ++i) {
        if (blocked && blocked(i)) continue;
        float s = scoreAction(actions[size_t(i)], inputs);
        if (s <= 0.0f) continue;
        if (i == current) s *= 1.0f + actions[size_t(i)].momentum;
        if (s > best.score) best = { i, s };
    }
    return best;
}

} // namespace kke::ai
