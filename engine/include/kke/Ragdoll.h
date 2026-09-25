#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <string>
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
};

struct RagdollJoint {
    int bodyA = -1, bodyB = -1;
    glm::vec3 anchor{0.0f};         // world space, shared by both bodies at creation
    // Hinge joints only rotate about this axis (knees). Others are ball joints.
    bool hinge = false;
    glm::vec3 hingeAxis{1.0f, 0.0f, 0.0f}; // world space
};

struct RagdollDesc {
    std::vector<RagdollBody> bodies;
    std::vector<RagdollJoint> joints;
    int findBody(const std::string& name) const;
};

// How a skeleton follows a ragdoll: each bone either rides a body (keeping
// the offset it had when the ragdoll was built) or, if unmapped (fingers,
// toes, eyes), keeps its rest pose relative to its parent.
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
RagdollDesc buildHumanoidRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld,
                                 float totalMass = 70.0f, std::string* missingBone = nullptr);

RagdollSkinBinding bindSkeletonToRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, const RagdollDesc& ragdoll);

// Bone world transforms from the bodies' current world transforms, then
// taken into the model's space with `worldToModel` (inverse of the
// instance transform) — ready for ModelModule::setBoneWorldOverride().
std::vector<glm::mat4> poseFromRagdoll(const ModelData& model, const RagdollSkinBinding& binding,
                                       const std::vector<glm::mat4>& bodyWorld, const glm::mat4& worldToModel);

} // namespace kke
