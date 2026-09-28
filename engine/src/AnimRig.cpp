#include "kke/AnimRig.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace kke {

namespace {

// Rotation of a transform that may carry (uniform) scale.
glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

glm::mat4 compose(const BoneTRS& b) {
    return glm::translate(glm::mat4(1.0f), b.t) * glm::mat4_cast(b.r) * glm::scale(glm::mat4(1.0f), b.s);
}

// Shortest rotation taking direction `from` onto `to`.
glm::quat rotationBetween(glm::vec3 from, glm::vec3 to) {
    const float lf = glm::length(from), lt = glm::length(to);
    if (lf < 1e-8f || lt < 1e-8f) return glm::quat(1, 0, 0, 0);
    from /= lf;
    to /= lt;
    const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (d > 0.99999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.99999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::length(axis) < 1e-4f) axis = glm::cross(glm::vec3(0, 1, 0), from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    return glm::angleAxis(std::acos(d), glm::normalize(glm::cross(from, to)));
}

// Turns bone `b` by the model-space rotation `worldDelta`, keeping the
// pose local (the change is re-expressed in the bone's own space).
void rotateInModel(Pose& pose, int b, const glm::quat& boneWorld, const glm::quat& worldDelta) {
    pose[b].r = glm::normalize(pose[b].r * (glm::inverse(boneWorld) * worldDelta * boneWorld));
}

float acosClamped(float x) { return std::acos(glm::clamp(x, -1.0f, 1.0f)); }

} // namespace

std::vector<glm::mat4> poseToModel(const ModelData& model, const Pose& pose) {
    std::vector<glm::mat4> world(model.bones.size());
    for (size_t b = 0; b < model.bones.size() && b < pose.size(); ++b) {
        const int p = model.bones[b].parent;
        world[b] = p >= 0 ? world[p] * compose(pose[b]) : compose(pose[b]);
    }
    return world;
}

// ---------------------------------------------------------------------
// Two-bone IK

TwoBoneChain findChain(const ModelData& model, const std::string& upper, const std::string& lower, const std::string& end) {
    auto find = [&](const std::string& name) {
        const std::string want = canonicalBoneName(name);
        for (size_t i = 0; i < model.bones.size(); ++i)
            if (canonicalBoneName(model.bones[i].name) == want) return static_cast<int>(i);
        return -1;
    };
    return { find(upper), find(lower), find(end) };
}

void solveTwoBone(const ModelData& model, Pose& pose, const TwoBoneChain& chain, const glm::vec3& targetIn, const glm::vec3& pole,
                  float weight) {
    if (!chain.valid() || weight <= 0.0f) return;
    std::vector<glm::mat4> world = poseToModel(model, pose);
    const glm::vec3 a = positionOf(world[chain.upper]);
    const glm::vec3 b = positionOf(world[chain.lower]);
    const glm::vec3 c = positionOf(world[chain.end]);
    const glm::vec3 t = glm::mix(c, targetIn, glm::clamp(weight, 0.0f, 1.0f));
    const float lab = glm::length(b - a), lcb = glm::length(c - b);
    if (lab < 1e-5f || lcb < 1e-5f) return;
    const float eps = 1e-3f;
    const float lat = glm::clamp(glm::length(t - a), eps, lab + lcb - eps);

    // 1. Bend: set the knee angle so the chain's reach is |t - a|,
    //    bending in the plane of the chain and the pole.
    const float acAb0 = acosClamped(glm::dot(glm::normalize(c - a), glm::normalize(b - a)));
    const float baBc0 = acosClamped(glm::dot(glm::normalize(a - b), glm::normalize(c - b)));
    const float acAb1 = acosClamped((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat));
    const float baBc1 = acosClamped((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb));
    glm::vec3 axis = glm::cross(c - a, pole - a);
    if (glm::length(axis) < 1e-6f) axis = glm::cross(c - a, b - a);
    if (glm::length(axis) > 1e-6f) {
        axis = glm::normalize(axis);
        // Positive angles bend toward the pole side.
        const glm::quat upperWorld = rotationOf(world[chain.upper]);
        const glm::quat lowerWorld = rotationOf(world[chain.lower]);
        rotateInModel(pose, chain.upper, upperWorld, glm::angleAxis(acAb1 - acAb0, axis));
        rotateInModel(pose, chain.lower, lowerWorld, glm::angleAxis(baBc1 - baBc0, axis));
    }

    // 2. Swing the whole chain so its end points at the target.
    world = poseToModel(model, pose);
    const glm::vec3 c2 = positionOf(world[chain.end]);
    rotateInModel(pose, chain.upper, rotationOf(world[chain.upper]), rotationBetween(c2 - a, t - a));
}

// ---------------------------------------------------------------------
// A person's arm

namespace {

// Rotation taking the orthonormal pair (a, b) onto (A, B) (b, B are made
// perpendicular to a, A first).
glm::quat frameTurn(const glm::vec3& a, const glm::vec3& b, const glm::vec3& A, const glm::vec3& B) {
    auto frame = [](glm::vec3 x, glm::vec3 y) {
        x = glm::normalize(x);
        y = glm::normalize(y - x * glm::dot(x, y));
        return glm::mat3(x, y, glm::cross(x, y));
    };
    return glm::normalize(glm::quat_cast(frame(A, B) * glm::transpose(frame(a, b))));
}

// `v` without its part along unit `n`, normalised; false when nothing is left.
bool across(const glm::vec3& v, const glm::vec3& n, glm::vec3& out) {
    const glm::vec3 p = v - n * glm::dot(v, n);
    const float l = glm::length(p);
    if (l < 1e-4f) return false;
    out = p / l;
    return true;
}

// q = twist * swing, twist about unit `axis`; returns the twist's signed angle.
float splitTwist(const glm::quat& q, const glm::vec3& axis, glm::quat& swing) {
    const glm::vec3 v(q.x, q.y, q.z);
    const glm::vec3 p = axis * glm::dot(v, axis);
    glm::quat twist(q.w, p.x, p.y, p.z);
    const float l = glm::length(twist);
    twist = l < 1e-6f ? glm::quat(1, 0, 0, 0) : twist / l;
    swing = glm::inverse(twist) * q;
    float angle = 2.0f * std::atan2(glm::dot(glm::vec3(twist.x, twist.y, twist.z), axis), twist.w);
    if (angle > glm::pi<float>()) angle -= glm::two_pi<float>();
    if (angle < -glm::pi<float>()) angle += glm::two_pi<float>();
    return angle;
}

} // namespace

HumanArm makeHumanArm(const ModelData& model, const TwoBoneChain& chain, const TwoBoneChain& other) {
    HumanArm arm;
    if (!chain.valid()) return arm;
    arm.chain = chain;
    arm.otherShoulder = other.valid() ? other.upper : -1;
    arm.forward = modelForward(model);
    std::vector<glm::mat4> world(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b)
        world[b] = model.bones[b].parent >= 0 ? world[model.bones[b].parent] * model.bones[b].localRest : model.bones[b].localRest;
    const glm::vec3 up(0, 1, 0), leftDir = glm::cross(up, arm.forward);
    const glm::vec3 s = positionOf(world[chain.upper]), e = positionOf(world[chain.lower]), h = positionOf(world[chain.end]);
    arm.left = arm.otherShoulder >= 0 ? glm::dot(s - positionOf(world[arm.otherShoulder]), leftDir) > 0.0f : glm::dot(s, leftDir) > 0.0f;
    // The elbow's hinge: bending turns the forearm toward the front, so
    // the axis is the bone's direction crossed with forward (at rest a
    // person's arms hang or stretch out sideways: the axis is well defined).
    auto local = [&](int bone, const glm::vec3& dir, glm::vec3& axis, glm::vec3& hinge) {
        const glm::quat inv = glm::inverse(rotationOf(world[bone]));
        glm::vec3 hw = glm::cross(glm::normalize(dir), arm.forward);
        if (glm::length(hw) < 1e-4f) hw = glm::cross(glm::normalize(dir), up);
        axis = glm::normalize(inv * dir);
        hinge = glm::normalize(inv * glm::normalize(hw));
    };
    local(chain.upper, e - s, arm.upperAxis, arm.upperHinge);
    local(chain.lower, h - e, arm.lowerAxis, arm.lowerHinge);
    arm.handRest = glm::normalize(glm::quat_cast(glm::mat3(model.bones[static_cast<size_t>(chain.end)].localRest)));
    return arm;
}

ArmResult solveHumanArm(const ModelData& model, Pose& pose, const HumanArm& arm, const ArmGoal& goal, const ArmLimits& limits) {
    ArmResult out;
    const TwoBoneChain& c = arm.chain;
    if (!arm.valid()) return out;
    std::vector<glm::mat4> world = poseToModel(model, pose);
    const glm::vec3 S = positionOf(world[c.upper]);
    out.hand = positionOf(world[c.end]);
    out.handRotation = rotationOf(world[c.end]);
    const float w = glm::clamp(goal.weight, 0.0f, 1.0f);
    if (w <= 0.0f) return out;
    const float lab = glm::length(positionOf(world[c.lower]) - S), lcb = glm::length(out.hand - positionOf(world[c.lower]));
    if (lab < 1e-5f || lcb < 1e-5f) return out;

    // The chest's frame: out along the shoulders' line (a turned torso
    // turns it), up, forward.
    const glm::vec3 up(0, 1, 0);
    glm::vec3 outDir;
    if (arm.otherShoulder < 0 || !across(S - positionOf(world[arm.otherShoulder]), up, outDir)) {
        const glm::vec3 leftDir = glm::normalize(glm::cross(up, arm.forward));
        outDir = arm.left ? leftDir : -leftDir;
    }
    // Left = cross(up, forward), so forward = cross(left, up) = cross(up, right).
    const glm::vec3 fwd = arm.left ? glm::cross(outDir, up) : glm::cross(up, outDir);

    // 1. Where the hand can go: within reach (never closer than the
    //    elbow's full bend allows) and within the shoulder's range.
    glm::vec3 d = glm::mix(out.hand, goal.hand, w) - S;
    const float reachMax = lab + lcb - 1e-3f;
    const float inner = glm::radians(180.0f - limits.elbowMaxFlex);
    const float reachMin = std::sqrt(std::max(1e-6f, lab * lab + lcb * lcb - 2.0f * lab * lcb * std::cos(inner)));
    float len = glm::length(d);
    if (len < 1e-5f) {
        d = -up * reachMin;
        len = reachMin;
    }
    const float L = glm::clamp(len, reachMin, reachMax);
    out.limited = len > reachMax + 1e-3f || len < reachMin - 1e-3f;
    glm::vec3 local(glm::dot(d, outDir), glm::dot(d, up), glm::dot(d, fwd));
    const float flat = std::sqrt(local.x * local.x + local.z * local.z);
    if (flat > 1e-4f) {
        const float bent = glm::clamp((reachMax - L) / std::max(1e-4f, reachMax - reachMin), 0.0f, 1.0f);
        const float lo = -glm::radians(glm::mix(limits.acrossChest, limits.acrossChestBent, bent));
        const float hi = glm::radians(limits.behind);
        const float phi = std::atan2(local.x, local.z); // 0 ahead, +90 out to the side, 180 behind
        const float kept = glm::clamp(phi, lo, hi);
        if (kept != phi) {
            local.x = flat * std::sin(kept);
            local.z = flat * std::cos(kept);
            out.limited = true;
        }
    }
    const glm::vec3 n = glm::normalize(outDir * local.x + up * local.y + fwd * local.z);
    const glm::vec3 H = S + n * L;

    // 2. The elbow: on the circle round the shoulder-hand line, where it
    //    hangs naturally, swivelled toward the hint by at most `swivel`.
    const float cosA = glm::clamp((lab * lab + L * L - lcb * lcb) / (2.0f * lab * L), -1.0f, 1.0f);
    const glm::vec3 centre = S + n * (lab * cosA);
    const float radius = lab * std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    glm::vec3 e;
    if (!across(-up + outDir * 0.35f - fwd * 0.15f, n, e) && !across(-fwd, n, e) && !across(outDir, n, e)) e = up;
    if (goal.elbowToward) {
        glm::vec3 hint;
        if (across(*goal.elbowToward - S, n, hint)) {
            const float angle = std::atan2(glm::dot(glm::cross(e, hint), n), glm::dot(e, hint));
            const float lim = glm::radians(limits.swivel);
            e = glm::angleAxis(glm::clamp(angle, -lim, lim), n) * e;
        }
    }
    const glm::vec3 E = centre + e * radius;

    // 3. Each bone from its direction and the hinge (the elbow's inside
    //    faces away from its point).
    const glm::vec3 dirU = glm::normalize(E - S), dirL = glm::normalize(H - E);
    glm::vec3 hinge = glm::cross(dirU, -e);
    if (glm::length(hinge) < 1e-5f) hinge = glm::cross(dirU, up);
    hinge = glm::normalize(hinge);
    const glm::quat upperW = frameTurn(arm.upperAxis, arm.upperHinge, dirU, hinge);
    glm::quat lowerW = frameTurn(arm.lowerAxis, arm.lowerHinge, dirL, hinge);
    const int parent = model.bones[static_cast<size_t>(c.upper)].parent;
    const glm::quat parentW = parent >= 0 ? rotationOf(world[static_cast<size_t>(parent)]) : glm::quat(1, 0, 0, 0);
    const glm::quat animatedHandLocal = pose[static_cast<size_t>(c.end)].r;
    const glm::quat handW = goal.handRotation ? glm::slerp(out.handRotation, *goal.handRotation, w) : out.handRotation;
    glm::quat upperLocal = glm::inverse(parentW) * upperW;
    glm::quat lowerLocal = glm::inverse(upperW) * lowerW;

    // 4. The hand: the forearm takes the twist (pronation), the wrist
    //    bends and twists a little; anything beyond is left out.
    glm::quat handLocal = goal.handRotation ? glm::inverse(lowerW) * handW : animatedHandLocal;
    glm::quat swing;
    const float twist = splitTwist(handLocal * glm::inverse(arm.handRest), arm.lowerAxis, swing);
    const float pron = glm::radians(limits.pronation), wristTwist = glm::radians(limits.wristTwist);
    const float forearm = goal.handRotation ? glm::clamp(twist, -pron, pron) : 0.0f;
    const float rest = glm::clamp(twist - forearm, -wristTwist, wristTwist);
    const float swingAngle = 2.0f * std::acos(glm::clamp(std::abs(swing.w), 0.0f, 1.0f));
    const float bendMax = glm::radians(limits.wristBend);
    if (swingAngle > bendMax) swing = glm::slerp(glm::quat(1, 0, 0, 0), swing.w < 0.0f ? -swing : swing, bendMax / swingAngle);
    if (std::abs(rest - (twist - forearm)) > 1e-3f || swingAngle > bendMax + 1e-3f) out.limited = true;
    lowerLocal = lowerLocal * glm::angleAxis(forearm, arm.lowerAxis);
    handLocal = glm::angleAxis(rest, arm.lowerAxis) * swing * arm.handRest;

    pose[static_cast<size_t>(c.upper)].r = glm::normalize(upperLocal);
    pose[static_cast<size_t>(c.lower)].r = glm::normalize(lowerLocal);
    pose[static_cast<size_t>(c.end)].r = glm::normalize(handLocal);
    world = poseToModel(model, pose);
    out.hand = positionOf(world[c.end]);
    out.handRotation = rotationOf(world[c.end]);
    return out;
}

// ---------------------------------------------------------------------
// Foot placement

FootPlacer::FootPlacer(const ModelData& model, const TwoBoneChain& left, const TwoBoneChain& right, int pelvis)
    : FootPlacer(model, left, right, pelvis, Settings{}) {}

FootPlacer::FootPlacer(const ModelData&, const TwoBoneChain& left, const TwoBoneChain& right, int pelvis, const Settings& s)
    : m_left(left), m_right(right), m_pelvis(pelvis), m_s(s) {}

void FootPlacer::apply(const ModelData& model, Pose& pose, const GroundQuery& ground, float dt, float weight) {
    SurfaceQuery flat;
    if (ground)
        flat = [&ground](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return ground(from, hit);
        };
    apply(model, pose, flat, dt, weight);
}

void FootPlacer::apply(const ModelData& model, Pose& pose, const SurfaceQuery& ground, float dt, float weight) {
    if (!valid()) return;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float maxTilt = glm::radians(m_s.maxTilt);
    glm::vec3 wantNormal[2] = { up, up };
    weight = glm::clamp(weight, 0.0f, 1.0f);
    const std::vector<glm::mat4> world = poseToModel(model, pose);
    const TwoBoneChain* legs[2] = { &m_left, &m_right };
    glm::vec3 feet[2];
    float want[2] = { 0.0f, 0.0f };
    for (int i = 0; i < 2; ++i) {
        feet[i] = positionOf(world[legs[i]->end]);
        glm::vec3 hit, normal;
        // The capsule stands at model y = 0; a foot's ground above or
        // below that is how far the foot should move.
        if (weight > 0.0f && ground && ground(feet[i] + glm::vec3(0.0f, m_s.probeUp, 0.0f), hit, normal)) {
            want[i] = glm::clamp(hit.y, -m_s.maxDrop, m_s.maxRaise);
            // Tilt toward the slope, at most maxTilt, scaled by weight.
            const glm::vec3 axis = glm::cross(up, normal);
            const float s = glm::length(axis);
            if (s > 1e-4f && normal.y > 0.0f) {
                const float angle = std::min(std::atan2(s, normal.y), maxTilt) * weight;
                wantNormal[i] = glm::angleAxis(angle, axis / s) * up;
            }
        }
    }
    const float wantPelvis = std::max(-m_s.maxDrop, std::min(0.0f, std::min(want[0], want[1])));
    const float k = dt > 0.0f ? 1.0f - std::exp(-m_s.smoothing * dt) : 1.0f;
    for (int i = 0; i < 2; ++i) {
        m_footOffset[i] += (want[i] - m_footOffset[i]) * k;
        m_footNormal[i] = glm::normalize(m_footNormal[i] + (wantNormal[i] - m_footNormal[i]) * k);
    }
    m_pelvisOffset += (wantPelvis - m_pelvisOffset) * k;
    const bool level = m_footNormal[0].y > 0.99999f && m_footNormal[1].y > 0.99999f;
    if (level && std::abs(m_pelvisOffset) < 1e-5f && std::abs(m_footOffset[0]) < 1e-5f && std::abs(m_footOffset[1]) < 1e-5f) return;

    // Lower the pelvis (in its parent's space).
    const int parent = model.bones[m_pelvis].parent;
    const glm::mat4 parentWorld = parent >= 0 ? world[parent] : glm::mat4(1.0f);
    const glm::vec3 pelvisPos = positionOf(world[m_pelvis]) + glm::vec3(0.0f, m_pelvisOffset, 0.0f);
    pose[m_pelvis].t = glm::vec3(glm::inverse(parentWorld) * glm::vec4(pelvisPos, 1.0f));

    // Then each leg reaches its foot's ground.
    const std::vector<glm::mat4> lowered = poseToModel(model, pose);
    for (int i = 0; i < 2; ++i) {
        const glm::vec3 hip = positionOf(lowered[legs[i]->upper]);
        const glm::vec3 knee = positionOf(lowered[legs[i]->lower]);
        const glm::vec3 target = feet[i] + glm::vec3(0.0f, m_footOffset[i], 0.0f);
        // Keep the knee bending the way the animation bends it; a
        // straight leg bends forward.
        glm::vec3 bend = knee - (hip + positionOf(lowered[legs[i]->end])) * 0.5f;
        if (glm::length(bend) < 1e-3f) bend = glm::vec3(0.0f, 0.0f, 1.0f);
        solveTwoBone(model, pose, *legs[i], target, knee + glm::normalize(bend) * 0.5f);
    }

    // Finally the feet lie along the ground: the animated foot rotation
    // (from before the leg IK, which would otherwise swing it along with
    // the calf), turned in model space by the tilt from level to the slope.
    if (level) return;
    const std::vector<glm::mat4> solved = poseToModel(model, pose);
    for (int i = 0; i < 2; ++i) {
        if (m_footNormal[i].y > 0.99999f) continue;
        const int foot = legs[i]->end, parentBone = model.bones[foot].parent;
        const glm::quat tilt = rotationBetween(up, m_footNormal[i]);
        const glm::quat footModel = tilt * rotationOf(world[foot]);
        const glm::quat parentModel = parentBone >= 0 ? rotationOf(solved[parentBone]) : glm::quat(1, 0, 0, 0);
        pose[foot].r = glm::normalize(glm::inverse(parentModel) * footModel);
    }
}

// ---------------------------------------------------------------------
// Retargeting

std::string canonicalBoneName(const std::string& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (auto colon = n.rfind(':'); colon != std::string::npos) n = n.substr(colon + 1); // "mixamorig:Hips"
    for (const char* prefix : { "bip01_", "bip01 ", "def-", "b_" })
        if (n.rfind(prefix, 0) == 0) n = n.substr(std::string(prefix).size());
    auto replace = [&](const std::string& from, const std::string& to) {
        if (auto p = n.find(from); p != std::string::npos) n.replace(p, from.size(), to);
    };
    replace("indexfinger", "index");            // Synty
    if (n.rfind("finger_", 0) == 0) n = "middle_" + n.substr(7); // Synty's merged fingers
    replace("_leaf", "");                       // UAL end markers
    if (n == "hips") n = "pelvis";
    return n;
}

glm::vec3 modelForward(const ModelData& model) {
    std::vector<glm::mat4> world(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b)
        world[b] = model.bones[b].parent >= 0 ? world[model.bones[b].parent] * model.bones[b].localRest : model.bones[b].localRest;
    auto find = [&](const char* name) {
        for (size_t i = 0; i < model.bones.size(); ++i)
            if (canonicalBoneName(model.bones[i].name) == name) return static_cast<int>(i);
        return -1;
    };
    for (auto [l, r] : { std::pair{ "thigh_l", "thigh_r" }, std::pair{ "upperarm_l", "upperarm_r" } }) {
        const int li = find(l), ri = find(r);
        if (li < 0 || ri < 0) continue;
        glm::vec3 left = positionOf(world[li]) - positionOf(world[ri]);
        left.y = 0.0f;
        if (glm::length(left) < 1e-4f) continue;
        // Facing +Z, a character's left is +X (Y up, right-handed).
        return glm::normalize(glm::cross(glm::normalize(left), glm::vec3(0, 1, 0)));
    }
    return glm::vec3(0, 0, 1);
}

BoneMatch matchBones(const ModelData& source, const ModelData& target) {
    std::unordered_map<std::string, int> byName;
    for (size_t i = 0; i < source.bones.size(); ++i) byName.emplace(canonicalBoneName(source.bones[i].name), static_cast<int>(i));
    BoneMatch m;
    m.sourceOf.assign(target.bones.size(), -1);
    for (size_t i = 0; i < target.bones.size(); ++i) {
        auto it = byName.find(canonicalBoneName(target.bones[i].name));
        if (it != byName.end()) {
            m.sourceOf[i] = it->second;
            ++m.matched;
        } else {
            m.unmatchedTarget.push_back(target.bones[i].name);
        }
    }
    return m;
}

std::vector<ModelAnimation> retargetAnimations(const ModelData& source, const ModelData& target, const BoneMatch& match) {
    std::vector<ModelAnimation> out;
    const size_t ns = source.bones.size(), nt = target.bones.size();
    if (match.sourceOf.size() != nt) return out;

    auto restWorld = [](const ModelData& m) {
        std::vector<glm::mat4> w(m.bones.size());
        for (size_t b = 0; b < m.bones.size(); ++b)
            w[b] = m.bones[b].parent >= 0 ? w[m.bones[b].parent] * m.bones[b].localRest : m.bones[b].localRest;
        return w;
    };
    const std::vector<glm::mat4> srcRest = restWorld(source), tgtRest = restWorld(target);
    std::vector<glm::quat> srcRestRot(ns), tgtRestRot(nt);
    for (size_t b = 0; b < ns; ++b) srcRestRot[b] = rotationOf(srcRest[b]);
    for (size_t b = 0; b < nt; ++b) tgtRestRot[b] = rotationOf(tgtRest[b]);
    std::vector<BoneTRS> tgtLocalRest(nt);
    for (size_t b = 0; b < nt; ++b) {
        const glm::mat4& m = target.bones[b].localRest;
        tgtLocalRest[b].t = positionOf(m);
        tgtLocalRest[b].r = rotationOf(m);
        tgtLocalRest[b].s = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
    }

    // Bones that carry motion (not just rotation): the pelvis, and the
    // root if it has a match (root motion). Their travel is scaled by
    // the ratio of pelvis heights, so a shorter character takes shorter
    // steps instead of sliding.
    float scale = 1.0f;
    std::vector<bool> moves(nt, false);
    for (size_t b = 0; b < nt; ++b) {
        const int s = match.sourceOf[b];
        if (s < 0) continue;
        const std::string name = canonicalBoneName(target.bones[b].name);
        if (name == "pelvis") {
            moves[b] = true;
            const float hs = positionOf(srcRest[s]).y, ht = positionOf(tgtRest[b]).y;
            if (std::abs(hs) > 1e-4f) scale = ht / hs;
        }
        if (name == "root") moves[b] = true;
    }

    // Turn the source's motion to face the way the target faces.
    const glm::quat turn = rotationBetween(modelForward(source), modelForward(target));

    for (const ModelAnimation& clip : source.animations) {
        ModelAnimation a;
        a.name = clip.name;
        a.duration = clip.duration;
        a.sampleRate = clip.sampleRate;
        a.frames.reserve(clip.frames.size());
        std::vector<glm::mat4> srcWorld(ns), tgtWorld(nt);
        for (const auto& frame : clip.frames) {
            for (size_t b = 0; b < ns && b < frame.size(); ++b)
                srcWorld[b] = source.bones[b].parent >= 0 ? srcWorld[source.bones[b].parent] * frame[b] : frame[b];
            std::vector<glm::mat4> locals(nt);
            for (size_t b = 0; b < nt; ++b) {
                const int p = target.bones[b].parent;
                const glm::mat4 parentWorld = p >= 0 ? tgtWorld[p] : glm::mat4(1.0f);
                const glm::quat parentRot = p >= 0 ? rotationOf(parentWorld) : glm::quat(1, 0, 0, 0);
                BoneTRS local = tgtLocalRest[b];
                const int s = match.sourceOf[b];
                if (s >= 0 && static_cast<size_t>(s) < frame.size()) {
                    // The source's change from rest, in model space, on
                    // top of the target's own rest.
                    const glm::quat delta = rotationOf(srcWorld[s]) * glm::inverse(srcRestRot[s]);
                    const glm::quat world = turn * delta * glm::inverse(turn) * tgtRestRot[b];
                    local.r = glm::normalize(glm::inverse(parentRot) * world);
                    if (moves[b]) {
                        const glm::vec3 pos = positionOf(tgtRest[b]) + turn * (positionOf(srcWorld[s]) - positionOf(srcRest[s])) * scale;
                        local.t = glm::vec3(glm::inverse(parentWorld) * glm::vec4(pos, 1.0f));
                    }
                }
                locals[b] = compose(local);
                tgtWorld[b] = parentWorld * locals[b];
            }
            a.frames.push_back(std::move(locals));
        }
        out.push_back(std::move(a));
    }
    return out;
}

size_t appendClipsByBoneName(ModelData& rig, const ModelData& source) {
    std::unordered_map<std::string, size_t> sourceBone;
    for (size_t b = 0; b < source.bones.size(); ++b) sourceBone.emplace(source.bones[b].name, b);
    std::vector<int> from(rig.bones.size(), -1);
    for (size_t b = 0; b < rig.bones.size(); ++b)
        if (auto it = sourceBone.find(rig.bones[b].name); it != sourceBone.end()) from[b] = static_cast<int>(it->second);
    for (const ModelAnimation& a : source.animations) {
        ModelAnimation out;
        out.name = a.name;
        out.duration = a.duration;
        out.sampleRate = a.sampleRate;
        out.frames.resize(a.frames.size());
        for (size_t f = 0; f < a.frames.size(); ++f) {
            out.frames[f].resize(rig.bones.size());
            for (size_t b = 0; b < rig.bones.size(); ++b)
                out.frames[f][b] = from[b] >= 0 && static_cast<size_t>(from[b]) < a.frames[f].size() ? a.frames[f][static_cast<size_t>(from[b])]
                                                                                                    : rig.bones[b].localRest;
        }
        rig.animations.push_back(std::move(out));
    }
    return source.animations.size();
}

} // namespace kke
