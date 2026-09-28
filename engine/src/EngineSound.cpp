#include "kke/EngineSound.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
constexpr float kPi = 3.14159265358979f;
}

EngineSound::Params EngineSound::inline4() {
    Params p;
    p.cylinders = 4;
    p.idleRpm = 850.0f;
    p.maxRpm = 7200.0f;
    p.exhaustHz = 190.0f;
    p.unevenness = 0.02f;
    p.roughness = 0.15f;
    p.brightness = 0.7f;
    p.intakeNoise = 0.35f;
    return p;
}

EngineSound::Params EngineSound::inline6() {
    Params p;
    p.cylinders = 6;
    p.idleRpm = 800.0f;
    p.maxRpm = 7500.0f;
    p.exhaustHz = 165.0f;
    p.unevenness = 0.01f;
    p.roughness = 0.1f;
    p.brightness = 0.6f;
    p.intakeNoise = 0.3f;
    return p;
}

EngineSound::Params EngineSound::v8() {
    Params p;
    p.cylinders = 8;
    p.idleRpm = 750.0f;
    p.maxRpm = 7000.0f;
    p.exhaustHz = 110.0f;
    p.unevenness = 0.18f; // cross-plane crank: the burble
    p.roughness = 0.35f;
    p.brightness = 0.45f;
    p.intakeNoise = 0.25f;
    p.pops = 0.8f;
    return p;
}

EngineSound::Params EngineSound::v10() {
    Params p;
    p.cylinders = 10;
    p.idleRpm = 1000.0f;
    p.maxRpm = 8500.0f;
    p.exhaustHz = 230.0f;
    p.unevenness = 0.04f;
    p.roughness = 0.12f;
    p.brightness = 0.85f;
    p.intakeNoise = 0.4f;
    p.pops = 0.6f;
    return p;
}

void EngineSound::Biquad::bandPass(float hz, float q, int sampleRate) {
    const float w = 2.0f * kPi * std::clamp(hz, 10.0f, 0.45f * static_cast<float>(sampleRate)) / static_cast<float>(sampleRate);
    const float alpha = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -alpha / a0;
    a1 = -2.0f * std::cos(w) / a0;
    a2 = (1.0f - alpha) / a0;
}

void EngineSound::Biquad::lowPass(float hz, float q, int sampleRate) {
    const float w = 2.0f * kPi * std::clamp(hz, 10.0f, 0.45f * static_cast<float>(sampleRate)) / static_cast<float>(sampleRate);
    const float alpha = std::sin(w) / (2.0f * q);
    const float c = std::cos(w);
    const float a0 = 1.0f + alpha;
    b0 = (1.0f - c) * 0.5f / a0;
    b1 = (1.0f - c) / a0;
    b2 = b0;
    a1 = -2.0f * c / a0;
    a2 = (1.0f - alpha) / a0;
}

EngineSound::EngineSound(const Params& p) { setParams(p); }

void EngineSound::setParams(const Params& p) {
    m_p = p;
    m_p.cylinders = std::clamp(m_p.cylinders, 1, 16);
    m_rng = p.seed * 2654435761u + 1u;
    // Each cylinder its own: a little louder or softer, a little early or late.
    for (int c = 0; c < 16; ++c) {
        m_cylGain[c] = noise();
        m_cylTime[c] = noise();
    }
    m_filterRate = 0; // new pipe: filters again
    m_rpmNow = m_rpm = std::max(m_rpm, m_p.idleRpm);
}

float EngineSound::noise() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng) * (2.0f / 4294967295.0f) - 1.0f;
}

void EngineSound::set(float rpm, float throttle, float slide, float speed) {
    m_rpm = std::clamp(rpm, m_p.idleRpm * 0.8f, m_p.maxRpm * 1.05f);
    m_throttle = std::clamp(throttle, 0.0f, 1.0f);
    m_slide = std::clamp(slide, 0.0f, 1.0f);
    m_speed = std::max(0.0f, speed);
}

void EngineSound::render(float* out, size_t count, int sampleRate) {
    if (count == 0 || sampleRate <= 0) return;
    const float sr = static_cast<float>(sampleRate);
    if (m_filterRate != sampleRate) {
        m_filterRate = sampleRate;
        m_pipe1.bandPass(m_p.exhaustHz, 2.5f, sampleRate);
        m_pipe2.bandPass(m_p.exhaustHz * 2.7f, 3.5f, sampleRate);
        m_intake.bandPass(1800.0f, 0.8f, sampleRate);
        m_rumble.lowPass(90.0f, 0.7f, sampleRate);
        m_squeal.bandPass(1000.0f, 12.0f, sampleRate);
        m_filterBright = -1.0f;
    }
    // The block's tone follows the throttle (set once per render: smooth enough).
    const float bright = 350.0f + (1200.0f + 3500.0f * m_p.brightness) * std::max(m_throttle, m_throttleNow);
    if (std::fabs(bright - m_filterBright) > 20.0f) {
        m_filterBright = bright;
        m_block.lowPass(bright, 0.8f, sampleRate);
    }
    const float cyl = static_cast<float>(m_p.cylinders);
    const float span = std::max(1.0f, m_p.maxRpm - m_p.idleRpm);
    const float popDecay = std::exp(-1.0f / (0.004f * sr));
    const float rpm0 = m_rpmNow, thr0 = m_throttleNow, sl0 = m_slideNow, sp0 = m_speedNow;
    const float inv = 1.0f / static_cast<float>(count);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i + 1) * inv;
        const float rpm = rpm0 + (m_rpm - rpm0) * t;
        const float thr = thr0 + (m_throttle - thr0) * t;
        const float slide = sl0 + (m_slide - sl0) * t;
        const float speed = sp0 + (m_speed - sp0) * t;
        const float revs = std::clamp((rpm - m_p.idleRpm) / span, 0.0f, 1.0f);

        // Firings: revs/60 turns a second, half the cylinders fire each turn.
        const float fire = rpm / 60.0f * cyl * 0.5f;
        m_phase += fire / sr / (1.0f + m_p.unevenness * m_cylTime[m_cylinder]);
        if (m_phase >= 1.0) {
            m_phase -= 1.0;
            m_cylinder = (m_cylinder + 1) % m_p.cylinders;
            m_pulse = (0.5f + 0.5f * thr) * (1.0f + m_p.roughness * m_cylGain[m_cylinder]);
            // Off the throttle, high revs: unburnt fuel pops in the pipe.
            if (thr < 0.08f && revs > 0.45f && noise() > 1.0f - 0.06f * m_p.pops) m_pop = 0.8f + 0.6f * std::fabs(noise());
        }
        // Each pulse dies away over a fifth of the time to the next.
        m_pulse *= std::exp(-fire * 5.0f / sr);
        m_pop *= popDecay;
        const float excite = m_pulse + m_pop * noise();
        const float pipe = m_pipe1.run(excite) * 1.6f + m_pipe2.run(excite) * 0.7f;
        const float body = m_block.run(excite) * 0.9f;
        const float intake = m_intake.run(noise()) * m_p.intakeNoise * thr * (0.3f + revs) * (0.3f + m_pulse);
        const float engine = std::tanh((pipe + body + intake) * (1.2f + 1.3f * thr)) * (0.45f + 0.35f * thr + 0.2f * revs);

        // Tyres: a wavering squeal when they slide, rumble with speed.
        if ((i & 63) == 0 && slide > 0.01f) {
            m_squealWobble += 64.0f / sr * 7.0f * 2.0f * kPi;
            if (m_squealWobble > 2.0f * kPi) m_squealWobble -= 2.0f * kPi;
            m_squeal.bandPass(900.0f + 250.0f * slide + 120.0f * std::sin(m_squealWobble), 14.0f, sampleRate);
        }
        const float squeal = slide > 0.01f ? m_squeal.run(noise()) * slide * slide * 3.0f : 0.0f;
        const float rumble = m_rumble.run(noise()) * std::min(speed / 40.0f, 1.0f) * 0.6f;
        out[i] = (engine * 0.5f + squeal + rumble) * m_p.gain;
    }
    m_rpmNow = m_rpm;
    m_throttleNow = m_throttle;
    m_slideNow = m_slide;
    m_speedNow = m_speed;
}

} // namespace kke
