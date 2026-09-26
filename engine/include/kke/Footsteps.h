#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

class RigidWorld;
class AudioModule;

// A foot touching down (docs/AUDIO.md "Footsteps").
struct FootstepEvent {
    int foot = 0;                 // 0 = left, 1 = right
    glm::vec3 position{0.0f};     // where the foot is, world space
    float intensity = 0.5f;       // 0..1, from how fast the body moves (or how far it fell)
};

// Finds footsteps in an animated character's feet, whatever the clip:
// a foot that rose clear of the ground and comes back down is a step.
// Works from each foot's height above the ground under it, so it follows
// slopes and stairs, and learns each rig's resting ankle height by itself
// (the lowest height seen, relaxing slowly upward) instead of needing it
// set per character. Pure logic: tests/test_footsteps.cpp.
class FootstepDetector {
public:
    struct Settings {
        float liftHeight = 0.05f;   // m above rest the foot must rise before its next step counts
        float plantHeight = 0.025f; // m above rest that counts as down
        float minInterval = 0.18f;  // s between two steps of the same foot
        float fullSpeed = 6.0f;     // m/s body speed of the loudest step
        float relax = 0.5f;         // 1/s: how fast the learned rest height follows the foot upward
    };
    FootstepDetector() = default;
    explicit FootstepDetector(const Settings& s) : settings(s) {}

    // Once per frame per foot. `height`: the foot bone above the ground
    // under it; `grounded`: the character stands (false in the air: no
    // steps, and the landing counts as one for each foot, as loud as the
    // fall was long).
    void update(int foot, const glm::vec3& position, float height, bool grounded, float bodySpeed, float dt,
                std::vector<FootstepEvent>& out);
    void reset();
    float restHeight(int foot) const { return m_feet[foot & 1].rest; }

    Settings settings;

private:
    struct Foot {
        float rest = 1e9f;          // learned resting height (1e9 = not yet)
        bool lifted = false;
        float sinceStep = 1e9f;
        float airTime = 0.0f, groundTime = 0.0f;
    };
    Foot m_feet[2];
};

#if KKE_ENABLE_JOLT
// Footsteps for a skinned character: finds its foot bones by name (UE's
// foot_l/foot_r, Mixamo's LeftFoot/RightFoot, Synty's Foot_L/Foot_R, ...),
// asks the physics world what's under each foot, and plays a step of that
// ground's material through the AudioModule.
class CharacterFootsteps {
public:
    // False when the rig has no recognisable feet (then update does nothing).
    bool bind(const ModelData& rig);
    bool bound() const { return m_foot[0] >= 0 && m_foot[1] >= 0; }
    // `modelBones`: model-space bone transforms of this frame's pose
    // (kke::poseToModel); `toWorld`: the instance transform.
    void update(const std::vector<glm::mat4>& modelBones, const glm::mat4& toWorld, RigidWorld& world, AudioModule* audio,
                float bodySpeed, bool grounded, float dt);
    FootstepDetector detector;
    float gain = 0.8f;
    uint64_t played() const { return m_played; }

private:
    int m_foot[2] = {-1, -1};
    uint32_t m_seed = 1;
    uint64_t m_played = 0;
    std::vector<FootstepEvent> m_events;
};
#endif

} // namespace kke
