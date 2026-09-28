#include "kke/Equipment.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// Rotation of a transform that may carry (uniform) scale.
glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    return glm::normalize(glm::quat_cast(r));
}

int findBone(const ModelData& model, const std::string& name) {
    const std::string want = canonicalBoneName(name);
    for (size_t i = 0; i < model.bones.size(); ++i)
        if (canonicalBoneName(model.bones[i].name) == want) return static_cast<int>(i);
    return -1;
}

std::vector<glm::mat4> restWorld(const ModelData& model) {
    std::vector<glm::mat4> w(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b)
        w[b] = model.bones[b].parent >= 0 ? w[static_cast<size_t>(model.bones[b].parent)] * model.bones[b].localRest : model.bones[b].localRest;
    return w;
}

// A right-handed frame from +Y and +Z (Z kept, Y made perpendicular) at `origin`.
glm::mat4 frameYZ(const glm::vec3& origin, glm::vec3 y, glm::vec3 z) {
    z = glm::normalize(z);
    y = y - z * glm::dot(y, z);
    y = glm::length(y) > 1e-6f ? glm::normalize(y) : glm::normalize(glm::cross(z, glm::vec3(1, 0, 0)));
    const glm::vec3 x = glm::cross(y, z);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(origin, 1.0f));
}

float percentile(std::vector<float>& v, float fraction) {
    if (v.empty()) return 0.0f;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(fraction * static_cast<float>(v.size() - 1) + 0.5f));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

} // namespace

// ---------------------------------------------------------------------
// Hands

HandRig makeHandRig(const ModelData& model, bool left) {
    HandRig h;
    h.left = left;
    const std::string side = left ? "_l" : "_r";
    h.hand = findBone(model, "hand" + side);
    if (h.hand < 0) h.hand = findBone(model, left ? "lefthand" : "righthand");
    if (h.hand < 0) return h;
    static const char* names[4] = { "index", "middle", "ring", "pinky" };
    for (int f = 0; f < 4; ++f)
        for (int k = 0; k < 4; ++k) h.finger[f][k] = findBone(model, std::string(names[f]) + "_0" + std::to_string(k + 1) + side);
    for (int k = 0; k < 4; ++k) h.thumb[k] = findBone(model, "thumb_0" + std::to_string(k + 1) + side);

    const std::vector<glm::mat4> rest = restWorld(model);
    auto at = [&](int b) { return positionOf(rest[static_cast<size_t>(b)]); };
    const glm::mat4 toHand = glm::inverse(rest[static_cast<size_t>(h.hand)]);
    const glm::quat toHandR = glm::inverse(rotationOf(rest[static_cast<size_t>(h.hand)]));
    const glm::vec3 W = at(h.hand);
    // Tip lengths: a tip bone, else most of the middle segment's length.
    for (int f = 0; f < 4; ++f) {
        const int* s = h.finger[f];
        if (s[1] >= 0 && s[2] >= 0) h.fingerTip[f] = s[3] >= 0 ? glm::length(at(s[3]) - at(s[2])) : glm::length(at(s[2]) - at(s[1])) * 0.85f;
    }
    if (h.thumb[1] >= 0 && h.thumb[2] >= 0) h.thumbTip = h.thumb[3] >= 0 ? glm::length(at(h.thumb[3]) - at(h.thumb[2])) : glm::length(at(h.thumb[2]) - at(h.thumb[1])) * 0.9f;

    const int index = h.finger[0][0], middle = h.finger[1][0], ring = h.finger[2][0], pinky = h.finger[3][0];
    if (index < 0 || middle < 0 || pinky < 0) {
        // No fingers: the palm half-way along the hand's own axis.
        glm::vec3 fwd = glm::vec3(0, 1, 0);
        for (size_t b = 0; b < model.bones.size(); ++b)
            if (model.bones[b].parent == h.hand) fwd = glm::normalize(at(static_cast<int>(b)) - W);
        h.fingers = glm::normalize(toHandR * fwd);
        h.palm = glm::translate(glm::mat4(1.0f), h.fingers * (h.knuckles * 0.6f));
        return h;
    }
    const glm::vec3 kn = ring >= 0 ? (at(index) + at(middle) + at(ring) + at(pinky)) * 0.25f : (at(index) + at(middle) + at(pinky)) / 3.0f;
    const glm::vec3 F = glm::normalize(at(middle) - W);
    glm::vec3 T = at(index) - at(pinky);
    T = glm::normalize(T - F * glm::dot(T, F));
    // A right hand's palm faces cross(thumb side, fingers); a left hand is its mirror.
    const glm::vec3 N = glm::normalize(left ? glm::cross(F, T) : glm::cross(T, F));
    h.knuckles = glm::length(at(middle) - W);
    h.fingers = glm::normalize(toHandR * F);
    h.thumbSide = glm::normalize(toHandR * T);
    h.palmNormal = glm::normalize(toHandR * N);

    // The palm's skin: how far the hand's own vertices reach out of the
    // palm from its middle (the mesh, when there is one).
    const glm::vec3 C = W + (kn - W) * 0.62f;
    float skin = h.knuckles * 0.16f;
    float fingerR = h.knuckles * 0.09f;
    if (!model.meshes.empty()) {
        std::vector<glm::mat4> locals(model.bones.size());
        for (size_t b = 0; b < model.bones.size(); ++b) locals[b] = model.bones[b].localRest;
        const std::vector<glm::mat4> skinM = computeSkinMatrices(model, locals);
        std::vector<float> out, radii;
        const int mid2 = h.finger[1][1], mid3 = h.finger[1][2];
        for (const ModelMesh& mesh : model.meshes)
            for (const ModelVertex& v : mesh.vertices) {
                int best = 0;
                for (int k = 1; k < 4; ++k)
                    if (v.weights[k] > v.weights[best]) best = k;
                if (v.weights[best] <= 0.0f) continue;
                const int bone = static_cast<int>(v.joints[best]);
                if (bone != h.hand && bone != mid2) continue;
                glm::vec3 p, n;
                skinVertex(v, skinM, p, n);
                if (bone == h.hand) {
                    // Only the palm's middle: near C across the hand.
                    const glm::vec3 d = p - C;
                    if (std::abs(glm::dot(d, F)) < h.knuckles * 0.3f && std::abs(glm::dot(d, T)) < h.knuckles * 0.35f) out.push_back(glm::dot(d, N));
                } else if (mid3 >= 0) {
                    const glm::vec3 a = at(mid2), b = at(mid3);
                    const glm::vec3 ab = b - a;
                    const float t = glm::dot(p - a, ab) / std::max(1e-8f, glm::dot(ab, ab));
                    if (t > 0.2f && t < 0.8f) radii.push_back(glm::length(p - (a + ab * t)));
                }
            }
        if (out.size() >= 6) skin = std::max(0.004f, percentile(out, 0.9f));
        if (radii.size() >= 6) fingerR = std::max(0.004f, percentile(radii, 0.6f));
    }
    h.fingerRadius = fingerR;
    // The handle lies across the palm from the heel of the hand toward the
    // index knuckle: along the thumb side, tipped toward the fingers.
    const glm::mat4 palmModel = frameYZ(C + N * skin, T + F * 0.45f, N);
    h.palm = toHand * palmModel;
    return h;
}

namespace {

// Human finger joint ranges (degrees): knuckle, middle, tip; the thumb's.
constexpr float kFingerMax[3] = { 90.0f, 105.0f, 75.0f };
constexpr float kThumbMax[3] = { 40.0f, 50.0f, 65.0f };

bool touches(const Capsule& seg, const GripSurface& s) {
    for (const Capsule& c : s.capsules) {
        glm::vec3 dir;
        if (capsuleOverlap(seg, c, dir) > 0.0f) return true;
    }
    for (const GripSurface::Plane& p : s.planes)
        if (glm::dot(seg.a - p.point, p.normal) < seg.radius || glm::dot(seg.b - p.point, p.normal) < seg.radius) return true;
    return false;
}

// One finger (or the thumb): joints bones[0..2], tip bones[3] or `tipLength` on.
void curlToContact(const std::vector<glm::mat4>& world, Pose& pose, const ModelData& model, const int* bones, float tipLength, const glm::vec3& palm,
                   const float* maxDeg, float close, float radius, const GripSurface& surface) {
    if (bones[0] < 0 || bones[1] < 0 || bones[2] < 0 || close <= 0.0f) return;
    glm::vec3 p[4];
    glm::quat rot[3];
    for (int j = 0; j < 3; ++j) {
        p[j] = positionOf(world[static_cast<size_t>(bones[j])]);
        rot[j] = rotationOf(world[static_cast<size_t>(bones[j])]);
    }
    p[3] = bones[3] >= 0 ? positionOf(world[static_cast<size_t>(bones[3])]) : p[2] + glm::normalize(p[2] - p[1]) * tipLength;
    const int parent = model.bones[static_cast<size_t>(bones[0])].parent;
    glm::quat parentRot = parent >= 0 ? rotationOf(world[static_cast<size_t>(parent)]) : glm::quat(1, 0, 0, 0);
    // Curling turns the finger toward the palm: about finger x palm.
    glm::vec3 axis = glm::cross(p[1] - p[0], palm);
    if (glm::length(axis) < 1e-6f) return;
    axis = glm::normalize(axis);
    for (int j = 0; j < 3; ++j) {
        auto hit = [&](float angle) {
            const glm::quat q = glm::angleAxis(angle, axis);
            glm::vec3 prev = p[j];
            for (int k = j + 1; k <= 3; ++k) {
                const glm::vec3 next = p[j] + q * (p[k] - p[j]);
                if (touches({ prev, next, radius }, surface)) return true;
                prev = next;
            }
            return false;
        };
        const float maxA = glm::radians(maxDeg[j]) * close;
        float angle = 0.0f;
        if (!hit(0.0f)) {
            constexpr int kSteps = 8;
            float lo = 0.0f, hi = -1.0f;
            for (int i = 1; i <= kSteps; ++i) {
                const float a = maxA * static_cast<float>(i) / kSteps;
                if (hit(a)) {
                    hi = a;
                    break;
                }
                lo = a;
            }
            if (hi < 0.0f) {
                angle = maxA;
            } else {
                for (int i = 0; i < 6; ++i) {
                    const float m = (lo + hi) * 0.5f;
                    (hit(m) ? hi : lo) = m;
                }
                angle = lo;
            }
        }
        if (angle > 0.0f) {
            const glm::quat q = glm::angleAxis(angle, axis);
            for (int k = j + 1; k <= 3; ++k) p[k] = p[j] + q * (p[k] - p[j]);
            BoneTRS& b = pose[static_cast<size_t>(bones[j])];
            b.r = glm::normalize(glm::angleAxis(angle, glm::inverse(parentRot) * axis) * b.r);
            for (int k = j; k < 3; ++k) rot[k] = q * rot[k];
        }
        parentRot = rot[j];
    }
}

} // namespace

void wrapFingers(const ModelData& model, Pose& pose, const HandRig& hand, const GripSurface& surface, float close, float thumb) {
    if (!hand.valid() || close <= 0.0f) return;
    const std::vector<glm::mat4> world = poseToModel(model, pose);
    const glm::vec3 palm = glm::normalize(rotationOf(world[static_cast<size_t>(hand.hand)]) * hand.palmNormal);
    for (int f = 0; f < 4; ++f) curlToContact(world, pose, model, hand.finger[f], hand.fingerTip[f], palm, kFingerMax, close, hand.fingerRadius, surface);
    if (thumb > 0.0f) curlToContact(world, pose, model, hand.thumb, hand.thumbTip, palm, kThumbMax, close * thumb, hand.fingerRadius * 1.1f, surface);
}

// ---------------------------------------------------------------------
// Equipment

const char* equipSlotName(EquipSlot slot) {
    switch (slot) {
    case EquipSlot::LeftHand: return "left hand";
    case EquipSlot::RightHand: return "right hand";
    case EquipSlot::Back: return "back";
    case EquipSlot::HipLeft: return "left hip";
    case EquipSlot::HipRight: return "right hip";
    case EquipSlot::Head: return "head";
    case EquipSlot::Count: break;
    }
    return "?";
}

Equippable::Equippable() { worn.fill(glm::mat4(1.0f)); }

int Equippable::grip(const std::string& n) const {
    for (size_t i = 0; i < grips.size(); ++i)
        if (grips[i].name == n) return static_cast<int>(i);
    return -1;
}

Equipment::Equipment(const ModelData& model) {
    m_hands[0] = makeHandRig(model, true);
    m_hands[1] = makeHandRig(model, false);
    m_sockets[static_cast<size_t>(EquipSlot::LeftHand)] = m_hands[0].palmSocket();
    m_sockets[static_cast<size_t>(EquipSlot::RightHand)] = m_hands[1].palmSocket();
    // Worn places on the body's surface: behind the upper chest, out from
    // each hip, on top of the head (from the fitted body shape).
    const BodyShape body = BodyShape::fit(model);
    const std::vector<glm::mat4> rest = restWorld(model);
    const glm::vec3 up(0, 1, 0), fwd = modelForward(model), right = glm::normalize(glm::cross(fwd, up));
    auto place = [&](EquipSlot slot, BodyPart part, const glm::vec3& out, bool atEnd) {
        const BodyShape::Part* p = body.part(part);
        if (!p) return;
        const Capsule c = body.posed(*p, rest);
        // The middle of the capsule, or (a hip) its end on the `out` side.
        glm::vec3 origin = (c.a + c.b) * 0.5f;
        if (atEnd) origin = glm::dot(c.b - c.a, out) > 0.0f ? c.b : c.a;
        m_sockets[static_cast<size_t>(slot)] = { p->bone, glm::inverse(rest[static_cast<size_t>(p->bone)]) * frameYZ(origin + out * c.radius, up, out) };
    };
    place(EquipSlot::Back, BodyPart::UpperChest, -fwd, false);
    place(EquipSlot::HipLeft, BodyPart::Pelvis, -right, true);
    place(EquipSlot::HipRight, BodyPart::Pelvis, right, true);
    if (const BodyShape::Part* p = body.part(BodyPart::Head)) {
        const Capsule c = body.posed(*p, rest);
        m_sockets[static_cast<size_t>(EquipSlot::Head)] = { p->bone, glm::inverse(rest[static_cast<size_t>(p->bone)]) * frameYZ(c.a + up * c.radius, fwd, up) };
    }
}

bool Equipment::equip(EquipSlot slot, const Equippable& item, int grip) {
    const size_t s = static_cast<size_t>(slot);
    if (s >= kEquipSlots || !(item.slots & slotBit(slot))) return false;
    const bool hand = slot == EquipSlot::LeftHand || slot == EquipSlot::RightHand;
    if (hand && (grip < 0 || grip >= static_cast<int>(item.grips.size()))) return false;
    m_items[s] = { &item, hand ? grip : 0 };
    return true;
}

void Equipment::unequip(EquipSlot slot) { m_items[static_cast<size_t>(slot)] = Held{}; }

glm::mat4 Equipment::inHand(int side, const Equippable& item, int grip) const {
    if (grip < 0 || grip >= static_cast<int>(item.grips.size())) return glm::mat4(1.0f);
    const ItemGrip& g = item.grips[static_cast<size_t>(grip)];
    return m_hands[side & 1].palm * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, g.radius)) * glm::inverse(g.frame);
}

glm::mat4 Equipment::itemTransform(EquipSlot slot, const std::vector<glm::mat4>& world) const {
    const Held& h = m_items[static_cast<size_t>(slot)];
    const Socket& sock = m_sockets[static_cast<size_t>(slot)];
    if (slot == EquipSlot::LeftHand || slot == EquipSlot::RightHand) {
        int side = slot == EquipSlot::LeftHand ? 0 : 1, grip = h.grip;
        if (h.item && grip != 0)
            for (int other = 0; other < 2; ++other) {
                const Held& o = m_items[static_cast<size_t>(other == 0 ? EquipSlot::LeftHand : EquipSlot::RightHand)];
                if (o.item == h.item && o.grip == 0) {
                    side = other;
                    grip = 0;
                }
            }
        const HandRig& hr = m_hands[static_cast<size_t>(side)];
        if (!hr.valid()) return glm::mat4(1.0f);
        const glm::mat4& bone = world[static_cast<size_t>(hr.hand)];
        return h.item ? bone * inHand(side, *h.item, grip) : bone * hr.palm;
    }
    if (!sock.valid()) return glm::mat4(1.0f);
    return h.item ? sock.world(world) * h.item->worn[static_cast<size_t>(slot)] : sock.world(world);
}

glm::mat4 Equipment::handFor(int side, const Equippable& item, int grip, const glm::mat4& itemModel) const {
    return itemModel * glm::inverse(inHand(side, item, grip));
}

ArmGoal Equipment::armGoal(int side, const Equippable& item, int grip, const glm::mat4& itemModel) const {
    const glm::mat4 h = handFor(side, item, grip, itemModel);
    ArmGoal g;
    g.hand = positionOf(h);
    g.handRotation = rotationOf(h);
    return g;
}

std::vector<Capsule> Equipment::heldShape(int side) const {
    std::vector<Capsule> out;
    const Held& h = m_items[static_cast<size_t>(side == 0 ? EquipSlot::LeftHand : EquipSlot::RightHand)];
    if (!h.item) return out;
    const glm::mat4 m = inHand(side, *h.item, h.grip);
    for (const Capsule& c : h.item->shape) out.push_back({ glm::vec3(m * glm::vec4(c.a, 1.0f)), glm::vec3(m * glm::vec4(c.b, 1.0f)), c.radius });
    return out;
}

void Equipment::closeHand(const ModelData& model, Pose& pose, int side, float close) const {
    const EquipSlot slot = side == 0 ? EquipSlot::LeftHand : EquipSlot::RightHand;
    const Held& h = m_items[static_cast<size_t>(slot)];
    const HandRig& hr = m_hands[static_cast<size_t>(side & 1)];
    if (!h.item || !hr.valid()) return;
    const std::vector<glm::mat4> world = poseToModel(model, pose);
    const glm::mat4 item = itemTransform(slot, world);
    const ItemGrip& g = h.item->grips[static_cast<size_t>(h.grip)];
    const glm::mat4 f = item * g.frame;
    GripSurface s;
    s.capsules.push_back({ glm::vec3(f * glm::vec4(0.0f, -g.halfLength, 0.0f, 1.0f)), glm::vec3(f * glm::vec4(0.0f, g.halfLength, 0.0f, 1.0f)), g.radius });
    wrapFingers(model, pose, hr, s, close);
}

} // namespace kke
