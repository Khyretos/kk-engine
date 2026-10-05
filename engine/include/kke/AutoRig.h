#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <string>

namespace kke {

// Auto-rigging: gives a humanoid mesh that has no skeleton (an OBJ export,
// a sculpt, a scan) the bones and skin weights of a reference skeleton, so
// it plays that skeleton's clips (UAL1_Standard.fbx) with retargetAnimations.
//
// How it works (docs/AUTO_RIG.md):
//  1. The mesh is turned to face the way the reference faces.
//  2. The reference skeleton is scaled to the mesh's height, then each
//     joint is moved into the body: hips, knees and ankles to the middle
//     of the leg at their height, the spine and neck to the middle of the
//     torso, and the arms onto the arm's own axis (any arm angle: A-pose,
//     T-pose, in between), elbow and wrist at the reference's proportions.
//  3. Skin weights: every point of the skin is weighted to the bone
//     segments nearest to it (left bones never reach the right side, and
//     the other way round), blended over a few centimetres, then smoothed
//     over the surface so joints bend softly. Fingers go with the hand.
//     Small separate parts inside the head (eyes, teeth) follow the head.
//  4. The mesh is posed into the reference's rest pose (an A-pose body is
//     lifted into the T-pose), so every bone rests turned exactly as the
//     reference's and a retarget copies the clips unchanged.
//
// The reference needs UE-style bone names (pelvis, spine_01..03, neck_01,
// Head, clavicle_l, upperarm_l, lowerarm_l, hand_l, thigh_l, calf_l,
// foot_l, ball_l and the _r side), which UAL, Mixamo-renamed and Synty
// rigs have. The body must stand upright on y = 0, arms away from the
// body (not touching the hips), legs apart.
struct AutoRigOptions {
    // Which way the mesh faces in its own model space. Character Creator,
    // Blender and most exports face +Z.
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    // How far past the nearest bone segment a point still shares weight (m,
    // at the reference's size): wider = softer joints.
    float blend = 0.03f;
    // Smoothing passes over the surface after the distance weights.
    int smoothing = 4;
};

struct AutoRigReport {
    bool ok = false;
    std::string reason;           // why not, when !ok
    float scale = 1.0f;           // mesh height / reference height
    float armDropDegrees[2] = {}; // how far below the T-pose the arms were (left, right)
    size_t points = 0;            // welded skin points weighted
};

// `body`: the mesh (meters, +Y up). On success it becomes a skinned model
// with the reference's bones (no clips) in the reference's rest pose.
// `reference`: a skinned humanoid with that skeleton (its mesh is unused).
AutoRigReport autoRigHumanoid(ModelData& body, const ModelData& reference, const AutoRigOptions& options = {});

} // namespace kke
