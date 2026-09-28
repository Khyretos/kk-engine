#include "kke/BodyShape.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace kke {

namespace {

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// The first bone whose canonical name is one of `names` (-1 = none).
int findBone(const ModelData& model, std::initializer_list<const char*> names) {
    for (const char* name : names)
        for (size_t i = 0; i < model.bones.size(); ++i)
            if (canonicalBoneName(model.bones[i].name) == name) return static_cast<int>(i);
    return -1;
}

// Closest points between segments p1-q1 and p2-q2 (Ericson, Real-Time
// Collision Detection 5.1.9).
void closestPoints(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    float s = 0.0f, t = 0.0f;
    constexpr float eps = 1e-10f;
    if (a <= eps && e <= eps) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= eps) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= eps) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2), denom = a * e - b * b;
            s = denom > eps ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

// The value below which `fraction` of `v` lies (v is reordered).
float percentile(std::vector<float>& v, float fraction) {
    if (v.empty()) return 0.0f;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(fraction * static_cast<float>(v.size() - 1) + 0.5f));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

float distanceToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float* along = nullptr) {
    const glm::vec3 ab = b - a;
    const float l2 = glm::dot(ab, ab);
    const float t = l2 > 1e-12f ? glm::dot(p - a, ab) / l2 : 0.0f;
    if (along) *along = t;
    return glm::length(p - (a + ab * glm::clamp(t, 0.0f, 1.0f)));
}

} // namespace

const char* bodyPartName(BodyPart part) {
    static constexpr std::array<const char*, static_cast<size_t>(BodyPart::Count)> names = {
        "pelvis", "belly", "chest", "upper chest", "neck", "head", "left thigh", "right thigh", "left calf", "right calf",
        "left upper arm", "right upper arm", "left forearm", "right forearm", "left hand", "right hand",
    };
    const size_t i = static_cast<size_t>(part);
    return i < names.size() ? names[i] : "?";
}

bool isArmPart(BodyPart part, bool left) {
    return left ? (part == BodyPart::UpperArmL || part == BodyPart::ForearmL || part == BodyPart::HandL)
                : (part == BodyPart::UpperArmR || part == BodyPart::ForearmR || part == BodyPart::HandR);
}

float capsuleOverlap(const Capsule& first, const Capsule& second, glm::vec3& push) {
    glm::vec3 c1, c2;
    closestPoints(first.a, first.b, second.a, second.b, c1, c2);
    glm::vec3 d = c1 - c2;
    const float dist = glm::length(d);
    if (dist > 1e-6f) {
        push = d / dist;
    } else {
        // Axes cross: out across both.
        glm::vec3 n = glm::cross(first.b - first.a, second.b - second.a);
        if (glm::length(n) < 1e-6f) n = glm::cross(first.b - first.a, glm::vec3(0, 1, 0));
        if (glm::length(n) < 1e-6f) n = glm::vec3(1, 0, 0);
        push = glm::normalize(n);
    }
    return first.radius + second.radius - dist;
}

// ---------------------------------------------------------------------
// Fitting

BodyShape BodyShape::fit(const ModelData& model) {
    BodyShape shape;
    if (model.bones.empty()) return shape;
    const size_t nb = model.bones.size();
    std::vector<glm::mat4> rest(nb), locals(nb);
    for (size_t b = 0; b < nb; ++b) {
        locals[b] = model.bones[b].localRest;
        rest[b] = model.bones[b].parent >= 0 ? rest[static_cast<size_t>(model.bones[b].parent)] * locals[b] : locals[b];
    }
    auto at = [&](int b) { return positionOf(rest[static_cast<size_t>(b)]); };

    const int pelvis = findBone(model, { "pelvis" });
    const int spine[3] = { findBone(model, { "spine_01", "spine" }), findBone(model, { "spine_02", "spine1" }), findBone(model, { "spine_03", "spine2" }) };
    const int neck = findBone(model, { "neck_01", "neck" }), head = findBone(model, { "head" });
    const int thigh[2] = { findBone(model, { "thigh_l", "leftupleg" }), findBone(model, { "thigh_r", "rightupleg" }) };
    const int calf[2] = { findBone(model, { "calf_l", "leftleg" }), findBone(model, { "calf_r", "rightleg" }) };
    const int foot[2] = { findBone(model, { "foot_l", "leftfoot" }), findBone(model, { "foot_r", "rightfoot" }) };
    const int upper[2] = { findBone(model, { "upperarm_l", "leftarm" }), findBone(model, { "upperarm_r", "rightarm" }) };
    const int lower[2] = { findBone(model, { "lowerarm_l", "leftforearm" }), findBone(model, { "lowerarm_r", "rightforearm" }) };
    const int hand[2] = { findBone(model, { "hand_l", "lefthand" }), findBone(model, { "hand_r", "righthand" }) };
    const int middle[2] = { findBone(model, { "middle_01_l", "lefthandmiddle1" }), findBone(model, { "middle_01_r", "righthandmiddle1" }) };
    const int clavicle[2] = { findBone(model, { "clavicle_l", "leftshoulder" }), findBone(model, { "clavicle_r", "rightshoulder" }) };
    if (pelvis < 0) return shape;

    // Which part each bone's vertices belong to: the nearest part bone up
    // the chain. Clavicles and feet stop the walk (they belong to none).
    std::vector<int> partOf(nb, -1);
    std::vector<int> stop(nb, 0);
    auto mark = [&](int bone, BodyPart p) {
        if (bone >= 0) partOf[static_cast<size_t>(bone)] = static_cast<int>(p);
    };
    for (int s = 0; s < 2; ++s) {
        if (clavicle[s] >= 0) stop[static_cast<size_t>(clavicle[s])] = 1;
        if (foot[s] >= 0) stop[static_cast<size_t>(foot[s])] = 1;
    }
    mark(pelvis, BodyPart::Pelvis);
    mark(spine[0], BodyPart::Belly);
    mark(spine[1], BodyPart::Chest);
    mark(spine[2], BodyPart::UpperChest);
    mark(neck, BodyPart::Neck);
    mark(head, BodyPart::Head);
    for (int s = 0; s < 2; ++s) {
        const bool l = s == 0;
        mark(thigh[s], l ? BodyPart::ThighL : BodyPart::ThighR);
        mark(calf[s], l ? BodyPart::CalfL : BodyPart::CalfR);
        mark(upper[s], l ? BodyPart::UpperArmL : BodyPart::UpperArmR);
        mark(lower[s], l ? BodyPart::ForearmL : BodyPart::ForearmR);
        mark(hand[s], l ? BodyPart::HandL : BodyPart::HandR);
    }
    std::vector<int> owner(nb, -1);
    for (size_t b = 0; b < nb; ++b) {
        int cur = static_cast<int>(b);
        while (cur >= 0 && partOf[static_cast<size_t>(cur)] < 0 && !stop[static_cast<size_t>(cur)]) cur = model.bones[static_cast<size_t>(cur)].parent;
        owner[b] = cur >= 0 && !stop[static_cast<size_t>(cur)] ? partOf[static_cast<size_t>(cur)] : -1;
    }

    // The mesh at rest, in model space, sorted by part.
    std::vector<std::vector<glm::vec3>> verts(static_cast<size_t>(BodyPart::Count));
    const std::vector<glm::mat4> skin = computeSkinMatrices(model, locals);
    for (const ModelMesh& mesh : model.meshes) {
        for (const ModelVertex& v : mesh.vertices) {
            int best = 0;
            for (int k = 1; k < 4; ++k)
                if (v.weights[k] > v.weights[best]) best = k;
            if (v.weights[best] <= 0.0f || v.joints[best] >= nb) continue;
            const int p = owner[v.joints[best]];
            if (p < 0) continue;
            glm::vec3 pos, nrm;
            skinVertex(v, skin, pos, nrm);
            verts[static_cast<size_t>(p)].push_back(pos);
        }
    }
    constexpr size_t kEnough = 24; // fewer vertices than this: use proportions

    // The body's frame and size, from the bones.
    const glm::vec3 up(0, 1, 0), fwd = modelForward(model);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, up)); // the character's right
    float lowest = at(pelvis).y;
    for (int s = 0; s < 2; ++s)
        if (foot[s] >= 0) lowest = std::min(lowest, at(foot[s]).y);
    const float top = head >= 0 ? at(head).y + 0.12f : at(pelvis).y * 1.9f;
    const float scale = std::max(0.2f, (top - lowest) / 1.75f);

    auto add = [&](BodyPart p, int bone, const glm::vec3& aModel, const glm::vec3& bModel, float radius) {
        if (bone < 0 || radius <= 0.0f) return;
        const glm::mat4 inv = glm::inverse(rest[static_cast<size_t>(bone)]);
        Part part;
        part.part = p;
        part.bone = bone;
        part.a = glm::vec3(inv * glm::vec4(aModel, 1.0f));
        part.b = glm::vec3(inv * glm::vec4(bModel, 1.0f));
        // Bones may carry scale (FBX centimetres): keep the radius in model units.
        part.radius = radius;
        shape.m_parts.push_back(part);
    };

    // Torso: capsules lying across the body, one per spine bone, as wide
    // and as deep as the mesh there.
    const int torsoBones[4] = { pelvis, spine[0], spine[1], spine[2] };
    const BodyPart torsoParts[4] = { BodyPart::Pelvis, BodyPart::Belly, BodyPart::Chest, BodyPart::UpperChest };
    const float hips = thigh[0] >= 0 && thigh[1] >= 0 ? glm::length(at(thigh[0]) - at(thigh[1])) * 0.5f : 0.1f * scale;
    const float shoulders = upper[0] >= 0 && upper[1] >= 0 ? glm::length(at(upper[0]) - at(upper[1])) * 0.5f : 0.18f * scale;
    for (int i = 0; i < 4; ++i) {
        const int bone = torsoBones[i];
        if (bone < 0) continue;
        std::vector<glm::vec3>& vs = verts[static_cast<size_t>(torsoParts[i])];
        const glm::vec3 origin = at(bone);
        if (vs.size() >= kEnough) {
            std::vector<float> xs, ys, zs;
            for (const glm::vec3& v : vs) {
                xs.push_back(glm::dot(v - origin, right));
                ys.push_back(v.y - origin.y);
                zs.push_back(glm::dot(v - origin, fwd));
            }
            const float x0 = percentile(xs, 0.04f), x1 = percentile(xs, 0.96f);
            const float z0 = percentile(zs, 0.04f), z1 = percentile(zs, 0.96f);
            const float ym = percentile(ys, 0.5f);
            const float r = std::max(0.02f, (z1 - z0) * 0.5f);
            const float half = std::max(0.0f, (x1 - x0) * 0.5f - r);
            const glm::vec3 c = origin + right * ((x0 + x1) * 0.5f) + up * ym + fwd * ((z0 + z1) * 0.5f);
            add(torsoParts[i], bone, c - right * half, c + right * half, r);
        } else {
            const float t = static_cast<float>(i) / 3.0f;
            const float r = 0.11f * scale;
            const float half = std::max(0.0f, glm::mix(hips + 0.04f * scale, shoulders * 0.75f, t) - r);
            add(torsoParts[i], bone, origin - right * half, origin + right * half, r);
        }
    }
    // Head: a ball round the mesh; neck: from the neck to the head.
    if (head >= 0) {
        std::vector<glm::vec3>& vs = verts[static_cast<size_t>(BodyPart::Head)];
        if (vs.size() >= kEnough) {
            std::vector<float> xs, ys, zs;
            for (const glm::vec3& v : vs) {
                xs.push_back(v.x);
                ys.push_back(v.y);
                zs.push_back(v.z);
            }
            const glm::vec3 lo(percentile(xs, 0.03f), percentile(ys, 0.03f), percentile(zs, 0.03f));
            const glm::vec3 hi(percentile(xs, 0.97f), percentile(ys, 0.97f), percentile(zs, 0.97f));
            const glm::vec3 c = (lo + hi) * 0.5f;
            const glm::vec3 h = (hi - lo) * 0.5f;
            add(BodyPart::Head, head, c, c, (h.x + h.y + h.z) / 3.0f);
        } else {
            const glm::vec3 c = at(head) + up * 0.09f * scale + fwd * 0.02f * scale;
            add(BodyPart::Head, head, c, c, 0.11f * scale);
        }
    }
    // Limbs: along the bone to the next joint, as thick as the mesh round it.
    auto limb = [&](BodyPart p, int bone, int next, float fallback, float along = 1.0f) {
        if (bone < 0 || next < 0) return;
        const glm::vec3 a = at(bone), b = a + (at(next) - a) * along;
        std::vector<glm::vec3>& vs = verts[static_cast<size_t>(p)];
        float r = fallback * scale;
        if (vs.size() >= kEnough) {
            std::vector<float> ds;
            for (const glm::vec3& v : vs) {
                float t = 0.0f;
                const float d = distanceToSegment(v, a, b, &t);
                if (t > 0.1f && t < 0.9f) ds.push_back(d);
            }
            if (ds.size() >= kEnough / 2) r = percentile(ds, 0.7f);
        }
        add(p, bone, a, b, r);
    };
    limb(BodyPart::Neck, neck, head, 0.05f);
    for (int s = 0; s < 2; ++s) {
        const bool l = s == 0;
        limb(l ? BodyPart::ThighL : BodyPart::ThighR, thigh[s], calf[s], 0.075f);
        limb(l ? BodyPart::CalfL : BodyPart::CalfR, calf[s], foot[s], 0.055f);
        limb(l ? BodyPart::UpperArmL : BodyPart::UpperArmR, upper[s], lower[s], 0.05f);
        limb(l ? BodyPart::ForearmL : BodyPart::ForearmR, lower[s], hand[s], 0.04f);
        // The hand: wrist to past the knuckles.
        limb(l ? BodyPart::HandL : BodyPart::HandR, hand[s], middle[s], 0.035f, 1.3f);
    }
    return shape;
}

const BodyShape::Part* BodyShape::part(BodyPart p) const {
    for (const Part& part : m_parts)
        if (part.part == p) return &part;
    return nullptr;
}

Capsule BodyShape::posed(const Part& part, const std::vector<glm::mat4>& world) const {
    const glm::mat4& m = world[static_cast<size_t>(part.bone)];
    return { glm::vec3(m * glm::vec4(part.a, 1.0f)), glm::vec3(m * glm::vec4(part.b, 1.0f)), part.radius };
}

std::vector<Capsule> BodyShape::posed(const std::vector<glm::mat4>& world) const {
    std::vector<Capsule> out;
    out.reserve(m_parts.size());
    for (const Part& p : m_parts) out.push_back(posed(p, world));
    return out;
}

// ---------------------------------------------------------------------
// Arms that know the body

namespace {

struct Obstacles {
    std::vector<Capsule> all;    // what the arm must stay out of
    std::vector<BodyPart> parts; // ... which part each is
    std::vector<Capsule> solid;  // the same without the other arm (for held things, which the other hand may hold)
};

Obstacles obstaclesFor(const BodyShape& body, const std::vector<glm::mat4>& world, bool left, bool otherArm) {
    Obstacles o;
    for (const BodyShape::Part& p : body.parts()) {
        if (isArmPart(p.part, left)) continue;
        const bool other = isArmPart(p.part, !left);
        // Only the other arm's upper arm: hands meet on a grip, forearms cross.
        if (other && (!otherArm || (p.part != BodyPart::UpperArmL && p.part != BodyPart::UpperArmR))) continue;
        const Capsule c = body.posed(p, world);
        o.all.push_back(c);
        o.parts.push_back(p.part);
        if (!other) o.solid.push_back(c);
    }
    return o;
}

struct ArmCapsules {
    float upper = 0.05f, fore = 0.04f, hand = 0.035f, handLength = 0.12f;
};

ArmCapsules armSize(const BodyShape& body, bool left) {
    ArmCapsules a;
    if (const BodyShape::Part* p = body.part(left ? BodyPart::UpperArmL : BodyPart::UpperArmR)) a.upper = p->radius;
    if (const BodyShape::Part* p = body.part(left ? BodyPart::ForearmL : BodyPart::ForearmR)) a.fore = p->radius;
    if (const BodyShape::Part* p = body.part(left ? BodyPart::HandL : BodyPart::HandR)) {
        a.hand = p->radius;
        a.handLength = glm::length(p->b - p->a);
    }
    return a;
}

// Depth of the arm S-E-H in the obstacles (sum over its pieces, each
// counted `margin` early), and the way out, weighted by depth.
float armDepth(const glm::vec3& S, const glm::vec3& E, const glm::vec3& H, const ArmCapsules& size, const Obstacles& obstacles, float margin,
               glm::vec3* out, float* deepest) {
    // The upper arm's outer part (its top is the shoulder, which presses
    // into the chest by nature: the upper arm skips the upper chest),
    // forearm, and the hand carried on along the forearm.
    const glm::vec3 fore = H - E;
    const float fl = glm::length(fore);
    const glm::vec3 handEnd = fl > 1e-5f ? H + fore / fl * size.handLength * 0.8f : H;
    const Capsule pieces[3] = { { S + (E - S) * 0.6f, E, size.upper }, { E, H, size.fore }, { H, handEnd, size.hand } };
    float sum = 0.0f, worst = 0.0f;
    glm::vec3 push(0.0f);
    for (int i = 0; i < 3; ++i) {
        for (size_t k = 0; k < obstacles.all.size(); ++k) {
            if (i == 0 && obstacles.parts[k] == BodyPart::UpperChest) continue;
            const Capsule& c = obstacles.all[k];
            glm::vec3 dir;
            const float o = capsuleOverlap(pieces[i], c, dir);
            if (o + margin <= 0.0f) continue;
            sum += o + margin;
            worst = std::max(worst, o);
            // The forearm and hand pull the hand out; the upper arm less (it
            // is mostly the elbow's job, but a straight arm can only move
            // its hand).
            push += dir * ((o + margin) * (i > 0 ? 1.0f : 0.5f));
        }
    }
    if (out) *out = push;
    if (deepest) *deepest = worst;
    return sum;
}

} // namespace

float armPenetration(const std::vector<glm::mat4>& world, const HumanArm& arm, const BodyShape& body, const BodyAvoid& avoid) {
    if (!arm.valid() || !body.valid()) return 0.0f;
    const Obstacles obs = obstaclesFor(body, world, arm.left, avoid.otherArm);
    const ArmCapsules size = armSize(body, arm.left);
    const glm::vec3 S = positionOf(world[static_cast<size_t>(arm.chain.upper)]);
    const glm::vec3 E = positionOf(world[static_cast<size_t>(arm.chain.lower)]);
    const glm::vec3 H = positionOf(world[static_cast<size_t>(arm.chain.end)]);
    float deepest = 0.0f;
    armDepth(S, E, H, size, obs, 0.0f, nullptr, &deepest);
    const glm::mat4& hand = world[static_cast<size_t>(arm.chain.end)];
    for (const Capsule& h : avoid.held) {
        const Capsule c{ glm::vec3(hand * glm::vec4(h.a, 1.0f)), glm::vec3(hand * glm::vec4(h.b, 1.0f)), h.radius };
        for (const Capsule& o : obs.solid) {
            glm::vec3 dir;
            deepest = std::max(deepest, capsuleOverlap(c, o, dir));
        }
    }
    return std::max(0.0f, deepest);
}

BodyAvoidResult solveHumanArm(const ModelData& model, Pose& pose, const HumanArm& arm, const ArmGoal& goalIn, const BodyShape& body,
                              const BodyAvoid& avoid, BodyAvoidState* state, const ArmLimits& limits) {
    BodyAvoidResult res;
    if (!arm.valid()) return res;
    if (!body.valid() || goalIn.weight <= 0.0f) {
        res.arm = solveHumanArm(model, pose, arm, goalIn, limits);
        return res;
    }
    const TwoBoneChain& ch = arm.chain;
    const BoneTRS keep[3] = { pose[static_cast<size_t>(ch.upper)], pose[static_cast<size_t>(ch.lower)], pose[static_cast<size_t>(ch.end)] };
    std::vector<glm::mat4> world = poseToModel(model, pose);
    Obstacles obs = obstaclesFor(body, world, arm.left, avoid.otherArm);
    const ArmCapsules size = armSize(body, arm.left);
    ArmGoal goal = goalIn;
    const float prev = state ? state->swivel : 0.0f;

    // 1. A hand aimed into the body goes to just outside it.
    for (int it = 0; it < 3; ++it) {
        glm::vec3 push(0.0f);
        bool inside = false;
        const Capsule h{ goal.hand, goal.hand, size.hand };
        for (const Capsule& c : obs.all) {
            glm::vec3 dir;
            const float o = capsuleOverlap(h, c, dir);
            if (o + avoid.margin > 0.0f) {
                push += dir * (o + avoid.margin);
                inside = true;
            }
        }
        if (!inside) break;
        goal.hand += push;
        res.moved = true;
    }

    // 2./3. The elbow round to where the arm is clear; the hand out if
    // nowhere is.
    const float range = limits.swivel * 2.0f;
    auto depthAt = [&](const glm::vec3& hand, float offset, glm::vec3* push, float* deepest) {
        ArmGoal g = goal;
        g.hand = hand;
        g.swivelOffset = goal.swivelOffset + offset;
        const ArmPoints pts = humanArmPoints(world, arm, g, limits);
        return armDepth(pts.shoulder, pts.elbow, pts.hand, size, obs, avoid.margin, push, deepest);
    };
    // The best elbow for a hand position: where it was and where it would
    // be first (usually clear), then all round. Returns the depth left.
    auto bestElbow = [&](const glm::vec3& hand, float& offset, glm::vec3& push, float& deepest) {
        float bestScore = 1e30f, bestDepth = 0.0f;
        auto consider = [&](float o) {
            glm::vec3 p;
            float d = 0.0f;
            const float depth = depthAt(hand, o, &p, &d);
            const float score = depth * 100.0f + std::abs(o - prev) * 0.002f + std::abs(o) * 0.001f;
            if (score < bestScore) {
                bestScore = score;
                bestDepth = depth;
                deepest = d;
                push = p;
                offset = o;
            }
        };
        consider(prev);
        if (bestDepth > 0.0f) consider(0.0f);
        if (bestDepth > 0.0f)
            for (float o = -range; o <= range + 1e-3f; o += 10.0f) consider(o);
        return bestDepth;
    };
    const ArmGoal aimed = goal; // after step 1
    float chosen = prev, deepest = 0.0f;
    glm::vec3 push(0.0f);
    // Frame to frame the answer barely changes: last frame's elbow first.
    float depth = depthAt(goal.hand, prev, &push, &deepest);
    if (depth > 0.0f) depth = bestElbow(goal.hand, chosen, push, deepest);
    // 2b. An arm reaching up past the head: the head leans away from it
    // (as a person's does) before the hand gives way. It leans back
    // upright over a few frames, not at once.
    const BodyShape::Part* neck = body.part(BodyPart::Neck);
    const BodyShape::Part* head = body.part(BodyPart::Head);
    auto leanHead = [&](float degrees, const glm::vec3& away) {
        const glm::vec3 base = positionOf(world[static_cast<size_t>(neck->bone)]);
        const glm::vec3 axisUp = glm::normalize(body.posed(*head, world).a - base);
        glm::vec3 axis = glm::cross(axisUp, away);
        if (glm::length(axis) < 1e-4f) return;
        glm::mat3 nr(world[static_cast<size_t>(neck->bone)]);
        for (int i = 0; i < 3; ++i) nr[i] = glm::normalize(nr[i]);
        const glm::quat neckW = glm::normalize(glm::quat_cast(nr));
        BoneTRS& nb = pose[static_cast<size_t>(neck->bone)];
        nb.r = glm::normalize(nb.r * (glm::inverse(neckW) * glm::angleAxis(glm::radians(degrees), glm::normalize(axis)) * neckW));
        world = poseToModel(model, pose);
        obs = obstaclesFor(body, world, arm.left, avoid.otherArm);
    };
    glm::vec3 headAway = state ? state->headAway : glm::vec3(0.0f);
    float tilt = 0.0f;
    while (depth > 0.0f && neck && head && tilt < avoid.headTilt - 1e-3f) {
        ArmGoal g = goal;
        g.swivelOffset = goal.swivelOffset + chosen;
        const ArmPoints pts = humanArmPoints(world, arm, g, limits);
        const Capsule headNow = body.posed(*head, world);
        const glm::vec3 axisUp = glm::normalize(headNow.a - positionOf(world[static_cast<size_t>(neck->bone)]));
        // Only when the arm is what the head is in the way of.
        glm::vec3 d1, d2;
        const float o1 = capsuleOverlap({ pts.elbow, pts.hand, size.fore }, headNow, d1);
        const float o2 = capsuleOverlap({ pts.shoulder + (pts.elbow - pts.shoulder) * 0.6f, pts.elbow, size.upper }, headNow, d2);
        if (std::max(o1, o2) + avoid.margin <= 0.0f) break;
        const glm::vec3 dir = o1 >= o2 ? d1 : d2;
        glm::vec3 away = -dir - axisUp * glm::dot(-dir, axisUp);
        if (glm::length(away) < 1e-4f) break;
        headAway = glm::normalize(away);
        const float step = std::min(5.0f, avoid.headTilt - tilt);
        leanHead(step, headAway);
        tilt += step;
        depth = bestElbow(goal.hand, chosen, push, deepest);
    }
    const float was = state ? state->headTilt : 0.0f;
    if (neck && head && tilt < was - 2.0f && glm::length(headAway) > 0.5f) {
        leanHead(was - 2.0f - tilt, headAway);
        tilt = was - 2.0f;
        depth = bestElbow(goal.hand, chosen, push, deepest);
    }
    res.headTilt = tilt;
    // Last frame's hand shift, when it still clears the arm: no more of it
    // than is needed now, so the hand comes back to its goal as soon as
    // the body lets it (rather than keeping a detour it once needed).
    const glm::vec3 lastShift = state ? state->shift : glm::vec3(0.0f);
    if (depth > 0.0f && glm::length(lastShift) > 1e-4f && glm::length(lastShift) <= avoid.maxShift + 1e-4f && depthAt(goal.hand + lastShift, chosen, nullptr, nullptr) <= 0.0f) {
        float lo = 0.0f, hi = 1.0f;
        for (int it = 0; it < 4; ++it) {
            const float mid = (lo + hi) * 0.5f;
            (depthAt(goal.hand + lastShift * mid, chosen, nullptr, nullptr) <= 0.0f ? hi : lo) = mid;
        }
        goal.hand += lastShift * hi;
        depth = 0.0f;
        res.moved = true;
    }
    // Nowhere clear: the hand moves out the way the body pushes it, a
    // little at a time (so it stops as soon as the arm is clear).
    // A push that makes it worse (it happens between the chest and the
    // other arm) is undone, and the hand never goes further than
    // `maxShift` from where it was aimed.
    if (depth > 0.0f) {
        glm::vec3 bestHand = goal.hand;
        float bestChosen = chosen, bestDepth = depth;
        for (int it = 0; it < avoid.iterations && depth > 0.0f; ++it) {
            const float l = glm::length(push);
            if (l < 1e-4f) break;
            glm::vec3 next = goal.hand + push / l * std::min(deepest + avoid.margin, 0.05f);
            const glm::vec3 off = next - aimed.hand;
            if (glm::length(off) > avoid.maxShift) next = aimed.hand + glm::normalize(off) * avoid.maxShift;
            goal.hand = next;
            depth = bestElbow(goal.hand, chosen, push, deepest);
            if (depth >= bestDepth) break;
            bestHand = goal.hand;
            bestChosen = chosen;
            bestDepth = depth;
        }
        goal.hand = bestHand;
        chosen = bestChosen;
        depth = bestDepth;
        if (glm::length(goal.hand - goalIn.hand) > 1e-5f) res.moved = true;
    }
    // Still caught (pushes cancel between the chest and the arm): the
    // nearest clear place round where the hand was aimed, on growing shells.
    if (depth > 0.0f) {
        const glm::vec3 from = humanArmPoints(world, arm, aimed, limits).hand;
        bool found = false;
        for (float r = 0.04f; r <= avoid.maxShift + 1e-3f && !found; r += 0.04f) {
            float bestScore = 1e30f;
            // The six sides and eight corners of a cube round it.
            static const glm::vec3 dirs[14] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
                                                glm::normalize(glm::vec3(1, 1, 1)), glm::normalize(glm::vec3(1, 1, -1)), glm::normalize(glm::vec3(1, -1, 1)),
                                                glm::normalize(glm::vec3(1, -1, -1)), glm::normalize(glm::vec3(-1, 1, 1)), glm::normalize(glm::vec3(-1, 1, -1)),
                                                glm::normalize(glm::vec3(-1, -1, 1)), glm::normalize(glm::vec3(-1, -1, -1)) };
            for (const glm::vec3& d : dirs) {
                        const glm::vec3 hand = from + d * r;
                        for (float o : { prev, 0.0f, -range * 0.5f, range * 0.5f }) {
                            if (depthAt(hand, o, nullptr, nullptr) > 0.0f) continue;
                            const float score = std::abs(o - prev) * 0.002f + std::abs(o) * 0.001f;
                            if (score < bestScore) {
                                bestScore = score;
                                goal.hand = hand;
                                chosen = o;
                                found = true;
                            }
                        }
                    }
        }
        if (found) {
            res.moved = true;
            depth = 0.0f;
        }
    }
    res.penetration = depth;
    goal.swivelOffset = goalIn.swivelOffset + chosen;
    res.swivel = chosen;

    // 4. The arm, then whatever it holds kept out of the body.
    res.arm = solveHumanArm(model, pose, arm, goal, limits);
    if (!avoid.held.empty()) {
        for (int it = 0; it < avoid.iterations; ++it) {
            const std::vector<glm::mat4> now = poseToModel(model, pose);
            const glm::mat4& hand = now[static_cast<size_t>(ch.end)];
            glm::vec3 push(0.0f);
            float deepest = -1.0f;
            for (const Capsule& h : avoid.held) {
                const Capsule c{ glm::vec3(hand * glm::vec4(h.a, 1.0f)), glm::vec3(hand * glm::vec4(h.b, 1.0f)), h.radius };
                for (const Capsule& o : obs.solid) {
                    glm::vec3 dir;
                    const float d = capsuleOverlap(c, o, dir);
                    if (d + avoid.margin > 0.0f) {
                        push += dir * (d + avoid.margin);
                        deepest = std::max(deepest, d);
                    }
                }
            }
            const float l = glm::length(push);
            if (deepest + avoid.margin <= 0.0f || l < 1e-6f) break;
            goal.hand += push / l * std::min(deepest + avoid.margin, 0.05f);
            res.moved = true;
            pose[static_cast<size_t>(ch.upper)] = keep[0];
            pose[static_cast<size_t>(ch.lower)] = keep[1];
            pose[static_cast<size_t>(ch.end)] = keep[2];
            res.arm = solveHumanArm(model, pose, arm, goal, limits);
        }
    }
    if (state) {
        state->swivel = chosen;
        state->headTilt = res.headTilt;
        state->headAway = headAway;
        state->shift = goal.hand - goalIn.hand;
    }
    res.penetration = armPenetration(poseToModel(model, pose), arm, body, avoid);
    return res;
}

} // namespace kke
