#include "kke/CharacterIk.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace kke {

namespace {

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// Shortest rotation taking unit `from` onto unit `to`.
glm::quat turnBetween(const glm::vec3& from, const glm::vec3& to) {
    const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
    const glm::vec3 axis = glm::cross(from, to);
    const float s = glm::length(axis);
    if (s < 1e-6f) return glm::quat(1, 0, 0, 0);
    return glm::angleAxis(std::atan2(s, d), axis / s);
}

int findBone(const ModelData& model, const char* name) {
    for (size_t b = 0; b < model.bones.size(); ++b)
        if (canonicalBoneName(model.bones[b].name) == name) return static_cast<int>(b);
    return -1;
}

float fade(float dt, float rate) { return dt > 0.0f ? 1.0f - std::exp(-rate * dt) : 1.0f; }

} // namespace

CharacterIk::CharacterIk(const ModelData& rig, const ModelData* skinned) : CharacterIk(rig, skinned, Settings{}) {}

CharacterIk::CharacterIk(const ModelData& rig, const ModelData* skinned, const Settings& settings) : m_s(settings) {
    m_leg[Left] = findChain(rig, "thigh_l", "calf_l", "foot_l");
    m_leg[Right] = findChain(rig, "thigh_r", "calf_r", "foot_r");
    m_feet = FootPlacer(rig, m_leg[Left], m_leg[Right], findBone(rig, "pelvis"), m_s.feet);
    const TwoBoneChain armL = findChain(rig, "upperarm_l", "lowerarm_l", "hand_l");
    const TwoBoneChain armR = findChain(rig, "upperarm_r", "lowerarm_r", "hand_r");
    m_arm[Left] = makeHumanArm(rig, armL, armR);
    m_arm[Right] = makeHumanArm(rig, armR, armL);
    const ModelData& shape = skinned && skinned->bones.size() == rig.bones.size() ? *skinned : rig;
    m_body = BodyShape::fit(shape);
    m_spine[0] = findBone(rig, "spine_01");
    m_spine[1] = findBone(rig, "spine_02");
    m_spine[2] = findBone(rig, "spine_03");
    m_forward = modelForward(rig);
    // The ankle's height above the sole at rest (the lowest of the foot
    // and its ball, or the floor).
    const std::vector<glm::mat4> rest = computeRestPose(rig);
    for (int i = 0; i < 2; ++i) {
        if (!m_leg[i].valid()) continue;
        const int foot = m_leg[i].end;
        float ankle = positionOf(rest[static_cast<size_t>(foot)]).y, low = std::min(0.0f, ankle);
        for (size_t b = 0; b < rig.bones.size(); ++b)
            if (rig.bones[b].parent == foot) low = std::min(low, positionOf(rest[b]).y);
        m_ankleRest[i] = std::max(0.02f, ankle - low);
    }
}

void CharacterIk::hand(Side side, const glm::vec3& point, const std::optional<glm::vec3>& elbowToward) {
    m_hand[side] = { point, elbowToward, true };
}

void CharacterIk::foot(Side side, const glm::vec3& point, const glm::vec3& normal) {
    const float l = glm::length(normal);
    m_foot[side] = { point, l > 1e-6f ? normal / l : glm::vec3(0, 1, 0), true };
}

void CharacterIk::reset() {
    for (int i = 0; i < 2; ++i) {
        m_hand[i] = m_lastHand[i] = HandContact{};
        m_foot[i] = m_lastFoot[i] = FootContact{};
        m_handW[i] = m_footW[i] = 0.0f;
        m_avoid[i] = BodyAvoidState{};
    }
    m_haveVelocity = false;
    m_lean = glm::vec3(0.0f);
}

void CharacterIk::apply(const ModelData& rig, Pose& pose, const glm::mat4& toWorld, const GroundQuery& ground, const glm::vec3& velocity, float dt) {
    const glm::mat4 toModel = glm::inverse(toWorld);
    for (int i = 0; i < 2; ++i) {
        if (m_hand[i].set) m_lastHand[i] = m_hand[i];
        if (m_foot[i].set) m_lastFoot[i] = m_foot[i];
        m_handW[i] += ((m_hand[i].set ? 1.0f : 0.0f) - m_handW[i]) * fade(dt, m_s.handBlend);
        m_footW[i] += ((m_foot[i].set ? 1.0f : 0.0f) - m_footW[i]) * fade(dt, m_s.footBlend);
        if (m_handW[i] < 1e-3f) m_handW[i] = 0.0f;
        if (m_footW[i] < 1e-3f) m_footW[i] = 0.0f;
    }
    m_groundW += ((m_groundWanted ? 1.0f : 0.0f) - m_groundW) * fade(dt, m_s.groundBlend);

    // 3 first, on the spine: the arms are then solved from where the
    // leaning chest put the shoulders.
    leanInto(rig, pose, toWorld, velocity, dt);

    // 2. Feet on the ground, then the planted ones where they're planted.
    if (m_feet.valid()) {
        m_feet.settings() = m_s.feet;
        auto query = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            if (!ground) return false;
            glm::vec3 h, n;
            if (!ground(glm::vec3(toWorld * glm::vec4(from, 1.0f)), h, n)) return false;
            hit = glm::vec3(toModel * glm::vec4(h, 1.0f));
            normal = glm::normalize(glm::mat3(toModel) * n);
            return true;
        };
        m_feet.apply(rig, pose, FootPlacer::SurfaceQuery(query), dt, m_groundW);
    }
    plantFeet(rig, pose, toModel);
    placeHands(rig, pose, toModel);

    for (int i = 0; i < 2; ++i) {
        m_hand[i].set = false;
        m_foot[i].set = false;
    }
}

void CharacterIk::leanInto(const ModelData& rig, Pose& pose, const glm::mat4& toWorld, const glm::vec3& velocity, float dt) {
    if (dt <= 0.0f) return;
    // Acceleration from a smoothed velocity (physics moves the body at its
    // own rate: a raw difference is zero one frame and a spike the next).
    glm::vec3 accel(0.0f);
    if (!m_haveVelocity) {
        m_velocity = velocity;
        m_haveVelocity = true;
    } else {
        const glm::vec3 before = m_velocity;
        m_velocity += (velocity - m_velocity) * fade(dt, 12.0f);
        accel = (m_velocity - before) / dt;
    }
    accel.y = 0.0f;
    // Into model space: which way, and how far (degrees), the chest leans.
    glm::mat3 r(toWorld);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    glm::vec3 want = glm::transpose(r) * accel * m_s.lean;
    want.y = 0.0f;
    if (glm::length(want) > m_s.maxLean) want = glm::normalize(want) * m_s.maxLean;
    m_lean += (want - m_lean) * fade(dt, m_s.leanSmoothing);
    const float angle = glm::length(m_lean);
    if (angle < 0.05f) return;
    const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), m_lean / angle));
    // Shared up the spine: lower bones take a little less.
    const float shares[3] = { 0.3f, 0.35f, 0.35f };
    float total = 0.0f;
    for (int i = 0; i < 3; ++i)
        if (m_spine[i] >= 0) total += shares[i];
    if (total <= 0.0f) return;
    for (int i = 0; i < 3; ++i) {
        const int b = m_spine[i];
        if (b < 0) continue;
        const std::vector<glm::mat4> world = poseToModel(rig, pose);
        const glm::quat boneWorld = rotationOf(world[static_cast<size_t>(b)]);
        const glm::quat delta = glm::angleAxis(glm::radians(angle) * shares[i] / total, axis);
        pose[static_cast<size_t>(b)].r = glm::normalize(pose[static_cast<size_t>(b)].r * (glm::inverse(boneWorld) * delta * boneWorld));
    }
}

void CharacterIk::plantFeet(const ModelData& rig, Pose& pose, const glm::mat4& toModel) {
    const glm::vec3 up(0, 1, 0);
    for (int i = 0; i < 2; ++i) {
        const float w = m_footW[i];
        const TwoBoneChain& leg = m_leg[i];
        if (w <= 0.0f || !leg.valid()) continue;
        const FootContact& c = m_foot[i].set ? m_foot[i] : m_lastFoot[i];
        const glm::vec3 sole = glm::vec3(toModel * glm::vec4(c.point, 1.0f));
        glm::vec3 n = glm::normalize(glm::mat3(toModel) * c.normal);
        // At most footContactTilt from level.
        const float tilt = std::acos(glm::clamp(n.y, -1.0f, 1.0f)), maxTilt = glm::radians(m_s.footContactTilt);
        if (tilt > maxTilt) {
            const glm::vec3 axis = glm::cross(up, n);
            if (glm::length(axis) > 1e-5f) n = glm::angleAxis(maxTilt, glm::normalize(axis)) * up;
        }
        const std::vector<glm::mat4> before = poseToModel(rig, pose);
        const glm::quat animatedFoot = rotationOf(before[static_cast<size_t>(leg.end)]);
        const glm::vec3 hip = positionOf(before[static_cast<size_t>(leg.upper)]);
        const glm::vec3 knee = positionOf(before[static_cast<size_t>(leg.lower)]);
        const glm::vec3 ankle = positionOf(before[static_cast<size_t>(leg.end)]);
        // The knee keeps bending the way it bends now; straight, forward.
        glm::vec3 bend = knee - (hip + ankle) * 0.5f;
        if (glm::length(bend) < 1e-3f) bend = m_forward;
        solveTwoBone(rig, pose, leg, sole + n * m_ankleRest[i], knee + glm::normalize(bend) * 0.5f, w);
        // The sole along what it stands on.
        const std::vector<glm::mat4> solved = poseToModel(rig, pose);
        const glm::quat placed = turnBetween(up, n) * animatedFoot;
        const glm::quat footModel = glm::slerp(rotationOf(solved[static_cast<size_t>(leg.end)]), placed, w);
        const int parent = rig.bones[static_cast<size_t>(leg.end)].parent;
        const glm::quat parentModel = parent >= 0 ? rotationOf(solved[static_cast<size_t>(parent)]) : glm::quat(1, 0, 0, 0);
        pose[static_cast<size_t>(leg.end)].r = glm::normalize(glm::inverse(parentModel) * footModel);
    }
}

void CharacterIk::placeHands(const ModelData& rig, Pose& pose, const glm::mat4& toModel) {
    for (int i = 0; i < 2; ++i) {
        const float w = m_handW[i];
        if (w <= 0.0f || !m_arm[i].valid()) {
            if (w <= 0.0f) m_avoid[i] = BodyAvoidState{};
            continue;
        }
        const HandContact& c = m_hand[i].set ? m_hand[i] : m_lastHand[i];
        ArmGoal goal;
        goal.hand = glm::vec3(toModel * glm::vec4(c.point, 1.0f));
        if (c.elbow) goal.elbowToward = glm::vec3(toModel * glm::vec4(*c.elbow, 1.0f));
        goal.weight = w;
        if (m_body.valid()) solveHumanArm(rig, pose, m_arm[i], goal, m_body, m_s.avoid, &m_avoid[i], m_s.arm);
        else solveHumanArm(rig, pose, m_arm[i], goal, m_s.arm);
    }
}

} // namespace kke
