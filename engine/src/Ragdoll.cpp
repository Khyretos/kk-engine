#include "kke/Ragdoll.h"

#include <glm/gtc/quaternion.hpp>

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

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-5f ? v / len : fallback;
}

// `base` leaned toward `dir1` by deg1 and toward `dir2` by deg2 (both
// perpendicular to base; small angles combine as tangents).
glm::vec3 lean(const glm::vec3& base, const glm::vec3& dir1, float deg1, const glm::vec3& dir2 = glm::vec3(0.0f), float deg2 = 0.0f) {
    return glm::normalize(base + dir1 * std::tan(glm::radians(deg1)) + dir2 * std::tan(glm::radians(deg2)));
}

// Adds joints to a desc: oval-cone ball joints and one-way hinges.
struct JointMaker {
    RagdollDesc& d;

    void ball(const char* name, int a, int b, glm::vec3 at, glm::vec3 center, glm::vec3 bendAxis, float swing, float side, float twist) {
        RagdollJoint j;
        j.name = name;
        j.bodyA = a;
        j.bodyB = b;
        j.anchor = at;
        j.swingAxis = center;
        j.swingBendAxis = bendAxis;
        j.swingDegrees = swing;
        j.swingSideDegrees = side;
        j.twistDegrees = twist;
        d.joints.push_back(j);
    }

    // A joint that folds one way only (knee, elbow, hock): `upper` and
    // `lower` are the two segments' directions now; a straight limb folds
    // so the lower one swings toward `bendsToward` (or about `fallback` if
    // that's along the limb). A clearly bent limb tells the axis itself
    // (an elbow turns with the upper arm's twist). Positive angles = more
    // bent; range `hyper` degrees past straight to `maxBend` bent.
    void hinge(const char* name, int a, int b, glm::vec3 at, glm::vec3 upper, glm::vec3 lower, glm::vec3 bendsToward, glm::vec3 fallback,
               float maxBend, float hyper) {
        upper = unitOr(upper, glm::vec3(0, -1, 0));
        lower = unitOr(lower, upper);
        glm::vec3 axis = glm::cross(upper, bendsToward);
        if (glm::length(axis) < 0.2f) axis = fallback - upper * glm::dot(fallback, upper);
        axis = unitOr(axis, unitOr(glm::cross(upper, glm::vec3(1, 0, 0)), glm::vec3(0, 0, 1)));
        const glm::vec3 bent = glm::cross(upper, lower);
        if (glm::length(bent) > std::sin(glm::radians(20.0f)) && glm::dot(bent, axis) > 0.0f) axis = glm::normalize(bent);
        const float bend = glm::degrees(std::atan2(glm::dot(bent, axis), glm::dot(upper, lower)));
        RagdollJoint j;
        j.name = name;
        j.bodyA = a;
        j.bodyB = b;
        j.anchor = at;
        j.hinge = true;
        j.hingeAxis = axis;
        j.hingeMinDegrees = -bend - hyper;
        j.hingeMaxDegrees = maxBend - bend;
        d.joints.push_back(j);
    }
};

} // namespace

int RagdollDesc::findBody(const std::string& name) const {
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

RagdollJoint* RagdollDesc::findJoint(const std::string& name) {
    for (RagdollJoint& j : joints)
        if (j.name == name) return &j;
    return nullptr;
}

const RagdollJoint* RagdollDesc::findJoint(const std::string& name) const {
    for (const RagdollJoint& j : joints)
        if (j.name == name) return &j;
    return nullptr;
}

void RagdollDesc::scaleLimits(float factor) {
    factor = std::max(0.0f, factor);
    for (RagdollJoint& j : joints) {
        j.swingDegrees = std::min(j.swingDegrees * factor, 179.0f);
        if (j.swingSideDegrees >= 0.0f) j.swingSideDegrees = std::min(j.swingSideDegrees * factor, 179.0f);
        j.twistDegrees = std::min(j.twistDegrees * factor, 179.0f);
        // Hinges: keep where "straight" is, scale the travel from there.
        const float straight = std::clamp(0.0f, j.hingeMinDegrees, j.hingeMaxDegrees);
        j.hingeMinDegrees = std::max(straight + (j.hingeMinDegrees - straight) * factor, -180.0f);
        j.hingeMaxDegrees = std::min(straight + (j.hingeMaxDegrees - straight) * factor, 180.0f);
    }
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

    glm::vec3 pelvis = pos("Pelvis"), spine2 = pos("spine_02"), neck = pos("neck_01"), head = pos("head");
    glm::vec3 upL = pos("UpperArm_L"), loL = pos("lowerarm_l"), handL = pos("Hand_L");
    glm::vec3 upR = pos("UpperArm_R"), loR = pos("lowerarm_r"), handR = pos("Hand_R");
    glm::vec3 thL = pos("Thigh_L"), caL = pos("calf_l"), ftL = pos("Foot_L");
    glm::vec3 thR = pos("Thigh_R"), caR = pos("calf_r"), ftR = pos("Foot_R");

    // Character frame from the skeleton itself: up = pelvis -> neck,
    // lateral = right hip -> left hip, forward = their cross.
    glm::vec3 up = glm::normalize(neck - pelvis);
    glm::vec3 lateral = glm::normalize(thL - thR);
    lateral = glm::normalize(lateral - up * glm::dot(lateral, up));
    glm::vec3 forward = glm::normalize(glm::cross(lateral, up));
    float height = glm::length(neck - pelvis) * 2.9f; // proportion of a human: ~ pelvis-to-neck x 2.9
    float limb = height * 0.035f;

    RagdollDesc d;
    auto add = [&](RagdollBody b, std::vector<std::string> bones) {
        b.bones = std::move(bones);
        d.bodies.push_back(b);
        return static_cast<int>(d.bodies.size()) - 1;
    };
    float m = totalMass;
    int pelvisB = add(segment("pelvis", pelvis - up * 0.04f * height, spine2, height * 0.08f, m * 0.15f, forward), { "Pelvis", "spine_01" });
    int torso = add(segment("torso", spine2, neck, height * 0.1f, m * 0.30f, forward), { "spine_02", "spine_03", "clavicle_l", "clavicle_r" });
    int headB = add(segment("head", neck, head + up * height * 0.07f, height * 0.06f, m * 0.08f, forward), { "neck_01", "head" });
    int uaL = add(segment("upperarm_l", upL, loL, limb, m * 0.03f, forward), { "UpperArm_L" });
    int laL = add(segment("lowerarm_l", loL, handL, limb * 0.85f, m * 0.025f, forward, height * 0.06f), { "lowerarm_l", "Hand_L" });
    int uaR = add(segment("upperarm_r", upR, loR, limb, m * 0.03f, forward), { "UpperArm_R" });
    int laR = add(segment("lowerarm_r", loR, handR, limb * 0.85f, m * 0.025f, forward, height * 0.06f), { "lowerarm_r", "Hand_R" });
    int tL = add(segment("thigh_l", thL, caL, limb * 1.5f, m * 0.10f, forward), { "Thigh_L" });
    int cL = add(segment("calf_l", caL, ftL, limb * 1.2f, m * 0.055f, forward, height * 0.03f), { "calf_l", "Foot_L" });
    int tR = add(segment("thigh_r", thR, caR, limb * 1.5f, m * 0.10f, forward), { "Thigh_R" });
    int cR = add(segment("calf_r", caR, ftR, limb * 1.2f, m * 0.055f, forward, height * 0.03f), { "calf_r", "Foot_R" });

    // Human ranges of motion (Jolt; FEMFX ignores them), in the body's own
    // frame. An oval cone is centred mid-range, e.g. a hip that flexes 120
    // and extends 20 degrees is a cone 70 degrees each way, leaned 50
    // forward of straight down.
    JointMaker j{ d };
    // Spine: bends 40 forward / 25 back, 25 to either side, twists 30.
    j.ball("spine", pelvisB, torso, spine2, lean(up, forward, 7.5f), lateral, 32.5f, 25.0f, 30.0f);
    // Neck (with the head): 50 down / 60 up, 40 to the side, turns 70.
    j.ball("neck", torso, headB, neck, lean(up, -forward, 5.0f), lateral, 55.0f, 40.0f, 70.0f);
    for (int side : { 1, -1 }) {
        const bool left = side > 0;
        const glm::vec3 out = lateral * static_cast<float>(side);
        const glm::vec3 shoulder = left ? upL : upR, elbow = left ? loL : loR, hand = left ? handL : handR;
        const glm::vec3 hip = left ? thL : thR, knee = left ? caL : caR, foot = left ? ftL : ftR;
        // Shoulder: arm swings ~80 forward/back of a mid-range that points
        // out, a little down and forward; ~85 up/down from there (straight
        // down to well above the shoulder); turns 60 about itself.
        j.ball(left ? "shoulder_l" : "shoulder_r", torso, left ? uaL : uaR, shoulder, lean(out, -up, 35.0f, forward, 20.0f), up, 80.0f, 85.0f,
               60.0f);
        // Elbow: 0..145 bent toward the front, a hair of hyperextension.
        j.hinge(left ? "elbow_l" : "elbow_r", left ? uaL : uaR, left ? laL : laR, elbow, elbow - shoulder, hand - elbow, forward, -lateral, 145.0f,
                3.0f);
        // Hip: 120 forward / 20 back, 45 out / 25 in, turns 35.
        j.ball(left ? "hip_l" : "hip_r", pelvisB, left ? tL : tR, hip, lean(-up, forward, 50.0f, out, 10.0f), lateral, 70.0f, 35.0f, 35.0f);
        // Knee: 0..140 bent toward the back.
        j.hinge(left ? "knee_l" : "knee_r", left ? tL : tR, left ? cL : cR, knee, knee - hip, foot - knee, -forward, lateral, 140.0f, 3.0f);
    }
    return d;
}

RagdollDesc buildQuadrupedRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, float totalMass, std::string* missingBone,
                                  const QuadrupedBones& names) {
    std::vector<std::string> required = { names.hips, names.torso, names.shoulders, names.neck, names.head };
    for (int i = 0; i < 4; ++i) {
        required.push_back(names.upperLeg[i]);
        required.push_back(names.lowerLeg[i]);
    }
    for (const std::string& name : required) {
        const int b = findBoneCI(model, name);
        if (b < 0 || b >= static_cast<int>(boneWorld.size())) {
            if (missingBone) *missingBone = name;
            return {};
        }
    }
    const std::vector<glm::mat4> rest = computeRestPose(model);
    auto has = [&](const std::string& name) {
        const int b = findBoneCI(model, name);
        return b >= 0 && b < static_cast<int>(boneWorld.size()) && b < static_cast<int>(rest.size());
    };
    std::vector<std::string> tail;
    for (const std::string& t : names.tail)
        if (has(t)) tail.push_back(t);
    if (tail.size() < 2) tail.clear();

    // Everything is built twice: from the rest pose (to know where each
    // joint's neutral is) and from the pose now (what gets simulated).
    struct Built {
        RagdollDesc d;
        glm::vec3 up, lateral, forward;
        glm::vec3 legTop[4], knee[4], legEnd[4];
        int legUpper[4], legLower[4];
        int pelvis, chest, neck, head, tail = -1;
        glm::vec3 torsoAt, shouldersAt, headAt, tailAt;
    };
    auto build = [&](const std::vector<glm::mat4>& pose) {
        Built b;
        auto at = [&](const std::string& name) { return glm::vec3(pose[findBoneCI(model, name)][3]); };
        // Where a bone's tip is now, from where `tip` sits relative to it at rest.
        auto tipOf = [&](const std::string& bone, const glm::vec3& restTip) {
            const int i = findBoneCI(model, bone);
            return glm::vec3(pose[i] * (glm::inverse(rest[i]) * glm::vec4(restTip, 1.0f)));
        };
        for (int l = 0; l < 4; ++l) {
            b.legTop[l] = at(names.upperLeg[l]);
            b.knee[l] = at(names.lowerLeg[l]);
            const int lower = findBoneCI(model, names.lowerLeg[l]);
            const glm::vec3 restKnee(rest[lower][3]);
            const glm::vec3 restTop(rest[findBoneCI(model, names.upperLeg[l])][3]);
            const glm::vec3 restFoot = has(names.foot[l]) ? glm::vec3(rest[findBoneCI(model, names.foot[l])][3])
                                                          : restKnee + (restKnee - restTop);
            b.legEnd[l] = tipOf(names.lowerLeg[l], restFoot);
        }
        const glm::vec3 hips = at(names.hips), torso = at(names.torso), shoulders = at(names.shoulders), neck = at(names.neck),
                        head = at(names.head);
        const glm::vec3 frontTop = (b.legTop[0] + b.legTop[1]) * 0.5f, backTop = (b.legTop[2] + b.legTop[3]) * 0.5f;
        b.forward = unitOr(frontTop - backTop, glm::vec3(0, 0, 1));
        b.lateral = unitOr(b.legTop[0] + b.legTop[2] - b.legTop[1] - b.legTop[3], glm::vec3(1, 0, 0));
        b.lateral = unitOr(b.lateral - b.forward * glm::dot(b.lateral, b.forward), glm::vec3(1, 0, 0));
        b.up = glm::normalize(glm::cross(b.forward, b.lateral));
        const float width = std::max(glm::length(b.legTop[0] - b.legTop[1]), glm::length(b.legTop[2] - b.legTop[3]));
        const float body = width * 0.4f, limb = width * 0.08f;
        const float m = totalMass;

        auto add = [&](RagdollBody body, std::vector<std::string> bones) {
            body.bones = std::move(bones);
            b.d.bodies.push_back(body);
            return static_cast<int>(b.d.bodies.size()) - 1;
        };
        // Pelvis and chest meet at the torso bone and reach past the legs.
        const glm::vec3 back = unitOr(hips - torso, -b.forward), front = unitOr(shoulders - torso, b.forward);
        const float pastHips = std::max(0.0f, glm::dot(backTop - hips, back)) + body * 0.3f;
        const float pastShoulders = std::max(0.0f, glm::dot(frontTop - shoulders, front)) + body * 0.3f;
        std::vector<std::string> pelvisBones = { names.hips }, chestBones = { names.torso, names.shoulders };
        for (const auto& [bone, bodyName] : names.alsoRide) {
            if (bodyName == "pelvis") pelvisBones.push_back(bone);
            if (bodyName == "chest") chestBones.push_back(bone);
        }
        b.pelvis = add(segment("pelvis", torso, hips, body, m * 0.22f, b.up, pastHips), pelvisBones);
        b.chest = add(segment("chest", torso, shoulders, body, m * 0.34f, b.up, pastShoulders), chestBones);
        b.neck = add(segment("neck", shoulders, head, width * 0.2f, m * 0.07f, b.up), { names.neck });
        b.head = add(segment("head", head, head + (head - neck) * 0.9f, width * 0.22f, m * 0.06f, b.up), { names.head });
        const char* upperNames[4] = { "upperleg_fl", "upperleg_fr", "upperleg_bl", "upperleg_br" };
        const char* lowerNames[4] = { "lowerleg_fl", "lowerleg_fr", "lowerleg_bl", "lowerleg_br" };
        for (int l = 0; l < 4; ++l) {
            b.legUpper[l] = add(segment(upperNames[l], b.legTop[l], b.knee[l], limb * 1.3f, m * 0.045f, b.forward), { names.upperLeg[l] });
            std::vector<std::string> lowerBones = { names.lowerLeg[l] };
            if (has(names.foot[l])) lowerBones.push_back(names.foot[l]);
            b.legLower[l] = add(segment(lowerNames[l], b.knee[l], b.legEnd[l], limb, m * 0.025f, b.forward), lowerBones);
        }
        if (!tail.empty()) {
            const glm::vec3 root = at(tail.front()), tip = at(tail.back()), beforeTip = at(tail[tail.size() - 2]);
            b.tail = add(segment("tail", root, tip, limb * 0.8f, m * 0.03f, b.up, glm::length(tip - beforeTip)), tail);
            b.tailAt = root;
        }
        b.torsoAt = torso;
        b.shouldersAt = shoulders;
        b.headAt = head;
        return b;
    };
    const Built r = build(rest);
    Built n = build(boneWorld);

    // A joint's neutral direction: bodyB's direction at rest, in bodyA's
    // frame, turned with bodyA to where it is now.
    auto neutral = [&](int bodyA, int bodyB, const glm::vec3& restAnchor) {
        const glm::mat3 aRest(r.d.bodies[bodyA].transform), aNow(n.d.bodies[bodyA].transform);
        const glm::vec3 dirRest = unitOr(glm::vec3(r.d.bodies[bodyB].transform[3]) - restAnchor, -r.up);
        return glm::normalize(aNow * (glm::transpose(aRest) * dirRest));
    };
    // The body's side axis as bodyA sees it now.
    auto sideways = [&](int bodyA) {
        const glm::mat3 aRest(r.d.bodies[bodyA].transform), aNow(n.d.bodies[bodyA].transform);
        return glm::normalize(aNow * (glm::transpose(aRest) * r.lateral));
    };
    JointMaker j{ n.d };
    // A stiff back: 25 up/down, 15 sideways, 15 twist.
    j.ball("spine", n.pelvis, n.chest, n.torsoAt, neutral(n.pelvis, n.chest, r.torsoAt), sideways(n.pelvis), 25.0f, 15.0f, 15.0f);
    // Neck: 45 up/down from the rest pose, 35 sideways, 20 twist.
    j.ball("neck", n.chest, n.neck, n.shouldersAt, neutral(n.chest, n.neck, r.shouldersAt), sideways(n.chest), 45.0f, 35.0f, 20.0f);
    // Head on the neck: 35 nod, 25 sideways, 25 twist.
    j.ball("head", n.neck, n.head, n.headAt, neutral(n.neck, n.head, r.headAt), sideways(n.neck), 35.0f, 25.0f, 25.0f);
    if (n.tail >= 0)
        j.ball("tail", n.pelvis, n.tail, n.tailAt, neutral(n.pelvis, n.tail, r.tailAt), sideways(n.pelvis), 45.0f, 45.0f, 20.0f);
    const char* hipNames[4] = { "hip_fl", "hip_fr", "hip_bl", "hip_br" };
    const char* kneeNames[4] = { "knee_fl", "knee_fr", "knee_bl", "knee_br" };
    for (int l = 0; l < 4; ++l) {
        const bool frontLeg = l < 2;
        const int bodyA = frontLeg ? n.chest : n.pelvis;
        // Legs swing forward and back (45 each way), barely sideways
        // (12), and hardly turn (10).
        j.ball(hipNames[l], bodyA, n.legUpper[l], n.legTop[l], neutral(bodyA, n.legUpper[l], r.legTop[l]), sideways(bodyA), 45.0f, 12.0f,
               10.0f);
        // Front knees fold the hoof backward (0..130), hind hocks fold it
        // forward (0..110); 5 degrees the other way.
        const glm::vec3 side = sideways(bodyA);
        const glm::vec3 ahead = glm::normalize(glm::cross(side, glm::mat3(n.d.bodies[bodyA].transform) *
                                                                    (glm::transpose(glm::mat3(r.d.bodies[bodyA].transform)) * r.up)));
        j.hinge(kneeNames[l], n.legUpper[l], n.legLower[l], n.knee[l], n.knee[l] - n.legTop[l], n.legEnd[l] - n.knee[l],
                frontLeg ? -ahead : ahead, frontLeg ? side : -side, frontLeg ? 130.0f : 110.0f, 5.0f);
    }
    return n.d;
}

RagdollSkinBinding bindSkeletonToRagdoll(const ModelData& model, const std::vector<glm::mat4>& boneWorld, const RagdollDesc& ragdoll) {
    // Which body each named bone rides; everything else follows its parent.
    std::vector<std::pair<std::string, std::string>> map;
    for (const RagdollBody& body : ragdoll.bodies)
        for (const std::string& bone : body.bones) map.emplace_back(bone, body.name);
    if (map.empty()) {
        map = {
            { "Pelvis", "pelvis" }, { "spine_01", "pelvis" }, { "spine_02", "torso" }, { "spine_03", "torso" },
            { "clavicle_l", "torso" }, { "clavicle_r", "torso" }, { "neck_01", "head" }, { "head", "head" },
            { "UpperArm_L", "upperarm_l" }, { "lowerarm_l", "lowerarm_l" }, { "Hand_L", "lowerarm_l" },
            { "UpperArm_R", "upperarm_r" }, { "lowerarm_r", "lowerarm_r" }, { "Hand_R", "lowerarm_r" },
            { "Thigh_L", "thigh_l" }, { "calf_l", "calf_l" }, { "Foot_L", "calf_l" },
            { "Thigh_R", "thigh_r" }, { "calf_r", "calf_r" }, { "Foot_R", "calf_r" },
        };
    }
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

std::vector<glm::mat4> blendPoses(const std::vector<glm::mat4>& from, const std::vector<glm::mat4>& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    std::vector<glm::mat4> out = to;
    const size_t n = std::min(from.size(), to.size());
    for (size_t i = 0; i < n; ++i) {
        glm::vec3 scale(glm::length(glm::vec3(to[i][0])), glm::length(glm::vec3(to[i][1])), glm::length(glm::vec3(to[i][2])));
        auto rotationOf = [](const glm::mat4& m) {
            glm::mat3 r(m);
            for (int c = 0; c < 3; ++c) {
                const float len = glm::length(r[c]);
                r[c] = len > 1e-12f ? r[c] / len : glm::vec3(0.0f);
            }
            return glm::quat_cast(r);
        };
        const glm::quat q = glm::slerp(rotationOf(from[i]), rotationOf(to[i]), t);
        const glm::vec3 p = glm::mix(glm::vec3(from[i][3]), glm::vec3(to[i][3]), t);
        glm::mat4 m = glm::mat4_cast(glm::normalize(q));
        for (int c = 0; c < 3; ++c) m[c] *= scale[c];
        m[3] = glm::vec4(p, 1.0f);
        out[i] = m;
    }
    return out;
}

float blendWeight(float elapsed, float duration) {
    if (duration <= 0.0f) return 1.0f;
    const float x = std::clamp(elapsed / duration, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace kke
