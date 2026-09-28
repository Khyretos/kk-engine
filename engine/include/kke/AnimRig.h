#pragma once

#include "kke/Animator.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kke {

// Posing on top of animation, in the model's own space (the space
// ModelModule draws bones in; the instance transform maps it to the
// world). Pure CPU, unit-tested in tests/test_anim_rig.cpp.
//
// These are "modifiers" in PointDown's sense (SkeletonModifier3D video):
// they run after the Animator has produced a pose, in a fixed order,
// and each one is small: foot placement, then hand targets.

// Model-space transform of every bone for this pose.
std::vector<glm::mat4> poseToModel(const ModelData& model, const Pose& pose);

// ---------------------------------------------------------------------
// Two-bone IK (thigh-calf-foot, upperarm-lowerarm-hand). Analytic: the
// law of cosines sets the bend, then the chain swings to the target.
// `pole` is a model-space point the knee/elbow bends toward. `weight`
// blends from the animated pose (0) to the full solve (1). An
// unreachable target straightens the chain toward it.
struct TwoBoneChain {
    int upper = -1, lower = -1, end = -1;
    bool valid() const { return upper >= 0 && lower >= 0 && end >= 0; }
};
TwoBoneChain findChain(const ModelData& model, const std::string& upper, const std::string& lower, const std::string& end);
void solveTwoBone(const ModelData& model, Pose& pose, const TwoBoneChain& chain, const glm::vec3& target, const glm::vec3& pole,
                  float weight = 1.0f);

// ---------------------------------------------------------------------
// A person's arm: two-bone IK that keeps to what a human arm can do.
// solveTwoBone bends a chain any way the pole says and swings it by the
// shortest turn, which lets an arm reach through its own back, bend its
// elbow backwards or leave the upper arm twisted. solveHumanArm instead
// places the elbow the way anthropomorphic-limb IK does it (Tolani,
// Goswami & Badler 2000): the elbow's "swivel" round the shoulder-hand
// line, starting from where it hangs naturally (down, a little out and
// back), then builds each bone's rotation from its direction and the
// elbow's hinge axis, so the elbow is a hinge and bends only forward.
// Ranges (ArmLimits, the usual joint ranges of motion):
//   - shoulder: the hand's direction from the shoulder, measured in the
//     chest's own frame (the line between the shoulders, so a turned
//     torso turns the range with it), from `acrossChest` toward the other
//     side to `behind` back past the side;
//   - elbow: straight to `elbowMaxFlex`, never backwards;
//   - forearm: `pronation` either way (the twist is the forearm's, not the
//     wrist's);
//   - wrist: bends at most `wristBend`, twists at most `wristTwist`.
// A target outside them is moved to the nearest place the arm can reach;
// the result says where the hand went.
struct ArmLimits {
    float elbowMaxFlex = 145.0f;    // degrees from straight
    float acrossChest = 70.0f;      // degrees from straight ahead toward the other side, arm long
    float acrossChestBent = 115.0f; // the same with the elbow fully bent (a hand on the other shoulder)
    float behind = 135.0f;          // degrees from straight ahead back past the side (90 = out to the side)
    float swivel = 70.0f;           // degrees the elbow may swing round from where it hangs
    float pronation = 90.0f;        // forearm twist either way
    float wristBend = 70.0f;        // any direction
    float wristTwist = 15.0f;
};

// Built once per rig: each bone's own axis and the elbow's hinge in bone
// space, from the rest pose. `other` is the other arm (its upper bone
// gives the shoulders' line); invalid = the chest is the model's own frame.
struct HumanArm {
    TwoBoneChain chain;
    int otherShoulder = -1;
    bool left = false;               // the character's left arm
    glm::vec3 upperAxis{0.0f}, upperHinge{0.0f}; // upper arm's local frame
    glm::vec3 lowerAxis{0.0f}, lowerHinge{0.0f}; // forearm's local frame
    glm::quat handRest{1, 0, 0, 0};  // the hand's rest rotation in the forearm (the wrist's neutral)
    glm::vec3 forward{0, 0, 1};      // modelForward
    bool valid() const { return chain.valid(); }
};
HumanArm makeHumanArm(const ModelData& model, const TwoBoneChain& arm, const TwoBoneChain& other = TwoBoneChain{});

struct ArmGoal {
    glm::vec3 hand{0.0f};                     // model space
    std::optional<glm::vec3> elbowToward;     // model-space point the elbow leans to (within `swivel`)
    std::optional<glm::quat> handRotation;    // model-space rotation for the hand (within the forearm and wrist ranges)
    float weight = 1.0f;                      // 0 = the animated pose, 1 = the goal
    // Degrees the elbow turns further round the shoulder-hand line, +
    // up and out (away from the body), still within `swivel`. The body
    // awareness in kke/BodyShape.h uses it to keep the arm out of the torso.
    float swivelOffset = 0.0f;
};
struct ArmResult {
    glm::vec3 hand{0.0f};   // where the hand went (model space)
    glm::quat handRotation{1, 0, 0, 0}; // the hand's rotation (model space)
    bool limited = false;   // the goal was outside the arm's ranges
};
ArmResult solveHumanArm(const ModelData& model, Pose& pose, const HumanArm& arm, const ArmGoal& goal, const ArmLimits& limits = ArmLimits{});
// Where solveHumanArm would put the shoulder, elbow and hand (model
// space) for these posed bones, without posing anything: cheap enough to
// try many elbow positions.
struct ArmPoints {
    glm::vec3 shoulder{0.0f}, elbow{0.0f}, hand{0.0f};
    bool limited = false;
};
ArmPoints humanArmPoints(const std::vector<glm::mat4>& world, const HumanArm& arm, const ArmGoal& goal, const ArmLimits& limits = ArmLimits{});

// ---------------------------------------------------------------------
// Foot placement: each foot keeps its animated height above the ground
// under it (so steps still lift), the pelvis drops when a foot must go
// lower than the capsule's floor, two-bone IK bends the legs, and each
// foot tilts to lie along the slope under it (up to maxTilt). Smoothed
// over frames, so stepping onto a stair isn't a snap.
class FootPlacer {
public:
    // model-space point -> the ground below it (model space). False = none.
    using GroundQuery = std::function<bool(const glm::vec3& from, glm::vec3& ground)>;
    // Same, plus the ground's normal there (model space, unit length).
    using SurfaceQuery = std::function<bool(const glm::vec3& from, glm::vec3& ground, glm::vec3& normal)>;

    struct Settings {
        float maxDrop = 0.45f;    // how far the pelvis may go down
        float maxRaise = 0.45f;   // how far a foot may go up
        float probeUp = 0.5f;     // ground ray starts this far above the foot
        float smoothing = 14.0f;  // 1/s: how fast offsets follow the ground
        float maxTilt = 30.0f;    // degrees a foot may tilt to match a slope
    };

    FootPlacer() = default;
    FootPlacer(const ModelData& model, const TwoBoneChain& left, const TwoBoneChain& right, int pelvis);
    FootPlacer(const ModelData& model, const TwoBoneChain& left, const TwoBoneChain& right, int pelvis, const Settings& s);
    bool valid() const { return m_left.valid() && m_right.valid() && m_pelvis >= 0; }

    // `weight` 0 = off (e.g. in the air, where feet must not reach down).
    void apply(const ModelData& model, Pose& pose, const SurfaceQuery& ground, float dt, float weight = 1.0f);
    // Flat feet: every ground counts as level.
    void apply(const ModelData& model, Pose& pose, const GroundQuery& ground, float dt, float weight = 1.0f);
    // The (smoothed, clamped) ground normal each foot is tilted to; 0 = left.
    glm::vec3 footNormal(int foot) const { return m_footNormal[foot & 1]; }
    float pelvisOffset() const { return m_pelvisOffset; }
    Settings& settings() { return m_s; }

private:
    TwoBoneChain m_left, m_right;
    int m_pelvis = -1;
    Settings m_s;
    float m_footOffset[2] = { 0.0f, 0.0f };
    float m_pelvisOffset = 0.0f;
    glm::vec3 m_footNormal[2] = { glm::vec3(0, 1, 0), glm::vec3(0, 1, 0) };
};

// ---------------------------------------------------------------------
// Retargeting: play one skeleton's clips on another (UAL's mannequin
// clips on a Synty character). Bones pair up by name, forgivingly
// (case, "mixamorig:" style prefixes, Synty's "indexFinger" / "finger"
// for index / middle). Each matched bone copies the source's rotation
// *change from its rest pose*, in model space, so bones whose axes point
// differently still move the same way; the pelvis also copies its
// motion, scaled by leg length. Unmatched target bones stay at rest.
//
// The one contract (PointDown, "Importing animated 3D characters"): a
// clip only means something for the skeleton it was made for. The match
// lists what didn't pair up, so a silent half-working retarget shows.
struct BoneMatch {
    std::vector<int> sourceOf;               // per target bone: source bone or -1
    std::vector<std::string> unmatchedTarget; // target bones left at rest
    int matched = 0;
};
std::string canonicalBoneName(const std::string& name);
// Which way the character faces in its model space (horizontal unit
// vector), from where its left and right thighs (or upper arms) are in
// the rest pose. UAL's mannequin faces -Z, Synty characters +Z: a
// retarget turns the motion by the difference, and a game turns the
// model so this points where the character walks. (0,0,1) if unknown.
glm::vec3 modelForward(const ModelData& model);
BoneMatch matchBones(const ModelData& source, const ModelData& target);
// Every source clip as a clip for the target skeleton.
std::vector<ModelAnimation> retargetAnimations(const ModelData& source, const ModelData& target, const BoneMatch& match);

// Adds `source`'s clips to `rig` when both use the same skeleton but the
// files list the bones in a different order or with extras (a second
// animation library for the same mannequin, animation-only FBX files for
// a Sidekick character): each rig bone takes the source bone of the same
// name, bones the source lacks stay at rest. No retargeting: use
// retargetAnimations for a different skeleton. Returns the clips added.
size_t appendClipsByBoneName(ModelData& rig, const ModelData& source);

} // namespace kke
