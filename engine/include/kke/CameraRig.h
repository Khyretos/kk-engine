#pragma once

#include "kke/Application.h"

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace kke {

// Gameplay cameras, the way the big engines structure them:
//   FirstPerson  - eyes at the character's head.
//   ThirdPerson  - Unreal's SpringArm: a boom from a pivot above the
//                  character, over-the-shoulder offset, position lag; when
//                  something is between the pivot and the camera the arm
//                  shortens instantly (never see through walls) and grows
//                  back smoothly.
//   Orbit        - around a point, for inspecting and editors.
//   Cinematic    - a smooth path through keyframes (Catmull-Rom), like a
//                  Cinemachine dolly track.
// Pure CPU; collision comes in as a function (RigidWorld::raycast in the
// engine, a lambda in tests). One rig per viewport/player.
class CameraRig {
public:
    enum class Mode { FirstPerson, ThirdPerson, Orbit, Cinematic };

    struct Settings {
        float eyeHeight = 1.65f;        // first person, above the feet
        float pivotHeight = 1.5f;       // third person boom origin, above the feet
        float armLength = 3.5f;         // third person distance
        float shoulderOffset = 0.45f;   // to the right (negative: left)
        float probeRadius = 0.2f;       // how far to stay off walls
        float armReturnSpeed = 4.0f;    // m/s the arm grows back after a hit
        float positionLag = 14.0f;      // 1/s; 0 = rigid
        float orbitDistance = 6.0f;
        float pitchMin = -80.0f, pitchMax = 80.0f;
        float fovDegrees = 60.0f;
    };

    // Distance along `dir` (unit) from `from` to the first hit, or
    // `maxDistance` if nothing.
    using RayFn = std::function<float(const glm::vec3& from, const glm::vec3& dir, float maxDistance)>;

    struct Keyframe { glm::vec3 position, target; float time; };

    Mode mode = Mode::ThirdPerson;
    Settings settings;
    float yaw = 0.0f, pitch = -10.0f; // degrees; yaw 0 looks along -Z

    void addLook(float yawDegrees, float pitchDegrees);
    // Horizontal directions for movement input relative to the view.
    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 viewDirection() const;

    void setCinematic(std::vector<Keyframe> keys, bool loop);
    // Moves the camera; `focus` = the character's feet (or the orbit point).
    void update(float dt, const glm::vec3& focus, const RayFn& ray, Camera& out);
    float currentArmLength() const { return m_arm; }

private:
    float m_arm = -1.0f;
    glm::vec3 m_lagged{0.0f};
    bool m_hasLagged = false;
    std::vector<Keyframe> m_keys;
    bool m_loop = false;
    float m_cinematicTime = 0.0f;
};

} // namespace kke
