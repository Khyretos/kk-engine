#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// Input that no human hand makes (docs/ANTI_CHEAT.md "Rigged controllers
// and macros"). Rapid-fire mods, macro keys, scripted adapters (Cronus-
// style) and input bots don't need anything installed that we could
// look for: they arrive as ordinary buttons and sticks. What gives them
// away is timing. People are noisy: a finger mashing a button drifts by
// 10-30 ms between presses, a thumb holding a stick half-way jitters.
// Machines repeat themselves to the millisecond.
//
//   ImpossibleRate   one button pressed faster than people can, sustained
//                    (20+ presses a second over a whole window);
//   Turbo            a fast button (8+ a second) whose press-to-press
//                    times barely vary: a rapid-fire mod;
//   Macro            the same sequence of 4+ presses and releases,
//                    replayed 3+ times with every gap within a few ms;
//   SteadyAxis       a stick held part-way with no noise at all for
//                    seconds (anti-recoil scripts, adapters);
//   InhumanReaction  most recent reactions faster than people react
//                    (under 100 ms after the game showed the cue);
//   BadValue         non-finite or out-of-range values, time going
//                    backwards: a forged input stream.
//
// This only FLAGS: a finding is evidence for a review, never a ban by
// itself (accessibility hardware, rhythm-game experts and odd drivers
// can trip one). Games decide: log it, show it to moderators, send it to
// their server, or weigh it with the server's own checks.
//
// Pure logic: feed it timestamps from the OS events (SDL's, in seconds),
// not frame times, or 60 Hz frames make every human look regular.
struct InputSanitySettings {
    size_t window = 24;              // presses per button judged together
    double maxHumanRate = 20.0;      // presses/s over the window: above = ImpossibleRate
    double turboRate = 8.0;          // regularity is only judged at or above this rate
    double turboMaxJitterMs = 2.0;   // std dev of press-to-press times below this = Turbo
    size_t macroMinSteps = 4;        // shortest sequence (presses + releases) that counts
    size_t macroMaxSteps = 16;       // longest looked for
    size_t macroRepeats = 3;         // times the sequence must repeat back to back
    double macroToleranceMs = 3.0;   // each gap within this of the same gap last time
    double steadySeconds = 2.0;      // part-way stick held without noise this long
    float steadyEpsilon = 1e-4f;     // "no noise": every value within this of the first
    float steadyMin = 0.05f;         // |value| range counted as part-way (not rest, not the edge)
    float steadyMax = 0.95f;
    double minReactionSeconds = 0.1; // faster than this is not a human reaction
    size_t reactionWindow = 10;      // recent reactions looked at
    size_t reactionMinFast = 8;      // this many of them too fast = InhumanReaction
    double cooldown = 5.0;           // s before the same finding on the same control repeats
};

class InputSanity {
public:
    enum class Kind : uint8_t { ImpossibleRate, Turbo, Macro, SteadyAxis, InhumanReaction, BadValue, Count };

    struct Finding {
        Kind kind = Kind::BadValue;
        uint32_t control = 0;  // the caller's id for the button or axis (0 for reactions)
        double time = 0.0;
        std::string detail;    // human-readable, for logs and review
    };

    // A button (key, mouse or pad button) went down or up at `time`
    // (seconds, one monotonic clock for everything fed in).
    void button(uint32_t control, bool down, double time);
    // An axis moved to `value` (-1..1) at `time`. Feeding only changes
    // (SDL's motion events) is right: call update() every frame so a
    // value that never changes is still timed.
    void axis(uint32_t control, float value, double time);
    // How long after the game showed a cue the player answered it
    // (seconds; negative = anticipated, ignored).
    void reaction(double seconds, double time);
    // Judges axes that haven't changed since their last event.
    void update(double time);

    const std::deque<Finding>& findings() const { return m_findings; } // newest last, at most 64
    size_t count(Kind kind) const { return m_counts[static_cast<size_t>(kind)]; }
    size_t total() const;
    // 0 (nothing seen) .. 1 (strong evidence), from the findings so far.
    float suspicion() const;
    void reset();

    InputSanitySettings settings;
    std::function<void(const Finding&)> onFinding; // each new finding, as it happens

private:
    struct Button {
        std::deque<double> presses;
        double lastTime = -1e300;
    };
    struct Step {
        uint32_t control;
        bool down;
        double time;
    };
    struct Axis {
        float runValue = 0.0f;
        double runStart = 0.0;
        double lastTime = -1e300;
        bool reported = false;
    };
    void report(Kind kind, uint32_t control, double time, std::string detail);
    void checkButton(uint32_t control, Button& b, double time);
    void checkMacro(double time);
    void checkAxis(uint32_t control, Axis& a, double time);

    std::map<uint32_t, Button> m_buttons;
    std::map<uint32_t, Axis> m_axes;
    std::deque<Step> m_steps;
    std::deque<double> m_reactions;
    double m_lastStepTime = -1e300;
    std::deque<Finding> m_findings;
    std::array<size_t, static_cast<size_t>(Kind::Count)> m_counts{};
    std::map<std::pair<uint8_t, uint32_t>, double> m_lastReported;
};

const char* toString(InputSanity::Kind kind);

} // namespace kke
