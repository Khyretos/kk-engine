#pragma once

// A hit flinch as a procedural layer: the upper body tips away from the
// blow and springs back (the caller decays `amount`). One bone turned in
// model space, so everything above it follows; no clip needed, which is
// why it works on the goblins (who have no hit clips) and the king alike.

#include "kke/AnimRig.h"
#include "kke/Animator.h"

#include <glm/gtc/quaternion.hpp>

namespace horde {

// `bone`: the one to turn (spine_01); `awayModel`: the blow's direction in
// the model's space; `amount` 0..1.
inline void applyFlinch(const kke::ModelData& rig, kke::Pose& pose, int bone, const glm::vec3& awayModel, float amount, float maxDegrees = 22.0f) {
    if (bone < 0 || static_cast<size_t>(bone) >= pose.size() || amount <= 0.0f) return;
    glm::vec3 away(awayModel.x, 0.0f, awayModel.z);
    const float len = glm::length(away);
    if (len < 1e-4f) return;
    away /= len;
    const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), away));
    const glm::quat turn = glm::angleAxis(glm::radians(maxDegrees) * amount, axis);
    const int parent = rig.bones[static_cast<size_t>(bone)].parent;
    glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
    if (parent >= 0) {
        const std::vector<glm::mat4> model = kke::poseToModel(rig, pose);
        glm::mat3 m(model[static_cast<size_t>(parent)]);
        for (int c = 0; c < 3; ++c) m[c] = glm::normalize(m[c]); // rotation only, whatever the scale
        parentRot = glm::normalize(glm::quat_cast(m));
    }
    kke::BoneTRS& b = pose[static_cast<size_t>(bone)];
    b.r = glm::normalize(glm::inverse(parentRot) * turn * parentRot * b.r);
}

} // namespace horde
