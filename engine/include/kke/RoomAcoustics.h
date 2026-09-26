#pragma once

#include "kke/ImpactSynth.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

// Ray-traced room acoustics (docs/AUDIO.md "Reverb and openings"): what the
// listener's surroundings sound like, measured with a few dozen rays the
// way WhoStoleMyCoffee/raytraced-audio (MIT) does it in Godot: rays that
// hit something close all around mean a small, live room; rays that escape
// sideways are openings (doors, windows), and sounds from outside come in
// through them; nothing overhead means outdoors, where there is next to no
// reverb whatever the ground is made of. Pure logic with a ray callback
// (Jolt in the engine, a fake in tests/test_room_acoustics.cpp).

struct AcousticRay {
    bool hit = false;
    float distance = 0.0f;
    uint32_t material = 0;   // AudioMaterialTable id of what was hit
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // of the surface hit: floors aren't walls
};
using AcousticRayFn = std::function<AcousticRay(const glm::vec3& from, const glm::vec3& dir, float maxDistance)>;

struct RoomAcoustics {
    float enclosure = 0.0f;     // 0..1, share of all rays that hit something
    float walls = 0.0f;         // 0..1, share of the sideways rays that hit (walls around)
    float ceiling = 0.0f;       // 0..1, share of the upward rays that hit (a roof)
    float meanDistance = 0.0f;  // m, average distance of the hits (room size)
    float absorption = 1.0f;    // average per-bounce absorption; an escaping ray counts as 1
    float rt60 = 0.0f;          // s, time for the reverb to die away by 60 dB
    float wet = 0.0f;           // 0..~0.5, reverb level
    float damping = 0.3f;       // 0..1, how fast the tail loses its highs (soft rooms: more)
    float preDelay = 0.0f;      // s, before the first reflections
    float openness = 0.0f;      // 0..1, share of the sideways rays that escaped
    glm::vec3 openingDir{0.0f}; // unit, the average escaping sideways direction (0 if none)
    std::vector<glm::vec3> openings; // each escaping sideways direction (unit)
};

struct RoomProbeSettings {
    int rays = 32;               // spread evenly over the sphere
    float maxDistance = 30.0f;   // m; a ray that goes further escaped
    float minAbsorption = 0.12f; // real rooms have furniture and people: never a perfect echo chamber
};

// Evenly spread unit directions (a Fibonacci sphere): same set every time.
std::vector<glm::vec3> fibonacciSphere(int count);

RoomAcoustics probeRoom(const glm::vec3& at, const AcousticRayFn& ray, const AudioMaterialTable& materials,
                        const RoomProbeSettings& settings = {});

// A Freeverb-style reverb (Jezar's public-domain algorithm): per channel 8
// damped feedback combs in parallel, then 4 all-passes in series, the right
// channel's delays spread a little for width. Each comb's feedback is set
// from the room's RT60 (g = 10^(-3 d / RT60)), so the tail lasts as long as
// the room says. Mono send in, stereo wet added to the output.
class Reverb {
public:
    explicit Reverb(int sampleRate = 48000);
    // Targets; the reverb glides to them over a block (no zipper noise).
    void setRoom(float rt60, float damping, float wet, float preDelay);
    // `in`: the mono send, `frames` samples. Adds the wet stereo signal to
    // `out` (interleaved L R).
    void process(const float* in, float* out, int frames);
    void clear();
    float wet() const { return m_wet; }

private:
    struct Comb {
        std::vector<float> buf;
        size_t idx = 0;
        float store = 0.0f, feedback = 0.0f;
    };
    struct AllPass {
        std::vector<float> buf;
        size_t idx = 0;
    };
    void updateFeedback();
    int m_rate;
    Comb m_comb[2][8];
    AllPass m_allpass[2][4];
    std::vector<float> m_pre;     // pre-delay ring
    size_t m_preIdx = 0;
    float m_rt60 = 0.5f, m_damp = 0.3f, m_wet = 0.0f, m_preDelay = 0.0f;
    float m_targetRt60 = 0.5f, m_targetDamp = 0.3f, m_targetWet = 0.0f, m_targetPre = 0.0f;
};

} // namespace kke
