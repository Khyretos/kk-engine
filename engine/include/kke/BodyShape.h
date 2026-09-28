#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// A character's own body as a set of capsules riding its bones, so IK
// knows where the body is and keeps the arms (and whatever the hands
// hold) out of it. The same idea as Unreal's Physics Asset "sphyls" or
// the collision capsules Assassin's Creed and most third-person games
// put on a skeleton; here they are fitted to the character's own mesh
// (each capsule wraps the vertices its bone moves), so a broad Synty
// character and the thin UAL mannequin both get a body their size.
// Without a skinned mesh they come from the bones' proportions.
//
// Pure CPU, unit-tested in tests/test_body_shape.cpp. docs/EQUIPMENT.md
// shows how the games use it.
enum class BodyPart : uint8_t {
    Pelvis, Belly, Chest, UpperChest, Neck, Head,
    ThighL, ThighR, CalfL, CalfR,
    UpperArmL, UpperArmR, ForearmL, ForearmR, HandL, HandR,
    Count
};
const char* bodyPartName(BodyPart part);
// Arms (and hands) of one side; true for the arm parts only.
bool isArmPart(BodyPart part, bool left);

// A capsule: the segment a-b swept by `radius`.
struct Capsule {
    glm::vec3 a{0.0f}, b{0.0f};
    float radius = 0.0f;
};
// How far two capsules overlap (> 0) or are apart (< 0), and the way to
// push `first` out of `second` (unit length; any when they don't overlap).
float capsuleOverlap(const Capsule& first, const Capsule& second, glm::vec3& push);

class BodyShape {
public:
    struct Part {
        BodyPart part = BodyPart::Count;
        int bone = -1;
        glm::vec3 a{0.0f}, b{0.0f}; // in the bone's own space
        float radius = 0.0f;
    };

    BodyShape() = default;
    // `model`: the character with its skinned mesh (the mesh is only read
    // here). Bones are found by their usual names (canonicalBoneName:
    // pelvis, spine_01..03, neck_01, head, thigh/calf/foot, upperarm/
    // lowerarm/hand, _l/_r), so UAL, Unreal and Synty rigs all work.
    static BodyShape fit(const ModelData& model);
    bool valid() const { return !m_parts.empty(); }
    const std::vector<Part>& parts() const { return m_parts; }
    const Part* part(BodyPart p) const;

    // Every part in model space for this frame's bones (poseToModel).
    std::vector<Capsule> posed(const std::vector<glm::mat4>& world) const;
    Capsule posed(const Part& part, const std::vector<glm::mat4>& world) const;

private:
    std::vector<Part> m_parts;
};

// ---------------------------------------------------------------------
// An arm that knows the body. solveHumanArm keeps an arm to a person's
// joint ranges; this also keeps it outside the body:
//   1. a hand aimed inside the torso, head or legs is moved to just
//      outside them;
//   2. the elbow swings round the shoulder-hand line (within the arm's
//      swivel range) to where neither the upper arm nor the forearm
//      goes through the body, preferring where it would have been and
//      where it was last frame (so it doesn't flicker); an arm reaching
//      up past the head makes the head lean away (the neck bends, up to
//      `headTilt`), as people do;
//   3. if no elbow position is clear (the hand wants to reach through
//      the chest to the far side), the hand moves out until the arm
//      clears;
//   4. anything held in the hand (`held`, capsules in the hand bone's
//      space: a racket, a sword, a rifle) is kept out of the body too,
//      by moving the hand.
// `margin` is the gap kept (a hand resting on a thigh touches it; it
// never sinks in). `maxShift` caps how far the hand gives way: a climber
// whose hand must stay on its hold sets it small and moves the body.
struct BodyAvoid {
    float margin = 0.01f;        // m kept between the arm and the body
    bool otherArm = true;        // also keep clear of the other upper arm
    std::vector<Capsule> held;   // in the hand bone's space
    float headTilt = 25.0f;      // degrees the head (its neck) may lean away from an arm reaching past it; 0 = never
    int iterations = 8;          // step 3/4 pushes at most (each moves the hand up to 5 cm)
    float maxShift = 0.4f;       // m the hand may move off its goal to clear the body (a climber's hand on a hold: small, and the body moves instead)
};
// Per arm, kept between frames by the caller.
struct BodyAvoidState {
    float swivel = 0.0f;         // the elbow's extra turn last frame (degrees)
    glm::vec3 shift{0.0f};       // how far the hand had to move last frame (model space)
    float headTilt = 0.0f;       // how far the head leaned away (degrees), and which way
    glm::vec3 headAway{0.0f};
};
struct BodyAvoidResult {
    ArmResult arm;
    float swivel = 0.0f;         // extra elbow turn used (degrees, + up and out)
    float penetration = 0.0f;    // m the arm is still inside the body (0 = clear)
    float headTilt = 0.0f;       // degrees the head leaned away
    bool moved = false;          // the hand had to move off its goal
};
BodyAvoidResult solveHumanArm(const ModelData& model, Pose& pose, const HumanArm& arm, const ArmGoal& goal, const BodyShape& body,
                              const BodyAvoid& avoid = BodyAvoid{}, BodyAvoidState* state = nullptr, const ArmLimits& limits = ArmLimits{});
// How deep this arm (upper arm from half-way out, forearm, hand, and the
// held capsules) is inside the rest of the body for the posed bones.
float armPenetration(const std::vector<glm::mat4>& world, const HumanArm& arm, const BodyShape& body, const BodyAvoid& avoid = BodyAvoid{});

} // namespace kke
