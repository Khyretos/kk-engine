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
// hit something close all around mean a small, live room; level rays that
// escape, or pass a wall their neighbours hit, are openings (doors,
// windows), and sounds from outside come in through them; nothing overhead
// means outdoors, where there is next to no reverb whatever the ground is
// made of. The walls' distances give the echoes. Pure logic with a ray
// callback (Jolt in the engine, a fake in tests/test_room_acoustics.cpp).

struct AcousticRay {
    bool hit = false;
    float distance = 0.0f;
    uint32_t material = 0;   // AudioMaterialTable id of what was hit
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // of the surface hit: floors aren't walls
};
using AcousticRayFn = std::function<AcousticRay(const glm::vec3& from, const glm::vec3& dir, float maxDistance)>;

// A way out of the listener's room found by the level ring of rays: a ray
// that escaped, or one that went through a gap in the wall its neighbours
// hit (a door into the next room).
struct RoomOpening {
    glm::vec3 dir{0.0f};  // unit, level
    float through = 0.0f; // m from the listener to where it passes the walls (0: out in the open)
    float reach = 0.0f;   // m it stays free (settings.maxDistance when it escaped)
};

// One ray of the level ring, kept for echoes.
struct RoomSample {
    glm::vec3 dir{0.0f};
    AcousticRay ray;
};

struct RoomAcoustics {
    float enclosure = 0.0f;     // 0..1, share of all rays that hit something
    float walls = 0.0f;         // 0..1, share of the level ring that hit a wall (not an opening)
    float ceiling = 0.0f;       // 0..1, share of the upward rays that hit (a roof)
    float meanDistance = 0.0f;  // m, average distance of the hits (room size)
    float absorption = 1.0f;    // average per-bounce absorption; an escaping ray counts as 1
    float surfaceAbsorption = 0.0f; // average absorption of what the rays hit (hard or soft room)
    float rt60 = 0.0f;          // s, time for the reverb to die away by 60 dB
    float wet = 0.0f;           // 0..~0.5, reverb level
    float damping = 0.3f;       // 0..1, how fast the tail loses its highs (soft rooms: more)
    float preDelay = 0.0f;      // s, before the first reflections
    float openness = 0.0f;      // 0..1, share of the level ring that found a way out
    float ceilingHeight = 0.0f; // m above the listener (0: none found)
    uint32_t ceilingMaterial = 0;
    glm::vec3 openingDir{0.0f}; // unit, the average way out (0 if none)
    std::vector<RoomOpening> openings;
    std::vector<RoomSample> ring;
};

struct RoomProbeSettings {
    int rays = 32;               // spread evenly over the sphere: size, roof, absorption
    int ringRays = 24;           // level, at ear height: walls, openings, echoes (never hit a flat floor)
    float maxDistance = 30.0f;   // m; a ray that goes further escaped
    float minAbsorption = 0.12f; // real rooms have furniture and people: never a perfect echo chamber
    // Radians both ray sets are turned about the vertical. Turning each
    // probe a little (AudioModule uses the golden angle) makes successive
    // probes look between each other's rays: a door narrower than the
    // gap between two rays is found within a few probes.
    float rotation = 0.0f;
    float openingDepth = 0.5f;   // m a ray must go past its neighbours' wall to count as a way through it
};

// Evenly spread unit directions (a Fibonacci sphere): same set every time.
std::vector<glm::vec3> fibonacciSphere(int count);

RoomAcoustics probeRoom(const glm::vec3& at, const AcousticRayFn& ray, const AudioMaterialTable& materials,
                        const RoomProbeSettings& settings = {});

// An echo off one wall: from `dir` (unit, world), `delay` s after the
// sound, `gain` relative to it.
struct EchoTap {
    glm::vec3 dir{0.0f};
    float delay = 0.0f;
    float gain = 0.0f;
};

// Probes over time -> the room the listener is in, steady. Each probe is
// only a few rays, turned a little from the last (RoomProbeSettings::
// rotation); the tracker blends the numbers so the reverb doesn't wobble,
// remembers openings from the last few probes (a narrow door seen once
// stays found), and keeps the distance to the walls in each of `bins`
// directions for echoes. Moving further than snapDistance between probes
// (a teleport, a respawn) starts over instead of blending.
class RoomTracker {
public:
    struct Settings {
        float blend = 0.4f;        // share of a new probe in the result
        float snapDistance = 2.0f; // m
        int openingMemory = 6;     // probes an opening is remembered for
        int bins = 24;             // echo directions around the listener
    };
    RoomTracker() : RoomTracker(Settings{}) {}
    explicit RoomTracker(const Settings& s);
    void update(const glm::vec3& listener, const RoomAcoustics& probe, const AudioMaterialTable& materials);
    void reset();
    const RoomAcoustics& room() const { return m_room; }
    // The strongest distinct echoes: one per wall (bins with delays within
    // 4 ms merge), plus the ceiling. Gains fall with the extra path (there
    // and back) and the surface's absorption. At most `maxTaps`.
    std::vector<EchoTap> echoes(int maxTaps, const AudioMaterialTable& materials) const;

    Settings settings;

private:
    struct Bin {
        bool hit = false;
        float distance = 0.0f;
        uint32_t material = 0;
        int age = 1 << 20;         // probes since a ray last landed here
    };
    struct Remembered { RoomOpening opening; int age; };
    RoomAcoustics m_room;
    bool m_have = false;
    glm::vec3 m_at{0.0f};
    std::vector<Bin> m_bins;
    std::vector<Remembered> m_openings;
};

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
