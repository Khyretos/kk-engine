#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke::ai {

// Senses: sight, hearing, smell and touch. The model is the one stealth
// games use (Thief, Metal Gear, The Last of Us, in their GDC talks): a
// sense doesn't answer yes or no, it gives a stimulus strength in [0, 1]
// each tick, and an agent's *awareness* of something rises with that
// strength and falls when it stops. So a sheep notices a dog crawling at
// the edge of its sight slowly, a barking dog next to it at once, and
// forgets it a while after it's gone. Pure logic, unit-tested in
// tests/test_ai.cpp; kke::ai::AiWorld runs it for every agent.

enum Sense : uint8_t {
    SenseNone = 0,
    SenseSight = 1,
    SenseHearing = 2,
    SenseSmell = 4,
    SenseTouch = 8, // very close, any direction (the "someone's behind me" feeling)
};

struct Senses {
    float sightRange = 20.0f;   // metres
    float fovDegrees = 220.0f;  // full cone; prey animals see almost all round
    float touchRange = 1.5f;    // noticed in any direction closer than this
    float hearing = 1.0f;       // multiplies a noise's loudness (its range in metres)
    float smell = 1.0f;         // multiplies scent range; 0 = can't smell
    float awarenessGain = 2.5f; // awareness per second at stimulus 1
    float awarenessDecay = 0.2f;// per second while nothing is sensed
    float memorySeconds = 12.0f;// forgotten this long after last sensed
    float eyeHeight = 0.8f;     // for line-of-sight rays
};

// How easy something is to notice. 1 = normal; crouching, standing still
// or being small lowers it, running raises it. AiWorld derives it from
// the target's speed unless the game sets it.
struct Conspicuity {
    float visibility = 1.0f;
    float size = 1.0f;
};

// Sight strength in [0, 1] of a target at `target` for an observer at
// `eye` looking along `forward` (ground plane). 0 outside range or cone,
// fading towards the edge of both. Line of sight is the caller's (it
// needs the world): only call this when the ray is clear, or scale by it.
float sightStrength(const glm::vec3& eye, const glm::vec3& forward, const Senses& s, const glm::vec3& target,
                    const Conspicuity& c = {});
// Touch: 1 inside half the range, fading to 0 at touchRange.
float touchStrength(const glm::vec3& self, const Senses& s, const glm::vec3& target);

// A sound heard by anyone within `loudness * hearing` metres, fading with
// distance. A bark is ~25 m, footsteps ~4 m running and ~1.5 m walking.
struct Noise {
    glm::vec3 position{0.0f};
    float loudness = 10.0f;
    uint32_t source = 0;  // who made it (0 = the world)
    uint32_t tag = 0;     // game-defined kind (bark, gunshot, ...)
};
float hearingStrength(const glm::vec3& ear, const Senses& s, const Noise& n);

// Scent: things that smell leave marks behind them as they move; marks
// drift with the wind and fade. An animal smells a mark within
// `smell * baseRange` metres, further downwind of it.
class ScentField {
public:
    struct Mark {
        glm::vec3 position{0.0f};
        float strength = 1.0f; // fades to 0
        uint32_t source = 0;
    };
    float baseRange = 6.0f;    // metres at strength 1 and smell 1
    float fadePerSecond = 0.08f;
    float minSpacing = 1.0f;   // a mover drops a new mark after this many metres
    glm::vec3 wind{0.0f};      // metres per second, ground plane

    // Leaves a mark if `source` moved far enough since its last one.
    void emit(uint32_t source, const glm::vec3& position, float strength = 1.0f);
    void update(float dt);
    void forget(uint32_t source);
    // The strongest mark per source that `nose` smells, as stimuli.
    struct Smelled {
        uint32_t source = 0;
        glm::vec3 position{0.0f}; // of the mark (where to follow the trail)
        float strength = 0.0f;
    };
    void smell(const glm::vec3& nose, const Senses& s, std::vector<Smelled>& out) const;
    const std::vector<Mark>& marks() const { return m_marks; }
    size_t size() const { return m_marks.size(); }

private:
    std::vector<Mark> m_marks;
    struct Last { uint32_t source; glm::vec3 position; };
    std::vector<Last> m_last;
};

// What an agent knows about one other thing.
struct Awareness {
    uint32_t id = 0;
    glm::vec3 lastPosition{0.0f};
    glm::vec3 lastVelocity{0.0f};
    float level = 0.0f;       // 0 = unaware, 1 = fully aware ("spotted")
    float lastSensedAt = -1e9f;
    uint8_t sensedBy = SenseNone; // which senses fed it this tick
    bool spotted = false;     // crossed the spotted threshold (and not yet lost)
};

// Awareness thresholds: rising through `spotted` fires Spotted; falling
// below `lost` (or being forgotten) fires Lost.
struct AwarenessThresholds {
    float spotted = 0.6f;
    float lost = 0.15f;
};

// Adds `stimulus` (the strongest sense this tick, 0 = nothing) to `a`
// over `dt`: rises with the stimulus, decays without it. Returns +1 when
// it just got spotted, -1 when just lost, 0 otherwise.
int updateAwareness(Awareness& a, float stimulus, float dt, float now, const Senses& s, const AwarenessThresholds& t = {});

} // namespace kke::ai
