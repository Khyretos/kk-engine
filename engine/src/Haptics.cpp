#include "kke/Haptics.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

struct Preset {
    const char* name;
    HapticPulse pulse;
};

// Tuned on an Xbox Series and a DualSense pad: the low motor reads as
// weight, the high one as texture; short and sharp reads as a hit.
const std::array<Preset, 10>& presets() {
    static const std::array<Preset, 10> list{ {
        { "impact", { 0.8f, 0.5f, 0.0f, 0.0f, 0.18f, 0.0f, 0.9f, 0.0f } },
        { "light", { 0.0f, 0.4f, 0.0f, 0.0f, 0.08f, 0.0f, 0.8f, 0.0f } },
        { "explosion", { 1.0f, 0.7f, 0.3f, 0.3f, 0.9f, 0.02f, 1.0f, 0.0f } },
        { "footstep", { 0.25f, 0.0f, 0.0f, 0.0f, 0.06f, 0.0f, 1.0f, 0.0f } },
        { "heartbeat", { 0.6f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.2f, 1.2f } },
        { "alarm", { 0.0f, 0.6f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 6.0f } },
        { "shoot", { 0.3f, 0.6f, 0.0f, 0.7f, 0.1f, 0.0f, 1.0f, 0.0f } },
        { "brake", { 0.0f, 0.0f, 0.6f, 0.0f, 0.15f, 0.0f, 0.6f, 0.0f } },
        { "engine", { 0.35f, 0.15f, 0.0f, 0.2f, 1.0f, 0.0f, 0.0f, 0.0f } },
        { "rain", { 0.0f, 0.15f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f } },
    } };
    return list;
}

} // namespace

bool hapticPreset(const std::string& name, float strength, HapticPulse& out) {
    for (const Preset& p : presets()) {
        if (name == p.name) {
            out = p.pulse;
            out.low *= strength;
            out.high *= strength;
            out.leftTrigger *= strength;
            out.rightTrigger *= strength;
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& hapticPresetNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const Preset& p : presets()) n.emplace_back(p.name);
        return n;
    }();
    return names;
}

float hapticEnvelope(const HapticPulse& p, float t) {
    if (t < 0.0f || t >= p.seconds || p.seconds <= 0.0f) return 0.0f;
    float e = 1.0f;
    if (p.attack > 0.0f && t < p.attack) e = t / p.attack;
    const float decayTime = std::clamp(p.decay, 0.0f, 1.0f) * p.seconds;
    const float fromEnd = p.seconds - t;
    if (decayTime > 0.0f && fromEnd < decayTime) e = std::min(e, fromEnd / decayTime);
    if (p.rate > 0.0f) e *= std::fmod(t * p.rate, 1.0f) < 0.35f ? 1.0f : 0.0f; // short beats
    return e;
}

void HapticMixer::play(const HapticPulse& pulse) {
    if (pulse.seconds <= 0.0f) return;
    m_pulses.push_back({ pulse, 0.0f });
    // A burst of hits shouldn't grow without bound: keep the newest 32.
    if (m_pulses.size() > 32) m_pulses.erase(m_pulses.begin());
}

void HapticMixer::setLevel(const std::string& name, float level, const HapticPulse& shape) {
    auto it = std::find_if(m_levels.begin(), m_levels.end(), [&](const Level& l) { return l.name == name; });
    if (level <= 0.0f) {
        if (it != m_levels.end()) m_levels.erase(it);
        return;
    }
    if (it == m_levels.end()) m_levels.push_back({ name, level, shape, 0.0f });
    else {
        it->level = level;
        it->shape = shape;
    }
}

void HapticMixer::stop() {
    m_pulses.clear();
    m_levels.clear();
}

HapticMix HapticMixer::update(float dt) {
    HapticMix m;
    for (Running& r : m_pulses) {
        const float e = hapticEnvelope(r.pulse, r.t);
        m.low += r.pulse.low * e;
        m.high += r.pulse.high * e;
        m.leftTrigger += r.pulse.leftTrigger * e;
        m.rightTrigger += r.pulse.rightTrigger * e;
        r.t += dt;
    }
    m_pulses.erase(std::remove_if(m_pulses.begin(), m_pulses.end(), [](const Running& r) { return r.t >= r.pulse.seconds; }), m_pulses.end());
    for (Level& l : m_levels) {
        // A level's own rate (alarm, heartbeat) loops over its length.
        const float e = l.shape.rate > 0.0f ? (std::fmod(l.t * l.shape.rate, 1.0f) < 0.35f ? 1.0f : 0.0f) : 1.0f;
        m.low += l.shape.low * l.level * e;
        m.high += l.shape.high * l.level * e;
        m.leftTrigger += l.shape.leftTrigger * l.level * e;
        m.rightTrigger += l.shape.rightTrigger * l.level * e;
        l.t += dt;
    }
    auto clip = [this](float v) { return std::clamp(v * m_scale, 0.0f, 1.0f); };
    m.low = clip(m.low);
    m.high = clip(m.high);
    m.leftTrigger = clip(m.leftTrigger);
    m.rightTrigger = clip(m.rightTrigger);
    return m;
}

} // namespace kke
