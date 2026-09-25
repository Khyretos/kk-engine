#include "kke/Ragdoll.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kke {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int findBoneCI(const ModelData& model, const std::string& name) {
    const std::string wanted = lower(name);
    for (size_t i = 0; i < model.bones.size(); ++i) {
        if (lower(model.bones[i].name) == wanted) return static_cast<int>(i);
    }
    return -1;
}

// A box whose local +Y runs from `from` to `to`, oriented so local +Z is
// as close as possible to `forward`.
RagdollBody segment(const std::string& name, glm::vec3 from, glm::vec3 to, float halfThickness, float mass, glm::vec3 forward,
                    float extendEnd = 0.0f) {
    glm::vec3 d = to - from;
    float len = glm::length(d);
    glm::vec3 y = len > 1e-5f ? d / len : glm::vec3(0, 1, 0);
    to += y * extendEnd;
    len += extendEnd;
    glm::vec3 x = glm::cross(y, forward);
    if (glm::length(x) < 1e-4f) x = glm::cross(y, glm::vec3(1, 0, 0));
    if (glm::length(x) < 1e-4f) x = glm::cross(y, glm::vec3(0, 0, 1));
    x = glm::normalize(x);
    glm::vec3 z = glm::cross(x, y);
    RagdollBody b;
    b.name = name;
    b.transform = glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4((from + to) * 0.5f, 1));
    b.halfExtents = glm::vec3(halfThickness, std::max(len * 0.5f, halfThickness), halfThickness);
    b.mass = mass;
    return b;
}

glm::mat4 rigidInverse(const glm::mat4& m) {
    glm::mat3 r = glm::transpose(glm::mat3(m));
    glm::mat4 inv(r);
    inv[3] = glm::vec4(-(r * glm::vec3(m[3])), 1.0f);
    return inv;
}

} // namespace

int RagdollDesc::findBody(const std::string& name) const {
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

RagdollDesc buildHumanoidRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, float totalMass,
                                 std::string* missingBone) {
    const char* required[] = { "Pelvis", "spine_01", "spine_02", "neck_01", "head",
                               "UpperArm_L", "lowerarm_l", "Hand_L", "UpperArm_R", "lowerarm_r", "Hand_R",
                               "Thigh_L", "calf_l", "Foot_L", "Thigh_R", "calf_r", "Foot_R" };
    auto pos = [&](const char* name) { return glm::vec3(boneWorld[findBoneCI(model, name)][3]); };
    for (const char* name : required) {
        int b = findBoneCI(model, name);
        if (b < 0 || b >= static_cast<int>(boneWorld.size())) {
            if (missingBone) *missingBone = name;
            return {};
        }
    }

    glm::vec3 pelvis = pos("Pelvis"), spine1 = pos("spine_01"), spine2 = pos("spine_02"), neck = pos("neck_01"), head = pos("head");
    glm::vec3 upL = pos("UpperArm_L"), loL = pos("lowerarm_l"), handL = pos("Hand_L");
    glm::vec3 upR = pos("UpperArm_R"), loR = pos("lowerarm_r"), handR = pos("Hand_R");
    glm::vec3 thL = pos("Thigh_L"), caL = pos("calf_l"), ftL = pos("Foot_L");
    glm::vec3 thR = pos("Thigh_R"), caR = pos("calf_r"), ftR = pos("Foot_R");

    // Character frame from the skeleton itself: up = pelvis -> neck,
    // lateral = right hip -> left hip, forward = their cross.
    glm::vec3 up = glm::normalize(neck - pelvis);
    glm::vec3 lateral = glm::normalize(thL - thR);
    glm::vec3 forward = glm::normalize(glm::cross(lateral, up));
    float height = glm::length(neck - pelvis) * 2.9f; // proportion of a human: ~ pelvis-to-neck x 2.9
    float limb = height * 0.035f;

    RagdollDesc d;
    auto add = [&](RagdollBody b) { d.bodies.push_back(b); return static_cast<int>(d.bodies.size()) - 1; };
    float m = totalMass;
    int pelvisB = add(segment("pelvis", pelvis - up * 0.04f * height, spine2, height * 0.08f, m * 0.15f, forward));
    int torso = add(segment("torso", spine2, neck, height * 0.1f, m * 0.30f, forward));
    int headB = add(segment("head", neck, head + up * height * 0.07f, height * 0.06f, m * 0.08f, forward));
    int uaL = add(segment("upperarm_l", upL, loL, limb, m * 0.03f, forward));
    int laL = add(segment("lowerarm_l", loL, handL, limb * 0.85f, m * 0.025f, forward, height * 0.06f));
    int uaR = add(segment("upperarm_r", upR, loR, limb, m * 0.03f, forward));
    int laR = add(segment("lowerarm_r", loR, handR, limb * 0.85f, m * 0.025f, forward, height * 0.06f));
    int tL = add(segment("thigh_l", thL, caL, limb * 1.5f, m * 0.10f, forward));
    int cL = add(segment("calf_l", caL, ftL, limb * 1.2f, m * 0.055f, forward, height * 0.03f));
    int tR = add(segment("thigh_r", thR, caR, limb * 1.5f, m * 0.10f, forward));
    int cR = add(segment("calf_r", caR, ftR, limb * 1.2f, m * 0.055f, forward, height * 0.03f));
    (void)spine1;

    auto joint = [&](int a, int b, glm::vec3 at, bool hinge = false) {
        RagdollJoint j;
        j.bodyA = a;
        j.bodyB = b;
        j.anchor = at;
        j.hinge = hinge;
        j.hingeAxis = lateral;
        d.joints.push_back(j);
    };
    joint(pelvisB, torso, spine2);
    joint(torso, headB, neck);
    joint(torso, uaL, upL);
    joint(uaL, laL, loL);
    joint(torso, uaR, upR);
    joint(uaR, laR, loR);
    joint(pelvisB, tL, thL);
    joint(tL, cL, caL, /*hinge=*/true);
    joint(pelvisB, tR, thR);
    joint(tR, cR, caR, /*hinge=*/true);
    return d;
}

RagdollSkinBinding bindSkeletonToRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, const RagdollDesc& ragdoll) {
    // Which body each named bone rides; everything else follows its parent.
    const std::pair<const char*, const char*> map[] = {
        { "Pelvis", "pelvis" }, { "spine_01", "pelvis" }, { "spine_02", "torso" }, { "spine_03", "torso" },
        { "clavicle_l", "torso" }, { "clavicle_r", "torso" }, { "neck_01", "head" }, { "head", "head" },
        { "UpperArm_L", "upperarm_l" }, { "lowerarm_l", "lowerarm_l" }, { "Hand_L", "lowerarm_l" },
        { "UpperArm_R", "upperarm_r" }, { "lowerarm_r", "lowerarm_r" }, { "Hand_R", "lowerarm_r" },
        { "Thigh_L", "thigh_l" }, { "calf_l", "calf_l" }, { "Foot_L", "calf_l" },
        { "Thigh_R", "thigh_r" }, { "calf_r", "calf_r" }, { "Foot_R", "calf_r" },
    };
    RagdollSkinBinding binding;
    binding.bodyOfBone.assign(model.bones.size(), -1);
    binding.boneOffset.assign(model.bones.size(), glm::mat4(1.0f));
    binding.restLocal.resize(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) binding.restLocal[b] = model.bones[b].localRest;
    for (auto& [boneName, bodyName] : map) {
        int bone = findBoneCI(model, boneName);
        int body = ragdoll.findBody(bodyName);
        if (bone < 0 || body < 0 || bone >= static_cast<int>(boneWorld.size())) continue;
        binding.bodyOfBone[bone] = body;
        binding.boneOffset[bone] = rigidInverse(ragdoll.bodies[body].transform) * boneWorld[bone];
    }
    // Unmapped bones keep the local pose they had at bind time (e.g. a
    // waving hand's fingers stay as they were), not necessarily the rest pose.
    for (size_t b = 0; b < model.bones.size(); ++b) {
        int p = model.bones[b].parent;
        if (binding.bodyOfBone[b] < 0 && p >= 0 && b < boneWorld.size()) binding.restLocal[b] = glm::inverse(boneWorld[p]) * boneWorld[b];
    }
    return binding;
}

std::vector<glm::mat4> poseFromRagdoll(const ModelData& model, const RagdollSkinBinding& binding,
                                       const std::vector<glm::mat4>& bodyWorld, const glm::mat4& worldToModel) {
    std::vector<glm::mat4> world(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        int body = binding.bodyOfBone[b];
        int parent = model.bones[b].parent;
        if (body >= 0 && body < static_cast<int>(bodyWorld.size())) {
            world[b] = bodyWorld[body] * binding.boneOffset[b];
        } else if (parent >= 0) {
            world[b] = world[parent] * binding.restLocal[b];
        } else {
            world[b] = glm::inverse(worldToModel) * binding.restLocal[b];
        }
    }
    for (glm::mat4& w : world) w = worldToModel * w;
    return world;
}

} // namespace kke
