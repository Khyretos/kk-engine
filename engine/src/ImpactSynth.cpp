#include "kke/ImpactSynth.h"

#include "kke/Material.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

AudioMaterialTable::AudioMaterialTable() {
    // Mode ratios: metal = free bar (1, 2.76, 5.40, 8.93, 13.34); glass =
    // thin plate/cup; wood and stone = measured-ish plank/slab ratios with
    // heavy damping. Tuned by ear against the tests' separation checks,
    // not taken from a paper: easy to change, see docs/AUDIO.md.
    AudioMaterial def;
    m_materials[Default] = def;

    AudioMaterial stone;
    stone.name = "Stone";
    stone.baseFrequency = 260.0f;
    stone.modeRatios = {1.0f, 1.74f, 2.9f, 4.3f};
    stone.decay = 0.06f;
    stone.decayTilt = 1.5f;
    stone.brightness = 0.5f;
    stone.noise = 0.9f;
    stone.noiseDecay = 0.018f;
    stone.noiseCutoff = 5000.0f;
    stone.transmission = 0.1f;
    m_materials[Stone] = stone;

    AudioMaterial wood;
    wood.name = "Wood";
    wood.baseFrequency = 190.0f;
    wood.modeRatios = {1.0f, 2.57f, 4.2f, 6.1f};
    wood.decay = 0.12f;
    wood.decayTilt = 1.2f;
    wood.brightness = 0.55f;
    wood.noise = 0.5f;
    wood.noiseDecay = 0.012f;
    wood.noiseCutoff = 3500.0f;
    wood.transmission = 0.35f;
    m_materials[Wood] = wood;

    AudioMaterial metal;
    metal.name = "Metal";
    metal.baseFrequency = 420.0f;
    metal.modeRatios = {1.0f, 2.76f, 5.40f, 8.93f, 13.34f};
    metal.decay = 1.4f;
    metal.decayTilt = 0.3f;
    metal.brightness = 0.8f;
    metal.noise = 0.15f;
    metal.noiseDecay = 0.004f;
    metal.noiseCutoff = 8000.0f;
    metal.transmission = 0.15f;
    m_materials[Metal] = metal;

    AudioMaterial glass;
    glass.name = "Glass";
    glass.baseFrequency = 1600.0f;
    glass.modeRatios = {1.0f, 2.32f, 4.25f, 6.63f, 9.38f};
    glass.decay = 0.7f;
    glass.decayTilt = 0.4f;
    glass.brightness = 0.85f;
    glass.noise = 0.35f;
    glass.noiseDecay = 0.003f;
    glass.noiseCutoff = 12000.0f;
    glass.transmission = 0.5f;
    m_materials[Glass] = glass;

    AudioMaterial rubber;
    rubber.name = "Rubber";
    rubber.baseFrequency = 90.0f;
    rubber.modeRatios = {1.0f, 1.5f};
    rubber.decay = 0.05f;
    rubber.decayTilt = 2.0f;
    rubber.brightness = 0.3f;
    rubber.noise = 0.15f;
    rubber.noiseDecay = 0.02f;
    rubber.noiseCutoff = 600.0f;
    rubber.transmission = 0.3f;
    m_materials[Rubber] = rubber;

    AudioMaterial dirt;
    dirt.name = "Dirt";
    dirt.baseFrequency = 70.0f;
    dirt.modeRatios = {1.0f};
    dirt.decay = 0.03f;
    dirt.decayTilt = 1.0f;
    dirt.brightness = 0.3f;
    dirt.noise = 1.0f;
    dirt.noiseDecay = 0.04f;
    dirt.noiseCutoff = 1500.0f;
    dirt.transmission = 0.05f;
    m_materials[Dirt] = dirt;

    AudioMaterial plastic = def;
    plastic.name = "Plastic";
    m_materials[Plastic] = plastic;
}

const AudioMaterial& AudioMaterialTable::get(uint32_t id) const {
    auto it = m_materials.find(id);
    if (it != m_materials.end()) return it->second;
    return m_materials.at(Default);
}

namespace {
// xorshift32: tiny, deterministic on every platform (no std:: distributions,
// whose output differs between standard libraries).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return float(next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float signedUnit() { return unit() * 2.0f - 1.0f; }                   // -1..1
};
} // namespace

uint32_t audioMaterialFor(const Material& m) {
    if (m.audioMaterial >= 0) return uint32_t(m.audioMaterial);
    if (m.metallic > 0.5f) return AudioMaterialTable::Metal;
    if (m.roughness < 0.15f && m.stiffness >= 1.0e7f) return AudioMaterialTable::Glass;
    if (m.stiffness < 1.0e6f) return AudioMaterialTable::Rubber;
    if (m.density >= 1800.0f) return AudioMaterialTable::Stone;
    if (m.density < 1000.0f) return AudioMaterialTable::Wood;
    return AudioMaterialTable::Default;
}

float impactIntensity(float speed, float threshold, float full) {
    if (speed <= threshold) return 0.0f;
    return std::clamp((speed - threshold) / std::max(0.01f, full - threshold), 0.0f, 1.0f);
}

SoundBuffer synthesizeImpact(const AudioMaterial& m, const ImpactParams& p, int sampleRate) {
    SoundBuffer out;
    out.sampleRate = sampleRate;
    const float intensity = std::clamp(p.intensity, 0.0f, 1.0f);
    const float size = std::max(0.05f, p.size);
    Rng rng(p.seed * 2654435761u + 0x1234567u);

    const float ln1000 = std::log(1000.0f); // 60 dB
    const float decay0 = std::max(0.005f, m.decay) * std::sqrt(size);
    float length = std::max(decay0, m.noiseDecay * 7.0f) + 0.01f;
    length = std::min(length, 2.0f);
    const size_t n = size_t(length * float(sampleRate));
    out.samples.assign(n, 0.0f);
    if (n == 0) return out;

    // Harder hits excite the higher modes more: that's why a hard knock
    // sounds brighter, not just louder.
    const float bright = m.brightness * (0.6f + 0.4f * intensity);
    const double nyquistGuard = 0.45 * double(sampleRate);
    float amp = 1.0f;
    for (size_t i = 0; i < m.modeRatios.size(); ++i, amp *= bright) {
        const double f = double(m.baseFrequency) / double(size) * double(m.modeRatios[i]) * (1.0 + 0.03 * rng.signedUnit());
        const float a = amp * (1.0f + 0.25f * rng.signedUnit());
        const float phase = rng.unit() * glm::two_pi<float>();
        if (f >= nyquistGuard || f <= 0.0) continue;
        const float decayI = decay0 / (1.0f + m.decayTilt * float(i));
        const double w = glm::two_pi<double>() * f / double(sampleRate);
        // Recursive sine oscillator: one multiply-add per sample instead of
        // a sin() call. y[k] = 2cos(w) y[k-1] - y[k-2].
        const double c = 2.0 * std::cos(w);
        double y1 = std::sin(phase - w), y2 = std::sin(phase - 2.0 * w);
        const double envStep = std::exp(-double(ln1000) / (double(decayI) * double(sampleRate)));
        double env = a;
        for (size_t k = 0; k < n && env > 1e-5; ++k) {
            const double y = c * y1 - y2;
            y2 = y1;
            y1 = y;
            out.samples[k] += float(y * env);
            env *= envStep;
        }
    }

    // Contact noise: white noise through two one-pole low-passes (-12 dB
    // per octave: one alone leaves soft materials hissing), decaying fast.
    if (m.noise > 0.0f) {
        const float cutoff = m.noiseCutoff * (0.5f + 0.5f * intensity);
        const float lp = 1.0f - std::exp(-glm::two_pi<float>() * cutoff / float(sampleRate));
        const float envStep = std::exp(-1.0f / (std::max(0.0005f, m.noiseDecay) * float(sampleRate)));
        float env = m.noise, s1 = 0.0f, s2 = 0.0f;
        for (size_t k = 0; k < n && env > 1e-5f; ++k) {
            s1 += lp * (rng.signedUnit() - s1);
            s2 += lp * (s1 - s2);
            out.samples[k] += s2 * env * 3.0f; // the low-passes lose level; roughly restore it
            env *= envStep;
        }
    }

    // 0.5 ms fade-in (no click from a mode starting mid-cycle), then scale
    // the peak to the intensity.
    const size_t ramp = std::min(n, size_t(sampleRate / 2000));
    for (size_t k = 0; k < ramp; ++k) out.samples[k] *= float(k) / float(ramp);
    float peak = 0.0f;
    for (float s : out.samples) peak = std::max(peak, std::fabs(s));
    if (peak > 0.0f) {
        const float target = 0.15f + 0.85f * intensity;
        const float g = target / peak;
        for (float& s : out.samples) s *= g;
    }
    // Trim the silent tail.
    size_t last = n;
    while (last > 0 && std::fabs(out.samples[last - 1]) < 1e-4f) --last;
    out.samples.resize(std::max<size_t>(last, 1));
    return out;
}

ImpactBank::ImpactBank(const AudioMaterialTable& table, int sampleRate, int levels, int variants)
    : m_table(table), m_sampleRate(sampleRate), m_levels(std::max(1, levels)), m_variants(std::max(1, variants)) {}

SoundHandle ImpactBank::get(uint32_t material, float intensity, uint32_t seed) {
    const int level = std::clamp(int(std::clamp(intensity, 0.0f, 1.0f) * float(m_levels)), 0, m_levels - 1);
    const int variant = int(seed % uint32_t(m_variants));
    const uint64_t key = (uint64_t(material) << 32) | (uint64_t(level) << 16) | uint64_t(variant);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;
    ImpactParams p;
    p.intensity = (float(level) + 0.75f) / float(m_levels);
    p.seed = uint32_t(key * 0x9E3779B97F4A7C15ull >> 32) + 1u;
    auto buf = std::make_shared<SoundBuffer>(synthesizeImpact(m_table.get(material), p, m_sampleRate));
    m_cache.emplace(key, buf);
    return buf;
}

size_t ImpactBank::cachedBytes() const {
    size_t b = 0;
    for (const auto& [k, v] : m_cache) b += v->samples.size() * sizeof(float);
    return b;
}

} // namespace kke
