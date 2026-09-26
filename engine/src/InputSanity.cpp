#include "kke/InputSanity.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace kke {

namespace {

constexpr size_t kMaxFindings = 64;

std::string format(const char* fmt, double a, double b = 0.0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    return buf;
}

} // namespace

const char* toString(InputSanity::Kind kind) {
    switch (kind) {
    case InputSanity::Kind::ImpossibleRate: return "impossible rate";
    case InputSanity::Kind::Turbo: return "turbo";
    case InputSanity::Kind::Macro: return "macro";
    case InputSanity::Kind::SteadyAxis: return "steady axis";
    case InputSanity::Kind::InhumanReaction: return "inhuman reaction";
    case InputSanity::Kind::BadValue: return "bad value";
    case InputSanity::Kind::Count: break;
    }
    return "unknown";
}

void InputSanity::report(Kind kind, uint32_t control, double time, std::string detail) {
    // The same thing on the same control once per cooldown: a turbo
    // button held for a minute is one finding, not hundreds.
    const auto key = std::make_pair(static_cast<uint8_t>(kind), control);
    if (auto it = m_lastReported.find(key); it != m_lastReported.end() && time - it->second < settings.cooldown) return;
    m_lastReported[key] = time;
    ++m_counts[static_cast<size_t>(kind)];
    m_findings.push_back({ kind, control, time, std::move(detail) });
    while (m_findings.size() > kMaxFindings) m_findings.pop_front();
    if (onFinding) onFinding(m_findings.back());
}

void InputSanity::button(uint32_t control, bool down, double time) {
    if (!std::isfinite(time)) return report(Kind::BadValue, control, m_lastStepTime, "non-finite time");
    Button& b = m_buttons[control];
    if (time < b.lastTime) return report(Kind::BadValue, control, time, "time went backwards");
    b.lastTime = time;
    if (down) {
        b.presses.push_back(time);
        while (b.presses.size() > std::max<size_t>(settings.window, 3)) b.presses.pop_front();
        checkButton(control, b, time);
    }
    if (time >= m_lastStepTime) {
        m_lastStepTime = time;
        m_steps.push_back({ control, down, time });
        while (m_steps.size() > settings.macroMaxSteps * settings.macroRepeats + 1) m_steps.pop_front();
        checkMacro(time);
    }
}

void InputSanity::checkButton(uint32_t control, Button& b, double time) {
    if (b.presses.size() < std::max<size_t>(settings.window, 3)) return;
    const size_t n = b.presses.size() - 1; // intervals
    const double span = b.presses.back() - b.presses.front();
    if (span <= 0.0) return report(Kind::ImpossibleRate, control, time, format("%.0f presses at the same instant", double(n + 1)));
    const double rate = static_cast<double>(n) / span;
    if (rate > settings.maxHumanRate) {
        return report(Kind::ImpossibleRate, control, time, format("%.1f presses/s sustained over %.0f presses", rate, double(n + 1)));
    }
    if (rate < settings.turboRate) return;
    const double mean = span / static_cast<double>(n);
    double var = 0.0;
    for (size_t i = 1; i < b.presses.size(); ++i) {
        const double d = (b.presses[i] - b.presses[i - 1]) - mean;
        var += d * d;
    }
    const double jitterMs = std::sqrt(var / static_cast<double>(n)) * 1000.0;
    if (jitterMs < settings.turboMaxJitterMs)
        report(Kind::Turbo, control, time, format("%.1f presses/s, only %.2f ms of jitter", rate, jitterMs));
}

// The newest `repeats` x `p` steps: is each step the same control and
// direction as the one `p` earlier, after the same gap (within the
// tolerance)? Two or more different controls in the sequence, so a
// single mashed button is left to the turbo check.
void InputSanity::checkMacro(double time) {
    const size_t repeats = std::max<size_t>(settings.macroRepeats, 2);
    const double tolerance = settings.macroToleranceMs / 1000.0;
    for (size_t p = std::max<size_t>(settings.macroMinSteps, 2); p <= settings.macroMaxSteps; ++p) {
        const size_t needed = p * repeats + 1; // +1: the gap before the first step
        if (m_steps.size() < needed) break;
        const size_t first = m_steps.size() - p * (repeats - 1);
        bool same = true;
        for (size_t i = first; i < m_steps.size() && same; ++i) {
            const Step& a = m_steps[i];
            const Step& b = m_steps[i - p];
            const double gapA = a.time - m_steps[i - 1].time;
            const double gapB = b.time - m_steps[i - p - 1].time;
            same = a.control == b.control && a.down == b.down && std::abs(gapA - gapB) <= tolerance;
        }
        if (!same) continue;
        std::set<uint32_t> controls;
        for (size_t i = m_steps.size() - p; i < m_steps.size(); ++i) controls.insert(m_steps[i].control);
        if (controls.size() < 2) continue;
        report(Kind::Macro, m_steps.back().control, time,
               format("%.0f-step sequence replayed %.0f times with identical timing", double(p), double(repeats)));
        m_steps.clear(); // judged: start over
        return;
    }
}

void InputSanity::axis(uint32_t control, float value, double time) {
    if (!std::isfinite(value) || !std::isfinite(time) || std::abs(value) > 1.0001f)
        return report(Kind::BadValue, control, std::isfinite(time) ? time : 0.0, "axis value out of range");
    Axis& a = m_axes[control];
    if (time < a.lastTime) return report(Kind::BadValue, control, time, "time went backwards");
    a.lastTime = time;
    checkAxis(control, a, time);
    if (std::abs(value - a.runValue) > settings.steadyEpsilon) {
        a.runValue = value;
        a.runStart = time;
        a.reported = false;
    }
}

void InputSanity::checkAxis(uint32_t control, Axis& a, double time) {
    const float mag = std::abs(a.runValue);
    if (a.reported || mag < settings.steadyMin || mag > settings.steadyMax) return;
    if (time - a.runStart < settings.steadySeconds) return;
    a.reported = true;
    report(Kind::SteadyAxis, control, time, format("held at %.4f with no noise for %.1f s", a.runValue, time - a.runStart));
}

void InputSanity::update(double time) {
    if (!std::isfinite(time)) return;
    for (auto& [control, a] : m_axes)
        if (time >= a.lastTime) checkAxis(control, a, time);
}

void InputSanity::reaction(double seconds, double time) {
    if (!std::isfinite(seconds)) return report(Kind::BadValue, 0, time, "non-finite reaction time");
    if (seconds < 0.0) return; // anticipated: a guess, not a reaction
    m_reactions.push_back(seconds);
    while (m_reactions.size() > settings.reactionWindow) m_reactions.pop_front();
    if (m_reactions.size() < settings.reactionWindow) return;
    const size_t fast = static_cast<size_t>(
        std::count_if(m_reactions.begin(), m_reactions.end(), [&](double r) { return r < settings.minReactionSeconds; }));
    if (fast >= settings.reactionMinFast)
        report(Kind::InhumanReaction, 0, time,
               format("%.0f of the last %.0f reactions under the human minimum", double(fast), double(m_reactions.size())));
}

size_t InputSanity::total() const {
    size_t n = 0;
    for (size_t c : m_counts) n += c;
    return n;
}

float InputSanity::suspicion() const {
    // How much one finding of each kind says; the chance that all of
    // them are innocent shrinks with every one.
    constexpr std::array<float, static_cast<size_t>(Kind::Count)> weight = { 0.5f, 0.3f, 0.4f, 0.2f, 0.4f, 0.5f };
    double innocent = 1.0;
    for (size_t k = 0; k < weight.size(); ++k) innocent *= std::pow(1.0 - static_cast<double>(weight[k]), static_cast<double>(m_counts[k]));
    return static_cast<float>(1.0 - innocent);
}

void InputSanity::reset() {
    m_buttons.clear();
    m_axes.clear();
    m_steps.clear();
    m_reactions.clear();
    m_findings.clear();
    m_counts.fill(0);
    m_lastReported.clear();
    m_lastStepTime = -1e300;
}

} // namespace kke
