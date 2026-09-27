#include "kke/ai/Learning.h"

#include "kke/DataFile.h"

#include <genann.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kke::ai {

namespace {

// splitmix64: the same numbers everywhere (genann's own start uses rand()).
uint64_t nextRandom(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
double random01(uint64_t& state) { return double(nextRandom(state) >> 11) * (1.0 / 9007199254740992.0); }

} // namespace

struct LearnedPolicy::Net {
    genann* ann = nullptr;
    explicit Net(genann* a) : ann(a) {}
    ~Net() {
        if (ann) genann_free(ann);
    }
    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;
};

LearnedPolicy::LearnedPolicy() = default;
LearnedPolicy::~LearnedPolicy() = default;
LearnedPolicy::LearnedPolicy(LearnedPolicy&&) noexcept = default;
LearnedPolicy& LearnedPolicy::operator=(LearnedPolicy&&) noexcept = default;

LearnedPolicy::LearnedPolicy(const LearnedPolicy& other)
    : m_features(other.m_features), m_actions(other.m_actions), m_hidden(other.m_hidden), m_examples(other.m_examples),
      m_trained(other.m_trained) {
    maxExamples = other.maxExamples;
    if (other.m_net && other.m_net->ann) m_net = std::make_unique<Net>(genann_copy(other.m_net->ann));
}

LearnedPolicy& LearnedPolicy::operator=(const LearnedPolicy& other) {
    if (this != &other) {
        LearnedPolicy copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void LearnedPolicy::setup(std::vector<std::string> features, std::vector<std::string> actions, int hidden) {
    m_features = std::move(features);
    m_actions = std::move(actions);
    m_hidden = std::clamp(hidden, 1, 64);
    m_examples.clear();
    m_net.reset();
    m_trained = false;
}

bool LearnedPolicy::addExample(const std::vector<float>& features, int action) {
    if (features.size() != m_features.size() || action < 0 || size_t(action) >= m_actions.size()) return false;
    if (maxExamples > 0 && m_examples.size() >= maxExamples) m_examples.erase(m_examples.begin());
    m_examples.push_back({ features, action });
    return true;
}

void LearnedPolicy::makeNet() {
    // One hidden layer is plenty for a handful of inputs and actions.
    genann* ann = genann_init(int(m_features.size()), 1, m_hidden, int(m_actions.size()));
    if (!ann) {
        m_net.reset();
        return;
    }
    // Exact sigmoids (the cached one is a lookup table filled on first use).
    ann->activation_hidden = genann_act_sigmoid;
    ann->activation_output = genann_act_sigmoid;
    m_net = std::make_unique<Net>(ann);
}

LearnedPolicy::Result LearnedPolicy::train(int epochs, float learningRate, uint64_t seed) {
    Result r;
    if (m_examples.empty() || m_features.empty() || m_actions.empty()) return r;
    makeNet();
    if (!m_net) return r;
    genann* ann = m_net->ann;
    uint64_t rng = seed;
    for (int w = 0; w < ann->total_weights; ++w) ann->weight[w] = random01(rng) - 0.5;

    std::vector<size_t> order(m_examples.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::vector<double> in(m_features.size()), want(m_actions.size());
    auto load = [&](const Example& e) {
        for (size_t i = 0; i < in.size(); ++i) in[i] = double(e.features[i]);
        std::fill(want.begin(), want.end(), 0.0);
        want[size_t(e.action)] = 1.0;
    };
    auto evaluate = [&](float& loss) {
        size_t right = 0;
        double sum = 0.0;
        for (const Example& e : m_examples) {
            load(e);
            const double* out = genann_run(ann, in.data());
            size_t top = 0;
            for (size_t k = 0; k < want.size(); ++k) {
                sum += (out[k] - want[k]) * (out[k] - want[k]);
                if (out[k] > out[top]) top = k;
            }
            if (top == size_t(e.action)) ++right;
        }
        loss = float(sum / double(m_examples.size() * want.size()));
        return float(right) / float(m_examples.size());
    };

    epochs = std::max(1, epochs);
    for (int epoch = 0; epoch < epochs; ++epoch) {
        // Fisher-Yates with the seeded generator.
        for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[size_t(nextRandom(rng) % i)]);
        for (size_t i : order) {
            load(m_examples[i]);
            genann_train(ann, in.data(), want.data(), double(learningRate));
        }
        r.epochs = epoch + 1;
        if ((epoch + 1) % 25 == 0) {
            float loss = 0.0f;
            if (evaluate(loss) >= 1.0f && loss < 0.02f) break;
        }
    }
    r.accuracy = evaluate(r.loss);
    m_trained = true;
    return r;
}

void LearnedPolicy::forget() {
    m_net.reset();
    m_trained = false;
}

std::vector<float> LearnedPolicy::predict(const std::vector<float>& features) const {
    if (!m_trained || !m_net || features.size() != m_features.size()) return {};
    std::vector<double> in(features.begin(), features.end());
    const double* out = genann_run(m_net->ann, in.data());
    std::vector<float> p(m_actions.size());
    double sum = 0.0;
    for (size_t k = 0; k < p.size(); ++k) sum += std::max(out[k], 0.0);
    for (size_t k = 0; k < p.size(); ++k) p[k] = sum > 1e-9 ? float(std::max(out[k], 0.0) / sum) : 1.0f / float(p.size());
    return p;
}

std::string LearnedPolicy::best(const std::vector<float>& features) const {
    const std::vector<float> p = predict(features);
    if (p.empty()) return {};
    return m_actions[size_t(std::max_element(p.begin(), p.end()) - p.begin())];
}

nlohmann::json LearnedPolicy::toJson() const {
    nlohmann::json j;
    j["format"] = "kke.ai.policy";
    j["version"] = 1;
    j["features"] = m_features;
    j["actions"] = m_actions;
    j["hidden"] = m_hidden;
    nlohmann::json ex = nlohmann::json::array();
    for (const Example& e : m_examples) ex.push_back({ { "features", e.features }, { "action", m_actions[size_t(e.action)] } });
    j["examples"] = std::move(ex);
    if (m_trained && m_net) j["weights"] = std::vector<double>(m_net->ann->weight, m_net->ann->weight + m_net->ann->total_weights);
    return j;
}

bool LearnedPolicy::fromJson(const nlohmann::json& j, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    try {
        if (!j.is_object() || j.value("format", std::string()) != "kke.ai.policy") return fail("not a kke.ai.policy");
        LearnedPolicy p;
        p.setup(j.at("features").get<std::vector<std::string>>(), j.at("actions").get<std::vector<std::string>>(), j.value("hidden", 8));
        if (j.contains("examples")) {
            for (const nlohmann::json& e : j.at("examples")) {
                const std::string action = e.at("action").get<std::string>();
                const auto it = std::find(p.m_actions.begin(), p.m_actions.end(), action);
                if (it == p.m_actions.end()) return fail("example action '" + action + "' is not one of the policy's actions");
                if (!p.addExample(e.at("features").get<std::vector<float>>(), int(it - p.m_actions.begin())))
                    return fail("an example has " + std::to_string(e.at("features").size()) + " features, not " + std::to_string(p.m_features.size()));
            }
        }
        if (j.contains("weights")) {
            const std::vector<double> w = j.at("weights").get<std::vector<double>>();
            p.makeNet();
            if (!p.m_net || size_t(p.m_net->ann->total_weights) != w.size())
                return fail("weights: " + std::to_string(w.size()) + " numbers for a network that has " +
                            std::to_string(p.m_net ? p.m_net->ann->total_weights : 0));
            std::copy(w.begin(), w.end(), p.m_net->ann->weight);
            p.m_trained = true;
        }
        p.maxExamples = maxExamples;
        *this = std::move(p);
        return true;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

bool LearnedPolicy::save(const std::filesystem::path& file, std::string* error) const {
    return datafile::saveFile(file, toJson(), error);
}

bool LearnedPolicy::load(const std::filesystem::path& file, std::string* error) {
    nlohmann::json j;
    if (!datafile::loadPath(file, j, error)) return false;
    if (!fromJson(j, error)) {
        if (error) *error = file.string() + ": " + *error;
        return false;
    }
    return true;
}

} // namespace kke::ai
