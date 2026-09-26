#include "kke/AudioMixer.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

const char* soundCategoryName(SoundCategory c) {
    switch (c) {
        case SoundCategory::Impact: return "Impact";
        case SoundCategory::Footstep: return "Footstep";
        case SoundCategory::Voice: return "Voice";
        case SoundCategory::Ambient: return "Ambient";
        case SoundCategory::Ui: return "UI";
        case SoundCategory::Music: return "Music";
        case SoundCategory::Alert: return "Alert";
        default: return "?";
    }
}

AudioMixer::AudioMixer(int sampleRate, int maxVoices)
    : m_sampleRate(std::max(8000, sampleRate)), m_maxVoices(std::max(1, maxVoices)) {
    m_voices.reserve(size_t(m_maxVoices));
}

AudioMixer::Spatial AudioMixer::spatialize(const Listener& l, const glm::vec3& pos, float minDistance, float maxDistance) {
    Spatial s;
    glm::vec3 d = pos - l.position;
    s.distance = glm::length(d);
    minDistance = std::max(0.01f, minDistance);
    maxDistance = std::max(minDistance + 0.01f, maxDistance);
    // Inverse distance (-6 dB per doubling past minDistance), then a fade
    // over the last 20% so a sound doesn't cut off at maxDistance.
    s.gain = s.distance <= minDistance ? 1.0f : minDistance / s.distance;
    const float fadeStart = maxDistance * 0.8f;
    if (s.distance >= maxDistance) s.gain = 0.0f;
    else if (s.distance > fadeStart) s.gain *= 1.0f - (s.distance - fadeStart) / (maxDistance - fadeStart);
    if (s.distance < 1e-4f) return s;

    glm::vec3 fwd = l.forward;
    if (glm::dot(fwd, fwd) < 1e-12f) fwd = glm::vec3(0, 0, -1);
    fwd = glm::normalize(fwd);
    glm::vec3 right = glm::cross(fwd, l.up);
    if (glm::dot(right, right) < 1e-12f) right = glm::vec3(1, 0, 0);
    right = glm::normalize(right);
    const glm::vec3 dir = d / s.distance;
    const float x = glm::dot(dir, right), z = glm::dot(dir, fwd);
    s.azimuth = std::atan2(x, z);
    s.pan = std::clamp(x, -1.0f, 1.0f);
    s.behind = z < 0.0f;
    return s;
}

float AudioMixer::estimate(const VoiceDesc& d) const {
    float g = d.gain * masterGain * categoryGain[size_t(d.category)] * d.priority;
    if (d.spatial) g *= spatialize(m_listener, d.position, d.minDistance, d.maxDistance).gain;
    return g;
}

uint32_t AudioMixer::play(const VoiceDesc& desc) {
    if (!desc.sound || desc.sound->samples.empty() || desc.sound->sampleRate <= 0) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    const float est = estimate(desc);
    if (est <= 1e-5f) { ++m_dropped; return 0; }
    Voice v;
    v.id = m_nextId++;
    if (m_nextId == 0) m_nextId = 1;
    v.desc = desc;
    v.estLoudness = est;
    if (int(m_voices.size()) >= m_maxVoices) {
        size_t quietest = 0;
        float q = 1e30f;
        for (size_t i = 0; i < m_voices.size(); ++i) {
            const Voice& o = m_voices[i];
            const float l = (o.started ? o.lastPeak : o.estLoudness) * o.desc.priority;
            if (l < q) { q = l; quietest = i; }
        }
        if (est <= q) { ++m_dropped; return 0; }
        m_voices[quietest] = v;
        ++m_stolen;
        return v.id;
    }
    m_voices.push_back(v);
    return v.id;
}

void AudioMixer::stop(uint32_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Voice& v) { return v.id == id; }), m_voices.end());
}

void AudioMixer::stopAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.clear();
}

bool AudioMixer::isPlaying(uint32_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Voice& v : m_voices) if (v.id == id) return true;
    return false;
}

void AudioMixer::setPosition(uint32_t id, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Voice& v : m_voices) if (v.id == id) v.desc.position = position;
}

void AudioMixer::setTransmission(uint32_t id, float transmission) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Voice& v : m_voices) if (v.id == id) v.transmission = std::clamp(transmission, 0.0f, 1.0f);
}

void AudioMixer::setListener(const Listener& l) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listener = l;
}

Listener AudioMixer::listener() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_listener;
}

size_t AudioMixer::voiceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_voices.size();
}

std::vector<ActiveSound> AudioMixer::activeSounds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ActiveSound> out;
    out.reserve(m_voices.size());
    for (const Voice& v : m_voices) {
        ActiveSound a;
        a.id = v.id;
        a.position = v.desc.position;
        a.spatial = v.desc.spatial;
        a.loudness = v.started ? v.lastPeak : v.estLoudness;
        a.transmission = v.transmission;
        a.category = v.desc.category;
        a.material = v.desc.material;
        if (v.desc.spatial) {
            Spatial s = spatialize(m_listener, v.desc.position, v.desc.minDistance, v.desc.maxDistance);
            a.azimuth = s.azimuth;
            a.distance = s.distance;
        }
        out.push_back(a);
    }
    return out;
}

namespace {
// A soft knee above 0.9 instead of hard clipping: many impacts at once
// get squashed rather than crackle.
inline float softLimit(float x) {
    const float a = std::fabs(x);
    if (a <= 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
    return x < 0.0f ? -y : y;
}
} // namespace

void AudioMixer::mix(float* out, int frames) {
    if (frames <= 0) return;
    std::fill(out, out + size_t(frames) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    const float twoPiOverRate = glm::two_pi<float>() / float(m_sampleRate);
    for (Voice& v : m_voices) {
        const SoundBuffer& buf = *v.desc.sound;
        float gain = v.desc.gain * masterGain * categoryGain[size_t(v.desc.category)];
        float pan = 0.0f;
        bool behind = false;
        if (v.desc.spatial) {
            Spatial s = spatialize(m_listener, v.desc.position, v.desc.minDistance, v.desc.maxDistance);
            gain *= s.gain;
            pan = s.pan;
            behind = s.behind;
        }
        // Occlusion: less energy through, and walls eat the highs first.
        const float t = v.transmission;
        gain *= t;
        float cutoff = 400.0f + 17600.0f * t * t;
        if (behind) cutoff = std::min(cutoff, 7000.0f); // head shadow: the cheapest front/back cue
        const float a = 1.0f - std::exp(-twoPiOverRate * cutoff);
        const float angle = (pan + 1.0f) * glm::quarter_pi<float>();
        const float gl = gain * std::cos(angle), gr = gain * std::sin(angle);
        if (!v.started) { v.prevGainL = gl; v.prevGainR = gr; v.started = true; }

        const double step = double(buf.sampleRate) / double(m_sampleRate);
        const size_t n = buf.samples.size();
        double pos = double(v.cursor) / 65536.0;
        float peak = 0.0f;
        const float invFrames = 1.0f / float(frames);
        for (int f = 0; f < frames; ++f) {
            size_t i0 = size_t(pos);
            if (i0 >= n) {
                if (!v.desc.loop) break;
                pos = std::fmod(pos, double(n));
                i0 = size_t(pos);
            }
            const float frac = float(pos - double(i0));
            const size_t i1 = i0 + 1 < n ? i0 + 1 : (v.desc.loop ? 0 : i0);
            const float x = buf.samples[i0] + (buf.samples[i1] - buf.samples[i0]) * frac;
            v.lpState += a * (x - v.lpState);
            const float k = float(f) * invFrames;
            const float l = v.prevGainL + (gl - v.prevGainL) * k;
            const float r = v.prevGainR + (gr - v.prevGainR) * k;
            out[2 * f] += v.lpState * l;
            out[2 * f + 1] += v.lpState * r;
            peak = std::max(peak, std::fabs(v.lpState) * std::max(l, r));
            pos += step;
        }
        v.cursor = size_t(pos * 65536.0);
        v.prevGainL = gl;
        v.prevGainR = gr;
        v.lastPeak = peak;
    }
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [](const Voice& v) { return !v.desc.loop && double(v.cursor) / 65536.0 >= double(v.desc.sound->samples.size()); }),
                   m_voices.end());
    for (int i = 0; i < frames * 2; ++i) out[i] = softLimit(out[i]);
}

} // namespace kke
