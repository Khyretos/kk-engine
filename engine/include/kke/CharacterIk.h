#pragma once

#include "kke/AnimRig.h"
#include "kke/BodyShape.h"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace kke {

// The layers a person gets on top of their animation, in the order the
// big engines (and PointDown's SkeletonModifier3D video) run them:
//   1. the clip is the base: the Animator's pose carries the style and
//      the timing of every move;
//   2. contacts put the hands and feet where the world says they are:
//      feet on the ground under them (FootPlacer: heel, ball and toe, the
//      pelvis drops for a low foot), or planted on a ledge, a wall or the
//      top of a box; hands on an edge or a handle, solved as a human arm
//      (shoulder girdle, joint ranges: kke::solveHumanArm) that keeps out
//      of its own body (kke::BodyShape);
//   3. procedural fine-tuning: the upper body leans into speeding up,
//      slowing down and turning (a little: a run that never leans looks
//      like a mannequin on a rail).
// Contacts are set every frame, in world space; a hand or foot without
// one fades back to the clip (Settings::handBlend, footBlend), so a hand
// reaching for a ledge or a foot leaving a wall never pops.
//
// Pure CPU, unit-tested in tests/test_character_ik.cpp. Used by the
// walkable showcase (games/showcase) for walking, vaulting, climbing and
// hanging; docs/PROCEDURAL_ANIMATION.md has the walkthrough.
class CharacterIk {
public:
    enum Side : int { Left = 0, Right = 1 };

    struct Settings {
        float handBlend = 16.0f;     // 1/s: a hand contact fading in or out
        float footBlend = 12.0f;     // 1/s: the same for a planted foot
        float groundBlend = 10.0f;   // 1/s: feet on the ground switching on or off
        float lean = 1.6f;           // degrees of lean per m/s^2 (forward, back, into turns)
        float maxLean = 9.0f;        // degrees, at most
        float leanSmoothing = 7.0f;  // 1/s
        float footContactTilt = 70.0f; // degrees a planted foot may turn to lie on what it stands on (a wall: the ball of the foot on it)
        BodyAvoid avoid;             // arms (and what the hands hold) out of the body
        ArmLimits arm;
        FootPlacer::Settings feet;
    };

    // The ground under a point, world space: `from` is above it, a ray
    // goes down. False = nothing there.
    using GroundQuery = std::function<bool(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal)>;

    CharacterIk() = default;
    // `rig`: the bones the pose is for (and its clips); `skinned`: the
    // same skeleton with its mesh, to fit the body's capsules to (nullptr:
    // they come from the bones' proportions).
    explicit CharacterIk(const ModelData& rig, const ModelData* skinned = nullptr);
    CharacterIk(const ModelData& rig, const ModelData* skinned, const Settings& settings);
    bool valid() const { return m_feet.valid() || m_arm[0].valid() || m_arm[1].valid(); }

    // This frame's contacts, world space. A hand goes to `point` (its
    // palm), the elbow leaning toward `elbowToward` if given; a foot's
    // sole goes onto `point`, lying along `normal`.
    void hand(Side side, const glm::vec3& point, const std::optional<glm::vec3>& elbowToward = std::nullopt);
    void foot(Side side, const glm::vec3& point, const glm::vec3& normal);
    // Feet without a contact stand on the ground under them (FootPlacer);
    // off in the air, where they'd reach down for the floor. Default on.
    void feetOnGround(bool on) { m_groundWanted = on; }

    // Poses on top of the clip. `toWorld`: the character's transform (the
    // feet at its origin); `velocity`: how the character moves (world,
    // m/s), for the lean. Clears this frame's contacts.
    void apply(const ModelData& rig, Pose& pose, const glm::mat4& toWorld, const GroundQuery& ground, const glm::vec3& velocity, float dt);
    // Forget the lean and fade state (after a teleport).
    void reset();

    float handWeight(Side side) const { return m_handW[side]; }
    float footWeight(Side side) const { return m_footW[side]; }
    glm::vec3 leanDegrees() const { return m_lean; } // model space: the way the chest leans, its length in degrees
    const BodyShape& bodyShape() const { return m_body; }
    const HumanArm& arm(Side side) const { return m_arm[side]; }
    Settings& settings() { return m_s; }

private:
    void leanInto(const ModelData& rig, Pose& pose, const glm::mat4& toWorld, const glm::vec3& velocity, float dt);
    void plantFeet(const ModelData& rig, Pose& pose, const glm::mat4& toModel);
    void placeHands(const ModelData& rig, Pose& pose, const glm::mat4& toModel);

    Settings m_s;
    FootPlacer m_feet;
    TwoBoneChain m_leg[2];
    HumanArm m_arm[2];
    BodyShape m_body;
    BodyAvoidState m_avoid[2];
    int m_spine[3] = { -1, -1, -1 };
    float m_ankleRest[2] = { 0.08f, 0.08f }; // the ankle above the sole at rest
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};

    // This frame's contacts, and the last ones (a fading limb holds on to them).
    struct HandContact { glm::vec3 point{0.0f}; std::optional<glm::vec3> elbow; bool set = false; };
    struct FootContact { glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f}; bool set = false; };
    HandContact m_hand[2], m_lastHand[2];
    FootContact m_foot[2], m_lastFoot[2];
    float m_handW[2] = { 0.0f, 0.0f }, m_footW[2] = { 0.0f, 0.0f };
    bool m_groundWanted = true;
    float m_groundW = 1.0f;

    // The lean: smoothed velocity and lean (model-space degrees).
    glm::vec3 m_velocity{0.0f};
    bool m_haveVelocity = false;
    glm::vec3 m_lean{0.0f};
};

} // namespace kke
