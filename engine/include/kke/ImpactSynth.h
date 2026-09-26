#pragma once

#include "kke/AudioMixer.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

struct Material;

// How a material sounds when hit, and how much sound gets through it.
//
// Impacts are made by *modal synthesis*: a struck object rings at a few
// resonant frequencies (its modes), each a decaying sine, plus a short
// burst of noise from the contact itself. The mode ratios are what makes
// metal sound like metal: a free metal bar rings at 1 : 2.76 : 5.40 : 8.93
// (inharmonic, bell-like, slow decay), wood's modes die in ~0.1 s, stone
// is mostly the noise burst, rubber is a low thud. Different ratios,
// decays and noise colours are what let a blind player tell materials
// apart by ear (docs/AUDIO.md "Accessibility"), with no sample library at all.
struct AudioMaterial {
    std::string name = "Default";
    float baseFrequency = 520.0f;             // Hz of the lowest mode
    std::vector<float> modeRatios{1.0f, 2.2f, 3.9f};
    float decay = 0.09f;                      // seconds for the lowest mode to fall 60 dB
    float decayTilt = 1.0f;                   // mode i decays (1 + tilt*i) times faster
    float brightness = 0.6f;                  // amplitude ratio between neighbouring modes
    float noise = 0.3f;                       // contact-noise level relative to the modes
    float noiseDecay = 0.006f;                // seconds (time constant)
    float noiseCutoff = 6000.0f;              // Hz; darker noise = softer material
    float transmission = 0.4f;                // occlusion: fraction of sound through a wall of this, 0..1
};

// Material ids -> AudioMaterial. The ids are the same ones RigidWorld
// bodies carry (BodyDesc::material) and contacts report. Unknown ids fall
// back to id 0. Games register their own on top of the defaults.
class AudioMaterialTable {
public:
    enum Id : uint32_t { Default = 0, Stone = 1, Wood = 2, Metal = 3, Glass = 4, Rubber = 5, Dirt = 6, Plastic = 7 };

    AudioMaterialTable(); // with the defaults above
    void set(uint32_t id, const AudioMaterial& m) { m_materials[id] = m; }
    const AudioMaterial& get(uint32_t id) const;
    bool has(uint32_t id) const { return m_materials.count(id) != 0; }
    const std::map<uint32_t, AudioMaterial>& all() const { return m_materials; }

private:
    std::map<uint32_t, AudioMaterial> m_materials;
};

struct ImpactParams {
    float intensity = 0.5f;   // 0..1: how hard (from contact speed); louder AND brighter
    float size = 1.0f;        // relative object size: bigger rings lower (f ~ 1/size)
    uint32_t seed = 1;        // same seed, same sound (replays, networking)
};

// One impact as mono PCM. Pure function: same inputs, same samples.
SoundBuffer synthesizeImpact(const AudioMaterial& material, const ImpactParams& params, int sampleRate = 48000);

// The audio material for a physics Material (its audioMaterial, or a
// guess from density/stiffness/metallic/roughness when that's -1).
uint32_t audioMaterialFor(const Material& m);

// Maps contact speed (m/s) to impact intensity 0..1, 0 below `threshold`.
float impactIntensity(float speed, float threshold = 0.8f, float full = 9.0f);

// A cache of synthesized impacts: per material, a few intensity levels x a
// few random variants, made the first time they're asked for (~0.1-1 ms
// each). Repeated hits then cost nothing but a lookup, and the variants
// keep a pile of crates from sounding like a machine gun.
class ImpactBank {
public:
    ImpactBank(const AudioMaterialTable& table, int sampleRate = 48000, int levels = 4, int variants = 4);

    SoundHandle get(uint32_t material, float intensity, uint32_t seed);
    size_t cachedCount() const { return m_cache.size(); }
    size_t cachedBytes() const;
    void clear() { m_cache.clear(); }

private:
    const AudioMaterialTable& m_table;
    int m_sampleRate, m_levels, m_variants;
    std::unordered_map<uint64_t, SoundHandle> m_cache;
};

} // namespace kke
