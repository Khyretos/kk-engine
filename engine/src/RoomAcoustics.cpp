#include "kke/RoomAcoustics.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<glm::vec3> fibonacciSphere(int count) {
    std::vector<glm::vec3> dirs;
    count = std::max(1, count);
    dirs.reserve(size_t(count));
    const float golden = glm::pi<float>() * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(count);
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float a = golden * float(i);
        dirs.emplace_back(std::cos(a) * r, y, std::sin(a) * r);
    }
    return dirs;
}

RoomAcoustics probeRoom(const glm::vec3& at, const AcousticRayFn& ray, const AudioMaterialTable& materials, const RoomProbeSettings& s) {
    RoomAcoustics room;
    if (!ray) return room;
    const std::vector<glm::vec3> dirs = fibonacciSphere(s.rays);
    int hits = 0, side = 0, sideHits = 0, up = 0, upHits = 0;
    float distSum = 0.0f, absorbSum = 0.0f;
    glm::vec3 openSum(0.0f);
    for (const glm::vec3& d : dirs) {
        const AcousticRay r = ray(at, d, s.maxDistance);
        const bool hit = r.hit && r.distance < s.maxDistance;
        const bool sideways = std::fabs(d.y) < 0.3f;
        const bool upward = d.y >= 0.35f;
        if (hit) {
            ++hits;
            distSum += r.distance;
            absorbSum += materials.get(r.material).absorption;
        } else {
            absorbSum += 1.0f; // sound that leaves never comes back
        }
        if (sideways) {
            ++side;
            // Ground further off isn't a wall: a sideways ray that ends
            // on a floor went out into the open.
            if (hit && r.normal.y < 0.7f) ++sideHits;
            else {
                const glm::vec3 flat = glm::normalize(glm::vec3(d.x, 0.0f, d.z));
                room.openings.push_back(flat);
                openSum += flat;
            }
        }
        if (upward) {
            ++up;
            if (hit) ++upHits;
        }
    }
    const float n = float(dirs.size());
    room.enclosure = float(hits) / n;
    room.walls = side ? float(sideHits) / float(side) : 0.0f;
    room.ceiling = up ? float(upHits) / float(up) : 0.0f;
    room.openness = side ? 1.0f - room.walls : 0.0f;
    room.meanDistance = hits ? distSum / float(hits) : s.maxDistance;
    room.absorption = std::max(s.minAbsorption, absorbSum / n);
    // Sabine's RT60 = 0.161 V / (S a); for a room of "radius" r (V/S = r/3)
    // that is 0.0537 r / a.
    room.rt60 = std::clamp(0.0537f * room.meanDistance / room.absorption, 0.05f, 4.0f);
    // Reverb needs something to bounce between: walls matter, a roof matters
    // more (an alley rings a little, a hall a lot, a field not at all).
    room.wet = std::clamp(0.4f * room.ceiling * (0.4f + 0.6f * room.walls) + 0.1f * room.walls * room.walls, 0.0f, 0.5f);
    room.damping = std::clamp(0.15f + (absorbSum / n) * 0.8f, 0.1f, 0.8f);
    room.preDelay = std::min(0.08f, room.meanDistance / 343.0f);
    if (glm::length(openSum) > 1e-4f) room.openingDir = glm::normalize(openSum);
    return room;
}

// ------------------------------------------------------------------ Reverb
namespace {
// Freeverb's tunings, in samples at 44.1 kHz; scaled to the actual rate.
const int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
const int kAllPassTuning[4] = {556, 441, 341, 225};
const int kStereoSpread = 23;
constexpr float kFixedGain = 0.015f;
constexpr float kAllPassFeedback = 0.5f;
} // namespace

Reverb::Reverb(int sampleRate) : m_rate(std::max(8000, sampleRate)) {
    const float scale = float(m_rate) / 44100.0f;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 8; ++i) m_comb[ch][i].buf.assign(size_t(float(kCombTuning[i] + ch * kStereoSpread) * scale), 0.0f);
        for (int i = 0; i < 4; ++i) m_allpass[ch][i].buf.assign(size_t(float(kAllPassTuning[i] + ch * kStereoSpread) * scale), 0.0f);
    }
    m_pre.assign(size_t(0.1f * float(m_rate)) + 1, 0.0f);
    updateFeedback();
}

void Reverb::clear() {
    for (auto& ch : m_comb)
        for (Comb& c : ch) {
            std::fill(c.buf.begin(), c.buf.end(), 0.0f);
            c.store = 0.0f;
        }
    for (auto& ch : m_allpass)
        for (AllPass& a : ch) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    std::fill(m_pre.begin(), m_pre.end(), 0.0f);
}

void Reverb::setRoom(float rt60, float damping, float wet, float preDelay) {
    m_targetRt60 = std::clamp(rt60, 0.05f, 8.0f);
    m_targetDamp = std::clamp(damping, 0.0f, 0.95f);
    m_targetWet = std::clamp(wet, 0.0f, 1.0f);
    m_targetPre = std::clamp(preDelay, 0.0f, 0.099f);
}

void Reverb::updateFeedback() {
    for (auto& ch : m_comb)
        for (Comb& c : ch) {
            // A comb of delay d seconds loses 20 log10(g) dB per pass:
            // 60 dB after RT60 means g = 10^(-3 d / RT60).
            const float d = float(c.buf.size()) / float(m_rate);
            c.feedback = std::min(0.985f, std::pow(10.0f, -3.0f * d / m_rt60));
        }
}

void Reverb::process(const float* in, float* out, int frames) {
    // Glide to the targets once per block (a block is ~10 ms).
    const float k = 0.3f;
    m_rt60 += (m_targetRt60 - m_rt60) * k;
    m_damp += (m_targetDamp - m_damp) * k;
    const float wetFrom = m_wet;
    m_wet += (m_targetWet - m_wet) * k;
    m_preDelay += (m_targetPre - m_preDelay) * k;
    updateFeedback();
    if (wetFrom < 1e-5f && m_wet < 1e-5f) return; // dry room: nothing to add (the tail has died with the wet level)
    const size_t preN = m_pre.size();
    const size_t delay = std::min(preN - 1, size_t(m_preDelay * float(m_rate)));
    const float damp1 = m_damp, damp2 = 1.0f - m_damp;
    for (int f = 0; f < frames; ++f) {
        m_pre[m_preIdx] = in[f];
        const float x = m_pre[(m_preIdx + preN - delay) % preN] * kFixedGain;
        m_preIdx = (m_preIdx + 1) % preN;
        const float wet = wetFrom + (m_wet - wetFrom) * (float(f) / float(frames));
        for (int ch = 0; ch < 2; ++ch) {
            float acc = 0.0f;
            for (Comb& c : m_comb[ch]) {
                const float y = c.buf[c.idx];
                c.store = y * damp2 + c.store * damp1;
                c.buf[c.idx] = x + c.store * c.feedback;
                if (++c.idx >= c.buf.size()) c.idx = 0;
                acc += y;
            }
            for (AllPass& a : m_allpass[ch]) {
                const float b = a.buf[a.idx];
                const float y = -acc + b;
                a.buf[a.idx] = acc + b * kAllPassFeedback;
                if (++a.idx >= a.buf.size()) a.idx = 0;
                acc = y;
            }
            out[2 * f + ch] += acc * wet;
        }
    }
}

} // namespace kke
