#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <string>
#include <utility>
#include <vector>

namespace kke {

// A ragdoll as plain data: boxes and the joints between them, in world
// space. Physics-agnostic on purpose — kke::IRagdollPhysics (see
// Capabilities.h) is what simulates one, and PhysicsModule (FEMFX) is
// just the first implementation; another physics module can plug in
// behind the same interface without the character code changing.
struct RagdollBody {
    std::string name;
    glm::mat4 transform{1.0f};      // rigid: rotation + center position, world space
    glm::vec3 halfExtents{0.1f};
    float mass = 1.0f;
    // Skeleton bones that ride this body (bindSkeletonToRagdoll; matched
    // case-insensitively). Empty on every body = the humanoid bone map.
    std::vector<std::string> bones;
};

struct RagdollJoint {
    // What the builders call it ("hip_l", "knee_l", "neck", ...), so a
    // game can find and override one: RagdollDesc::findJoint.
    std::string name;
    int bodyA = -1, bodyB = -1;
    glm::vec3 anchor{0.0f};         // world space, shared by both bodies at creation
    // Hinge joints only rotate about this axis (knees). Others are ball joints.
    bool hinge = false;
    glm::vec3 hingeAxis{1.0f, 0.0f, 0.0f}; // world space
    // Limits, for physics that has them (Jolt: RigidBodyModule; FEMFX has
    // none). Angles in degrees; world-space axes as they are in the pose
    // the ragdoll was built in, so they turn with bodyA from then on.
    //
    // Ball joints: bodyB swings within an oval cone around `swingAxis`
    // (zero = bodyB's direction from the anchor at build time, i.e. the
    // build pose is the neutral one). `swingDegrees` is the half angle of
    // the swing about `swingBendAxis` (e.g. a leg's forward/back swing
    // about the hip's side axis), `swingSideDegrees` the half angle across
    // it (sideways; < 0 = same as swingDegrees, a round cone). A zero
    // swingBendAxis means "any" (only meaningful for a round cone). bodyB
    // twists at most +-`twistDegrees` about itself.
    //
    // Hinges: the angle about hingeAxis (right hand rule, bodyB relative
    // to bodyA) stays within hingeMinDegrees..hingeMaxDegrees.
    float swingDegrees = 60.0f;
    float swingSideDegrees = -1.0f;
    float twistDegrees = 30.0f;
    glm::vec3 swingAxis{0.0f};
    glm::vec3 swingBendAxis{0.0f};
    float hingeMinDegrees = -150.0f, hingeMaxDegrees = 150.0f;
    // false = no limits at all (a free ball joint or hinge).
    bool limited = true;
    // How much the joint resists moving (muscle tone), N*m. < 0 = scaled
    // from the lighter body's mass, so a pug and a horse both look right.
    float frictionTorque = -1.0f;
};

struct RagdollDesc {
    std::vector<RagdollBody> bodies;
    std::vector<RagdollJoint> joints;
    int findBody(const std::string& name) const;
    // A joint by name (nullptr if none), to override its limits before
    // handing the desc to IRagdollPhysics::createRagdoll:
    //   if (auto* knee = desc.findJoint("knee_l")) knee->hingeMaxDegrees = 90;
    RagdollJoint* findJoint(const std::string& name);
    const RagdollJoint* findJoint(const std::string& name) const;
    // Every limit times `factor` (0.5 = stiffer, 2 = looser), capped at
    // what each joint type can do. Hinges keep their straight-leg stop
    // (the min side) and scale how far they bend.
    void scaleLimits(float factor);
};

struct RagdollSkinBinding {
    std::vector<int> bodyOfBone;            // -1 = follows parent
    std::vector<glm::mat4> boneOffset;      // inverse(body at bind) * bone world at bind
    std::vector<glm::mat4> restLocal;       // bone local rest, for unmapped bones
};

// Builds an 11-body humanoid (pelvis, torso, head, upper/lower arms,
// thighs, calves) from a skeleton with common Synty / Unreal-style bone
// names (Pelvis, spine_01..03, neck_01, head, UpperArm_L, lowerarm_l,
// Hand_L, Thigh_L, calf_l, Foot_L, ...; matched case-insensitively).
// `boneWorld` is each bone's world transform right now (e.g.
// instanceTransform * ModelModule::boneWorld()), so a ragdoll starts from
// whatever pose the character was in. Returns an empty desc (and sets
// *missingBone) if a required bone is absent.
//
// Joint limits follow human ranges of motion, measured in the body's own
// frame (not the build pose): spine, neck, shoulders and hips are oval
// cones centred where the joint's range is (a hip swings ~120 deg forward
// but ~20 back), elbows and knees are hinges that bend one way only.
// Joint names: spine, neck, shoulder_l/r, elbow_l/r, hip_l/r, knee_l/r.
RagdollDesc buildHumanoidRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld,
                                 float totalMass = 70.0f, std::string* missingBone = nullptr);

// Bone names for buildQuadrupedRagdoll. Legs in the order front left,
// front right, back left, back right. The defaults are the Quaternius
// animal rigs (Farm Animals: horse, cow, pig, pug, sheep, llama, zebra;
// the same names are common in other low-poly packs). `foot` and `tail`
// are optional: a leg without a foot bone ends as long as its upper leg,
// and an animal without a tail has no tail body.
struct QuadrupedBones {
    std::string hips = "Hips", torso = "Torso", shoulders = "Shoulders", neck = "Neck", head = "Head";
    std::string upperLeg[4] = { "FrontUpLeg.L", "FrontUpLeg.R", "BackUpLeg.L", "BackUpLeg.R" };
    std::string lowerLeg[4] = { "FrontLowLeg.L", "FrontLowLeg.R", "BackLowLeg.L", "BackLowLeg.R" };
    std::string foot[4] = { "FrontFoot.L", "FrontFoot.R", "BackFoot.L", "BackFoot.R" };
    std::vector<std::string> tail = { "Tail1", "Tail2", "Tail3", "Tail4" }; // root to tip
    // Extra bones that ride a body (e.g. the Quaternius "Body", "Back",
    // "FrontLeg.L" helpers), as {bone, body name}.
    std::vector<std::pair<std::string, std::string>> alsoRide = {
        { "Body", "pelvis" }, { "Back", "pelvis" }, { "BackLeg.L", "pelvis" }, { "BackLeg.R", "pelvis" },
        { "FrontLeg.L", "chest" }, { "FrontLeg.R", "chest" },
    };
};

// A four-legged animal: pelvis, chest, neck, head, upper and lower legs
// and (if there is one) a tail; 12-13 bodies. Limits are an animal's, not
// a person's: a stiff back, legs that swing mostly forward and back, front
// knees that fold the hoof backward and hocks that fold it forward, and
// neck / head / tail cones centred on the rest pose (a grazing horse can
// still lift its head). `boneWorld` as for buildHumanoidRagdoll; the rest
// pose comes from the model. Joint names: spine, neck, head, tail,
// hip_fl/fr/bl/br (leg to body), knee_fl/fr/bl/br (front knee, hind hock).
RagdollDesc buildQuadrupedRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, float totalMass = 500.0f,
                                  std::string* missingBone = nullptr, const QuadrupedBones& bones = QuadrupedBones{});

// Which bone rides which body: RagdollBody::bones if any body lists some
// (the builders fill them in), else the humanoid bone map.
RagdollSkinBinding bindSkeletonToRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, const RagdollDesc& ragdoll);

// Bone world transforms from the bodies' current world transforms, then
// taken into the model's space with `worldToModel` (inverse of the
// instance transform) — ready for ModelModule::setBoneWorldOverride().
std::vector<glm::mat4> poseFromRagdoll(const ModelData& model, const RagdollSkinBinding& binding,
                                       const std::vector<glm::mat4>& bodyWorld, const glm::mat4& worldToModel);

// Blend back to animation: bone by bone, rotation slerped and position
// lerped from `from` (e.g. the last ragdoll pose) to `to` (the animated
// pose), t = 0..1 (clamped). Scale is taken from `to`. Poses of
// different sizes blend over the common bones and take the rest from `to`.
std::vector<glm::mat4> blendPoses(const std::vector<glm::mat4>& from, const std::vector<glm::mat4>& to, float t);

// Smooth 0..1 weight for a blend that started `elapsed` seconds ago and
// takes `duration` seconds (ease in and out; duration <= 0 = done).
float blendWeight(float elapsed, float duration);

} // namespace kke
