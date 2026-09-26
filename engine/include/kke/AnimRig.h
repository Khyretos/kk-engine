#pragma once

#include "kke/Animator.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
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

} // namespace kke
