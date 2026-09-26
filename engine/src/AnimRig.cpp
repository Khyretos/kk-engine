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

} // namespace kke
