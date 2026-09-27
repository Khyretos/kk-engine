#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kke::ai {

// Teaching by example (imitation learning, docs/AI.md "Teaching by
// example"). You show an animal what to do in a situation: an example is
// the numbers its considerations read at that moment (fear, food, alone,
// ...) plus the action you picked. After a few dozen examples, a small
// neural network (genann: one hidden layer, trained on the device in
// milliseconds) predicts which action you would pick. AiWorld adds that
// prediction to the utility scores, so the animal leans towards what it
// was taught. Its instincts stay: only actions that already make sense
// right now (score above 0) get the lean.
//
// Reproducible: the same examples and seed give the same network (the
// start weights and the shuffle come from a seeded generator, not
// rand()). Saved as JSON or YAML (kke/DataFile.h), examples included.
class LearnedPolicy {
public:
    LearnedPolicy();
    ~LearnedPolicy();
    LearnedPolicy(LearnedPolicy&&) noexcept;
    LearnedPolicy& operator=(LearnedPolicy&&) noexcept;
    LearnedPolicy(const LearnedPolicy& other);
    LearnedPolicy& operator=(const LearnedPolicy& other);

    // What it sees (input names) and what it picks from (action names).
    // Forgets any examples and training.
    void setup(std::vector<std::string> features, std::vector<std::string> actions, int hidden = 8);
    const std::vector<std::string>& features() const { return m_features; }
    const std::vector<std::string>& actions() const { return m_actions; }

    // ---- Examples
    // `features` in features() order; `action` an index into actions().
    // The oldest go once there are maxExamples (a long session doesn't
    // grow without end).
    bool addExample(const std::vector<float>& features, int action);
    size_t exampleCount() const { return m_examples.size(); }
    void clearExamples() { m_examples.clear(); }
    size_t maxExamples = 2000;

    // ---- Training
    struct Result {
        float accuracy = 0.0f; // share of the examples it now gets right
        float loss = 0.0f;     // mean squared error at the end
        int epochs = 0;
    };
    // Passes over the examples in a seeded shuffle; stops early once
    // every example is right and the loss is small. False result
    // (epochs 0) when there are no examples.
    Result train(int epochs = 400, float learningRate = 0.5f, uint64_t seed = 1);
    bool trained() const { return m_trained; }
    void forget(); // back to untrained; examples kept

    // How much it would pick each action, summing to 1. Empty when not
    // trained or `features` has the wrong size.
    std::vector<float> predict(const std::vector<float>& features) const;
    // actions()[the most likely], or "" when not trained.
    std::string best(const std::vector<float>& features) const;

    // ---- Saving (the examples too, so it can go on learning)
    nlohmann::json toJson() const;
    bool fromJson(const nlohmann::json& j, std::string* error = nullptr);
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const; // .json or .yml by extension
    bool load(const std::filesystem::path& file, std::string* error = nullptr);

private:
    struct Example {
        std::vector<float> features;
        int action = 0;
    };
    struct Net;
    std::vector<std::string> m_features, m_actions;
    int m_hidden = 8;
    std::vector<Example> m_examples;
    std::unique_ptr<Net> m_net;
    bool m_trained = false;
    void makeNet();
};

} // namespace kke::ai
