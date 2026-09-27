#include "kke/ProceduralAnim.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>

namespace kke {

namespace {

constexpr float kGravity = 9.81f;
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-8f) r[i] /= len;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

glm::vec3 horizontal(glm::vec3 v) {
    v.y = 0.0f;
    return v;
}

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

// Turns bone `b` by the model-space rotation `delta` about its own pivot.
void rotateInModel(Pose& pose, int b, const glm::quat& boneWorld, const glm::quat& delta) {
    pose[b].r = glm::normalize(pose[b].r * (glm::inverse(boneWorld) * delta * boneWorld));
}

// Sets bone `b` so its model-space transform is `model` (scale kept).
void setModelTransform(const ModelData& data, Pose& pose, const std::vector<glm::mat4>& world, int b, const glm::mat4& model) {
    const int parent = data.bones[b].parent;
    const glm::mat4 local = parent >= 0 ? glm::inverse(world[parent]) * model : model;
    pose[b].t = positionOf(local);
    pose[b].r = rotationOf(local);
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
        w[b] = model.bones[b].parent >= 0 ? w[model.bones[b].parent] * model.bones[b].localRest : model.bones[b].localRest;
    return w;
}

float smoothFactor(float rate, float dt) { return dt > 0.0f ? 1.0f - std::exp(-rate * dt) : 1.0f; }

float fract(float x) { return x - std::floor(x); }

// Least-squares slope of y against x (0 if the xs don't spread).
float slope(const std::vector<float>& x, const std::vector<float>& y) {
    const size_t n = std::min(x.size(), y.size());
    if (n < 2) return 0.0f;
    float mx = 0.0f, my = 0.0f;
    for (size_t i = 0; i < n; ++i) mx += x[i], my += y[i];
    mx /= float(n);
    my /= float(n);
    float sxx = 0.0f, sxy = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    return sxx > 1e-6f ? sxy / sxx : 0.0f;
}

} // namespace

// =====================================================================
// Layering

BoneMask boneMask(const ModelData& model, const std::vector<std::string>& roots, float weight, bool children) {
    std::vector<std::string> want;
    for (const std::string& r : roots) want.push_back(canonicalBoneName(r));
    BoneMask mask(model.bones.size(), 0.0f);
    std::vector<bool> in(model.bones.size(), false);
    for (size_t b = 0; b < model.bones.size(); ++b) {
        const std::string name = canonicalBoneName(model.bones[b].name);
        const int p = model.bones[b].parent;
        in[b] = std::find(want.begin(), want.end(), name) != want.end() || (children && p >= 0 && in[p]);
        if (in[b]) mask[b] = weight;
    }
    return mask;
}

void blendPosesMasked(const Pose& base, const Pose& layer, const BoneMask& mask, float weight, Pose& out) {
    if (&out != &base) out = base;
    const size_t n = std::min(base.size(), layer.size());
    for (size_t b = 0; b < n; ++b) {
        const float w = glm::clamp(weight * (mask.empty() ? 1.0f : (b < mask.size() ? mask[b] : 0.0f)), 0.0f, 1.0f);
        if (w <= 0.0f) continue;
        out[b].t = glm::mix(base[b].t, layer[b].t, w);
        out[b].r = glm::normalize(glm::slerp(base[b].r, layer[b].r, w));
        out[b].s = glm::mix(base[b].s, layer[b].s, w);
    }
}

void addPose(const Pose& base, const Pose& additive, const Pose& reference, const BoneMask& mask, float weight, Pose& out) {
    if (&out != &base) out = base;
    const size_t n = std::min({ base.size(), additive.size(), reference.size() });
    const glm::quat identity(1, 0, 0, 0);
    for (size_t b = 0; b < n; ++b) {
        const float w = weight * (mask.empty() ? 1.0f : (b < mask.size() ? mask[b] : 0.0f));
        if (w == 0.0f) continue;
        const glm::quat delta = glm::normalize(glm::inverse(reference[b].r) * additive[b].r);
        out[b].r = glm::normalize(out[b].r * glm::slerp(identity, delta, w));
        out[b].t += (additive[b].t - reference[b].t) * w;
        const glm::vec3 ratio = additive[b].s / glm::max(glm::abs(reference[b].s), glm::vec3(1e-6f));
        out[b].s *= glm::mix(glm::vec3(1.0f), ratio, w);
    }
}

// =====================================================================
// LookAt

LookAt::LookAt(std::vector<Link> chain, const glm::vec3& forward, const Settings& settings)
    : m_chain(std::move(chain)), m_forward(unitOr(horizontal(forward), glm::vec3(0, 0, 1))), m_s(settings) {
    float total = 0.0f;
    for (const Link& l : m_chain) total += std::max(0.0f, l.share);
    for (Link& l : m_chain) l.share = total > 0.0f ? std::max(0.0f, l.share) / total : 1.0f / float(m_chain.size());
}

LookAt LookAt::humanoid(const ModelData& model, const Settings& settings) {
    std::vector<Link> chain;
    const std::pair<const char*, float> parts[] = { { "spine_02", 0.15f }, { "spine_03", 0.2f }, { "neck_01", 0.3f }, { "head", 0.35f } };
    for (auto [name, share] : parts) {
        int b = findBone(model, name);
        if (b < 0 && std::string(name) == "neck_01") b = findBone(model, "neck");
        if (b >= 0) chain.push_back({ b, share });
    }
    if (chain.empty() || findBone(model, "head") < 0) return LookAt();
    return LookAt(std::move(chain), modelForward(model), settings);
}

LookAt LookAt::quadruped(const ModelData& model, const QuadrupedBones& bones, const Settings& settings) {
    const int neck = findBone(model, bones.neck), head = findBone(model, bones.head);
    if (head < 0) return LookAt();
    std::vector<Link> chain;
    if (neck >= 0) chain.push_back({ neck, 0.45f });
    chain.push_back({ head, 0.55f });
    // Which way the animal faces: from its hind legs to its front legs.
    const std::vector<glm::mat4> rest = restWorld(model);
    glm::vec3 front(0.0f), back(0.0f);
    int nf = 0, nb = 0;
    for (int i = 0; i < 4; ++i) {
        const int b = findBone(model, bones.upperLeg[i]);
        if (b < 0) continue;
        (i < 2 ? front : back) += positionOf(rest[b]);
        (i < 2 ? nf : nb) += 1;
    }
    glm::vec3 forward(0, 0, 1);
    if (nf && nb) forward = unitOr(horizontal(front / float(nf) - back / float(nb)), forward);
    return LookAt(std::move(chain), forward, settings);
}

void LookAt::apply(const ModelData& model, Pose& pose, const glm::vec3* target, float dt, float weight) {
    if (!valid()) return;
    std::vector<glm::mat4> world = poseToModel(model, pose);
    const glm::vec3 head = positionOf(world[m_chain.back().bone]);
    float wantYaw = 0.0f, wantPitch = 0.0f;
    if (target) {
        const glm::vec3 d = *target - head;
        const glm::vec3 dh = horizontal(d);
        const float lh = glm::length(dh);
        if (lh > 1e-4f || std::abs(d.y) > 1e-4f) {
            wantYaw = glm::degrees(std::atan2(glm::dot(glm::cross(m_forward, dh), kUp), glm::dot(m_forward, dh)));
            wantPitch = glm::degrees(std::atan2(d.y, lh));
            if (std::abs(wantYaw) > m_s.giveUpYaw) wantYaw = wantPitch = 0.0f;
        }
    }
    wantYaw = glm::clamp(wantYaw, -m_s.maxYaw, m_s.maxYaw);
    wantPitch = glm::clamp(wantPitch, -m_s.maxPitch, m_s.maxPitch);
    const float k = smoothFactor(m_s.speed, dt);
    m_yaw += (wantYaw - m_yaw) * k;
    m_pitch += (wantPitch - m_pitch) * k;

    const float w = glm::clamp(weight, 0.0f, 1.0f);
    const float yaw = glm::radians(m_yaw) * w, pitch = glm::radians(m_pitch) * w;
    if (std::abs(yaw) < 1e-6f && std::abs(pitch) < 1e-6f) return;
    const glm::vec3 faced = glm::angleAxis(yaw, kUp) * m_forward;
    const glm::vec3 pitchAxis = unitOr(glm::cross(faced, kUp), glm::vec3(-1, 0, 0));
    for (const Link& l : m_chain) {
        if (l.bone < 0 || l.bone >= static_cast<int>(pose.size())) continue;
        const glm::quat q = glm::angleAxis(pitch * l.share, pitchAxis) * glm::angleAxis(yaw * l.share, kUp);
        rotateInModel(pose, l.bone, rotationOf(world[l.bone]), q);
        world = poseToModel(model, pose);
    }
}

// =====================================================================
// FABRIK

IkChain findIkChain(const ModelData& model, const std::string& root, const std::string& end) {
    const int r = findBone(model, root);
    int b = findBone(model, end);
    IkChain chain;
    if (r < 0 || b < 0) return chain;
    while (b >= 0) {
        chain.bones.push_back(b);
        if (b == r) break;
        b = model.bones[b].parent;
    }
    if (chain.bones.empty() || chain.bones.back() != r) return IkChain{};
    std::reverse(chain.bones.begin(), chain.bones.end());
    return chain;
}

namespace {

// Turns interior joint i about the line through its neighbours so it
// bends toward `pole` (lengths to both neighbours unchanged).
void bendToward(std::vector<glm::vec3>& p, size_t i, const glm::vec3& pole) {
    const glm::vec3 a = p[i - 1], c = p[i + 1];
    const glm::vec3 axis = c - a;
    const float len = glm::length(axis);
    if (len < 1e-6f) return;
    const glm::vec3 n = axis / len;
    const glm::vec3 j = p[i] - a, q = pole - a;
    const glm::vec3 jp = j - n * glm::dot(j, n), qp = q - n * glm::dot(q, n);
    if (glm::length(jp) < 1e-6f || glm::length(qp) < 1e-6f) return;
    const float angle = std::atan2(glm::dot(glm::cross(jp, qp), n), glm::dot(jp, qp));
    p[i] = a + glm::angleAxis(angle, n) * j;
}

} // namespace

void fabrikPoints(std::vector<glm::vec3>& p, const glm::vec3& target, const glm::vec3* pole, const FabrikSettings& s) {
    const size_t n = p.size();
    if (n < 2) return;
    std::vector<float> len(n - 1);
    float total = 0.0f;
    for (size_t i = 0; i + 1 < n; ++i) total += (len[i] = glm::length(p[i + 1] - p[i]));
    const glm::vec3 root = p[0];
    if (glm::length(target - root) >= total) {
        const glm::vec3 dir = unitOr(target - root, unitOr(p[n - 1] - root, kUp));
        for (size_t i = 1; i < n; ++i) p[i] = p[i - 1] + dir * len[i - 1];
        return;
    }
    if (pole)
        for (size_t i = 1; i + 1 < n; ++i) bendToward(p, i, *pole);
    for (int it = 0; it < std::max(1, s.iterations); ++it) {
        p[n - 1] = target;
        for (size_t i = n - 1; i-- > 0;) p[i] = p[i + 1] + unitOr(p[i] - p[i + 1], kUp) * len[i];
        p[0] = root;
        for (size_t i = 1; i < n; ++i) p[i] = p[i - 1] + unitOr(p[i] - p[i - 1], kUp) * len[i - 1];
        if (pole)
            for (size_t i = 1; i + 1 < n; ++i) bendToward(p, i, *pole);
        if (glm::length(p[n - 1] - target) <= s.tolerance) break;
    }
}

void solveFabrik(const ModelData& model, Pose& pose, const IkChain& chain, const glm::vec3& target, const glm::vec3* pole, float weight,
                 const FabrikSettings& settings) {
    if (!chain.valid() || weight <= 0.0f) return;
    for (int b : chain.bones)
        if (b < 0 || b >= static_cast<int>(pose.size())) return;
    std::vector<glm::mat4> world = poseToModel(model, pose);
    std::vector<glm::vec3> pts;
    for (int b : chain.bones) pts.push_back(positionOf(world[b]));
    const glm::vec3 t = glm::mix(pts.back(), target, glm::clamp(weight, 0.0f, 1.0f));
    fabrikPoints(pts, t, pole, settings);
    for (size_t i = 0; i + 1 < chain.bones.size(); ++i) {
        const int b = chain.bones[i];
        const glm::vec3 at = positionOf(world[b]);
        const glm::vec3 now = positionOf(world[chain.bones[i + 1]]) - at;
        rotateInModel(pose, b, rotationOf(world[b]), rotationBetween(now, pts[i + 1] - at));
        world = poseToModel(model, pose);
    }
}

glm::vec3 kneePosition(const glm::vec3& hip, const glm::vec3& foot, float upper, float lower, const glm::vec3& bend) {
    const glm::vec3 d = foot - hip;
    const glm::vec3 dir = unitOr(d, glm::vec3(0, -1, 0));
    const float eps = 1e-4f;
    const float l = glm::clamp(glm::length(d), std::abs(upper - lower) + eps, upper + lower - eps);
    const float x = (upper * upper - lower * lower + l * l) / (2.0f * l);
    const float h = std::sqrt(std::max(0.0f, upper * upper - x * x));
    glm::vec3 side = bend - dir * glm::dot(bend, dir);
    if (glm::length(side) < 1e-5f) side = glm::cross(dir, std::abs(dir.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1));
    return hip + dir * x + glm::normalize(side) * h;
}

// =====================================================================
// Gaits

const char* gaitName(Gait gait) {
    switch (gait) {
    case Gait::Walk: return "walk";
    case Gait::Trot: return "trot";
    case Gait::Pace: return "pace";
    case Gait::Canter: return "canter";
    case Gait::Gallop: return "gallop";
    case Gait::Bound: return "bound";
    case Gait::Pronk: return "pronk";
    case Gait::Tripod: return "tripod";
    case Gait::Wave: return "wave";
    case Gait::Auto: return "auto";
    }
    return "auto";
}

Gait gaitFromName(const std::string& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (Gait g : { Gait::Walk, Gait::Trot, Gait::Pace, Gait::Canter, Gait::Gallop, Gait::Bound, Gait::Pronk, Gait::Tripod, Gait::Wave })
        if (n == gaitName(g)) return g;
    if (n == "run") return Gait::Trot;
    return Gait::Auto;
}

GaitPattern gaitPattern(Gait gait, int n) {
    GaitPattern p;
    p.gait = gait == Gait::Auto ? Gait::Walk : gait;
    if (n <= 0) return p;
    p.phase.assign(n, 0.0f);
    if (n == 1) {
        p.duty = 0.6f;
        return p;
    }
    if (n == 2) {
        // Bipeds walk or run; a pronk is a hop.
        p.phase = { 0.0f, 0.5f };
        if (p.gait == Gait::Pronk) {
            p.phase = { 0.0f, 0.0f };
            p.duty = 0.4f;
        } else if (p.gait == Gait::Walk || p.gait == Gait::Wave) {
            p.gait = Gait::Walk;
            p.duty = 0.62f;
        } else {
            p.gait = Gait::Trot; // running
            p.duty = 0.38f;
        }
        return p;
    }
    if (n == 4) {
        enum { FL, FR, BL, BR };
        switch (p.gait) {
        case Gait::Trot: case Gait::Tripod:
            p.gait = Gait::Trot;
            p.phase[FL] = p.phase[BR] = 0.0f;
            p.phase[FR] = p.phase[BL] = 0.5f;
            p.duty = 0.5f;
            break;
        case Gait::Pace:
            p.phase[FL] = p.phase[BL] = 0.0f;
            p.phase[FR] = p.phase[BR] = 0.5f;
            p.duty = 0.5f;
            break;
        case Gait::Canter: // right lead: left hind, then the right hind with the left fore, then the right fore
            p.phase[BL] = 0.0f;
            p.phase[BR] = p.phase[FL] = 0.3f;
            p.phase[FR] = 0.6f;
            p.duty = 0.4f;
            break;
        case Gait::Gallop: // rotary (dogs): left hind, right hind, right fore, left fore
            p.phase[BL] = 0.0f;
            p.phase[BR] = 0.1f;
            p.phase[FR] = 0.4f;
            p.phase[FL] = 0.5f;
            p.duty = 0.32f;
            break;
        case Gait::Bound:
            p.phase[BL] = p.phase[BR] = 0.0f;
            p.phase[FL] = p.phase[FR] = 0.5f;
            p.duty = 0.4f;
            break;
        case Gait::Pronk:
            p.duty = 0.4f;
            break;
        default: // walk: lateral sequence, left hind, left fore, right hind, right fore
            p.gait = Gait::Walk;
            p.phase[BL] = 0.0f;
            p.phase[FL] = 0.25f;
            p.phase[BR] = 0.5f;
            p.phase[FR] = 0.75f;
            p.duty = 0.75f;
            break;
        }
        return p;
    }
    if (n % 2 == 0) {
        const int pairs = n / 2;
        if (p.gait == Gait::Pronk) {
            p.duty = 0.4f;
            return p;
        }
        if (p.gait == Gait::Walk || p.gait == Gait::Wave) {
            // A wave: one leg at a time, back to front, left side then right.
            p.gait = Gait::Wave;
            for (int i = 0; i < n; ++i) {
                const int pair = i / 2, side = i % 2;
                p.phase[i] = float(side * pairs + (pairs - 1 - pair)) / float(n);
            }
            p.duty = 1.0f - 1.0f / float(n);
            return p;
        }
        // Alternating tripods (tetrapods for eight legs): neighbours opposite.
        p.gait = Gait::Tripod;
        for (int i = 0; i < n; ++i) p.phase[i] = ((i / 2 + i % 2) % 2) * 0.5f;
        p.duty = 0.5f;
        return p;
    }
    for (int i = 0; i < n; ++i) p.phase[i] = float(i) / float(n);
    p.duty = 0.6f;
    return p;
}

float froudeNumber(float speed, float hipHeight) { return speed * speed / (kGravity * std::max(hipHeight, 1e-3f)); }

Gait gaitForSpeed(float speed, float hipHeight, int legCount) {
    const float fr = froudeNumber(speed, hipHeight);
    if (legCount == 2) return fr < 0.5f ? Gait::Walk : Gait::Trot;
    if (legCount == 4) return fr < 0.5f ? Gait::Walk : fr < 2.5f ? Gait::Trot : Gait::Gallop;
    if (legCount >= 6) return fr < 0.15f ? Gait::Wave : Gait::Tripod;
    return Gait::Walk;
}

std::vector<LegDesc> makeLegs(int count, float length, float width, float hipHeight) {
    std::vector<LegDesc> legs;
    if (count <= 0) return legs;
    const int pairs = count / 2;
    const bool spread = count >= 6;
    for (int i = 0; i < count; ++i) {
        LegDesc l;
        const bool odd = count % 2 == 1 && i == count - 1;
        const int pair = i / 2;
        const float side = odd ? 0.0f : (i % 2 == 0 ? 1.0f : -1.0f); // left = +X (facing +Z)
        const float z = pairs > 1 ? length * 0.5f - length * float(pair) / float(pairs - 1) : 0.0f;
        l.hip = glm::vec3(side * width * 0.5f, hipHeight, odd ? -length * 0.5f : z);
        if (spread) {
            l.restFoot = glm::vec3(side * (width * 0.5f + hipHeight * 1.1f), 0.0f, l.hip.z * 1.4f);
            const float reach = glm::length(l.restFoot - l.hip);
            l.upper = reach * 0.62f;
            l.lower = reach * 0.62f;
        } else {
            l.restFoot = glm::vec3(l.hip.x, 0.0f, l.hip.z);
            l.upper = hipHeight * 0.55f;
            l.lower = hipHeight * 0.55f;
        }
        legs.push_back(l);
    }
    return legs;
}

// =====================================================================
// ProceduralGait

ProceduralGait::ProceduralGait(std::vector<LegDesc> legs, const Settings& settings) : m_legs(std::move(legs)), m_s(settings) {
    m_feet.resize(m_legs.size());
    m_state.resize(m_legs.size());
    float hip = 0.0f, len = 0.0f;
    for (const LegDesc& l : m_legs) hip += l.hip.y, len += l.length();
    if (!m_legs.empty()) {
        m_hipHeight = std::max(0.01f, hip / float(m_legs.size()));
        m_legLength = std::max(0.01f, len / float(m_legs.size()));
    }
    m_wanted = m_s.gait == Gait::Auto ? Gait::Walk : m_s.gait;
    m_pattern = gaitPattern(m_wanted, legCount());
    const float period = glm::two_pi<float>() * std::sqrt(m_legLength / kGravity);
    m_cycle = m_s.cycleSlow * period;
}

glm::vec3 ProceduralGait::stepTarget(int i, const glm::mat4& body, const glm::vec3& velocity, float turnRate, float ahead,
                                     const SurfaceQuery& ground, glm::vec3& normal) const {
    // Raibert: under the hip when the foot lands, plus half the ground the
    // body covers while the foot is down, so it ends as far behind.
    const float t = ahead + 0.5f * m_pattern.duty * m_cycle;
    const glm::vec3 bodyPos = positionOf(body);
    const glm::mat4 predicted = glm::translate(glm::mat4(1.0f), bodyPos + horizontal(velocity) * t) *
                                glm::mat4_cast(glm::angleAxis(glm::radians(turnRate * t), kUp)) *
                                glm::translate(glm::mat4(1.0f), -bodyPos) * body;
    glm::vec3 target = glm::vec3(predicted * glm::vec4(m_legs[i].restFoot, 1.0f));
    normal = kUp;
    glm::vec3 hit, n;
    if (ground && ground(target + kUp * (m_hipHeight + m_legLength), hit, n)) {
        target = hit;
        normal = unitOr(n, kUp);
    }
    return target;
}

void ProceduralGait::reset(const glm::mat4& body, const SurfaceQuery& ground) {
    m_body = m_bodyPose = body;
    m_phase = 0.0f;
    m_height = m_pitch = m_roll = m_bobNow = 0.0f;
    m_moving = false;
    for (int i = 0; i < legCount(); ++i) {
        glm::vec3 normal;
        Foot& f = m_feet[i];
        f.position = stepTarget(i, body, glm::vec3(0.0f), 0.0f, 0.0f, ground, normal);
        f.normal = normal;
        f.planted = true;
        f.swing = 0.0f;
        f.landed = false;
        m_state[i] = LegState{};
        m_state[i].ground = f.position;
        m_state[i].lastPhase = fract(m_phase - m_pattern.phase[i]);
    }
    updateBody(body, glm::vec3(0.0f), 0.0f, 0.0f);
}

void ProceduralGait::update(const glm::mat4& body, const glm::vec3& velocity, float turnRate, const SurfaceQuery& ground, float dt) {
    if (!valid()) return;
    m_body = body;
    if (dt <= 0.0f) {
        updateBody(body, velocity, turnRate, 0.0f);
        return;
    }
    const int n = legCount();
    const float speed = glm::length(horizontal(velocity));

    const Gait want = m_s.gait == Gait::Auto ? gaitForSpeed(speed, m_hipHeight, n) : m_s.gait;
    if (want != m_wanted) {
        m_wanted = want;
        m_pattern = gaitPattern(want, n);
    }

    // Cycle length: as slow as the leg likes, but never so slow that a
    // foot on the ground would be dragged further than a stride.
    const float period = glm::two_pi<float>() * std::sqrt(m_legLength / kGravity);
    const float maxStride = m_s.strideScale * m_legLength;
    float cycle = m_s.cycleSlow * period;
    if (speed > 1e-4f) cycle = std::min(cycle, maxStride / (speed * std::max(0.05f, m_pattern.duty)));
    m_cycle = std::max(cycle, m_s.cycleFast * period);

    // Standing still: keep stepping only while a foot is out of place.
    m_moving = speed > 0.02f * m_legLength || std::abs(turnRate) > 5.0f;
    bool settling = false;
    for (int i = 0; i < n; ++i) {
        glm::vec3 unused;
        const glm::vec3 rest = stepTarget(i, body, glm::vec3(0.0f), 0.0f, 0.0f, nullptr, unused);
        if (!m_feet[i].planted || glm::length(horizontal(m_feet[i].position - rest)) > m_s.resettle * m_legLength) settling = true;
    }
    const bool stepping = m_moving || settling;
    if (stepping) m_phase = fract(m_phase + dt / m_cycle);

    int airborne = 0;
    for (const Foot& f : m_feet) airborne += f.planted ? 0 : 1;

    for (int i = 0; i < n; ++i) {
        Foot& f = m_feet[i];
        LegState& st = m_state[i];
        f.landed = false;
        const float lp = fract(m_phase - m_pattern.phase[i]);
        const bool inSwing = lp >= m_pattern.duty;
        const bool wasInSwing = st.lastPhase >= m_pattern.duty;
        st.lastPhase = lp;
        if (f.planted && stepping) {
            glm::vec3 unused;
            const glm::vec3 rest = stepTarget(i, body, glm::vec3(0.0f), 0.0f, 0.0f, nullptr, unused);
            const bool outOfPlace = glm::length(horizontal(f.position - rest)) > m_s.resettle * m_legLength;
            const glm::vec3 hip = glm::vec3(body * glm::vec4(m_legs[i].hip, 1.0f));
            // A foot left far behind (a sudden turn or shove) steps out of turn.
            const bool stretched = glm::length(f.position - hip) > m_legLength * 1.05f ||
                                   glm::length(horizontal(f.position - rest)) > maxStride * 0.9f;
            if ((inSwing && !wasInSwing && (m_moving || outOfPlace)) || (stretched && airborne * 2 < n)) {
                f.planted = false;
                st.from = f.position;
                st.swingTime = 0.0f;
                st.swingLength = std::max(0.05f, (1.0f - m_pattern.duty) * m_cycle);
                ++airborne;
            }
        }
        if (!f.planted) {
            st.swingTime += dt;
            const float t = std::min(1.0f, st.swingTime / st.swingLength);
            glm::vec3 normal;
            const glm::vec3 target = stepTarget(i, body, velocity, turnRate, (1.0f - t) * st.swingLength, ground, normal);
            const float e = t * t * (3.0f - 2.0f * t);
            st.ground = glm::mix(st.from, target, e);
            // Clear whichever end is higher, then the arc on top.
            const float lift = m_s.stepHeight * m_legLength * std::sin(glm::pi<float>() * t);
            f.position = st.ground + kUp * lift;
            f.normal = glm::normalize(glm::mix(f.normal, normal, e));
            f.swing = t;
            if (t >= 1.0f) {
                f.position = st.ground = target;
                f.normal = normal;
                f.planted = true;
                f.swing = 0.0f;
                f.landed = true;
                --airborne;
            }
        } else {
            st.ground = f.position;
        }
    }
    updateBody(body, velocity, turnRate, dt);
}

void ProceduralGait::updateBody(const glm::mat4& body, const glm::vec3& velocity, float turnRate, float dt) {
    const int n = legCount();
    const glm::mat4 toBody = glm::inverse(body);
    std::vector<float> xs, zs, ys;
    float height = 0.0f, lift = 0.0f;
    for (int i = 0; i < n; ++i) {
        const glm::vec3 local = glm::vec3(toBody * glm::vec4(m_state[i].ground, 1.0f));
        xs.push_back(local.x);
        zs.push_back(local.z);
        ys.push_back(local.y);
        height += local.y;
        if (!m_feet[i].planted) lift += std::sin(glm::pi<float>() * m_feet[i].swing);
    }
    height = n ? height / float(n) : 0.0f;
    height = glm::clamp(height, -0.5f * m_hipHeight, 0.5f * m_hipHeight);
    const float maxTilt = glm::radians(m_s.maxTilt);
    float pitch = std::atan(slope(zs, ys)) * m_s.bodyFollow; // + = nose up
    float roll = std::atan(slope(xs, ys)) * m_s.bodyFollow;  // + = left side up
    // Into the turn, like a cyclist: tan(lean) = v * omega / g.
    const float speed = glm::length(horizontal(velocity));
    roll -= std::atan(speed * glm::radians(turnRate) / kGravity) * m_s.lean;
    pitch = glm::clamp(pitch, -maxTilt, maxTilt);
    roll = glm::clamp(roll, -maxTilt, maxTilt);
    const float swingingShare = n ? lift / std::max(1.0f, float(n) * (1.0f - m_pattern.duty)) : 0.0f;
    const float bob = m_moving ? m_s.bob * m_legLength * std::min(1.0f, swingingShare) : 0.0f;

    const float k = smoothFactor(m_s.smoothing, dt);
    m_height += (height - m_height) * k;
    m_pitch += (glm::degrees(pitch) - m_pitch) * k;
    m_roll += (glm::degrees(roll) - m_roll) * k;
    m_bobNow += (bob - m_bobNow) * k;

    // Tilt about the hips' height, not the ground point, so the body
    // doesn't swing sideways as it rolls.
    const glm::mat4 pivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_hipHeight, 0.0f));
    // Nose up is a turn about -X (facing +Z); left side up about +Z.
    const glm::mat4 tilt = glm::mat4_cast(glm::angleAxis(glm::radians(m_roll), glm::vec3(0, 0, 1)) *
                                          glm::angleAxis(glm::radians(-m_pitch), glm::vec3(1, 0, 0)));
    m_bodyPose = glm::translate(glm::mat4(1.0f), kUp * (m_height + m_bobNow)) * body * pivot * tilt * glm::inverse(pivot);
    for (int i = 0; i < n; ++i) m_feet[i].hip = glm::vec3(m_bodyPose * glm::vec4(m_legs[i].hip, 1.0f));
}

glm::vec3 ProceduralGait::knee(int i) const {
    const LegDesc& l = m_legs[i];
    const glm::vec3 up = glm::mat3(m_bodyPose) * kUp;
    const glm::vec3 forward = glm::mat3(m_bodyPose) * glm::vec3(0, 0, 1);
    // Front legs and bipeds' knees go forward, hind hocks back; splayed
    // legs (insects, spiders) bend up.
    const bool hind = legCount() >= 4 && legCount() < 6 && l.hip.z < 0.0f;
    const glm::vec3 bend = up + (hind ? -forward : forward) * (legCount() >= 6 ? 0.0f : 1.0f);
    return kneePosition(m_feet[i].hip, m_feet[i].position, l.upper, l.lower, bend);
}

// ---------------------------------------------------------------------
// Skeletons

std::vector<TwoBoneChain> quadrupedLegChains(const ModelData& model, const QuadrupedBones& bones) {
    std::vector<TwoBoneChain> chains;
    for (int i = 0; i < 4; ++i) chains.push_back(findChain(model, bones.upperLeg[i], bones.lowerLeg[i], bones.foot[i]));
    return chains;
}

std::vector<LegDesc> legsFromSkeleton(const ModelData& model, const std::vector<TwoBoneChain>& chains, const glm::mat4& modelToBody) {
    const std::vector<glm::mat4> rest = restWorld(model);
    std::vector<LegDesc> legs;
    for (const TwoBoneChain& c : chains) {
        if (!c.valid()) continue;
        const glm::vec3 hip = glm::vec3(modelToBody * glm::vec4(positionOf(rest[c.upper]), 1.0f));
        const glm::vec3 knee = glm::vec3(modelToBody * glm::vec4(positionOf(rest[c.lower]), 1.0f));
        const glm::vec3 foot = glm::vec3(modelToBody * glm::vec4(positionOf(rest[c.end]), 1.0f));
        LegDesc l;
        l.hip = hip;
        l.restFoot = foot;
        l.upper = std::max(1e-3f, glm::length(knee - hip));
        l.lower = std::max(1e-3f, glm::length(foot - knee));
        legs.push_back(l);
    }
    return legs;
}

void applyGait(const ModelData& model, Pose& pose, const std::vector<TwoBoneChain>& chains, int bodyBone, const ProceduralGait& gait,
               const glm::mat4& modelWorld, float weight) {
    weight = glm::clamp(weight, 0.0f, 1.0f);
    if (weight <= 0.0f || !gait.valid()) return;
    const glm::mat4 worldToModel = glm::inverse(modelWorld);
    std::vector<glm::mat4> world = poseToModel(model, pose);

    // The body's sway (height, bob, pitch, roll, lean) in model space.
    if (bodyBone >= 0 && bodyBone < static_cast<int>(pose.size())) {
        const glm::mat4 sway = worldToModel * gait.bodyPose() * glm::inverse(gait.body()) * modelWorld;
        const glm::quat r = glm::slerp(glm::quat(1, 0, 0, 0), rotationOf(sway), weight);
        const glm::vec3 pivot = positionOf(world[bodyBone]);
        const glm::vec3 moved = glm::vec3(sway * glm::vec4(pivot, 1.0f));
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::mix(pivot, moved, weight)) * glm::mat4_cast(r) *
                            glm::translate(glm::mat4(1.0f), -pivot) * world[bodyBone];
        setModelTransform(model, pose, world, bodyBone, m);
        world = poseToModel(model, pose);
    }

    const int n = std::min(static_cast<int>(chains.size()), gait.legCount());
    int leg = 0;
    for (int c = 0; c < static_cast<int>(chains.size()) && leg < n; ++c) {
        const TwoBoneChain& ch = chains[c];
        if (!ch.valid()) continue; // legsFromSkeleton skipped it too
        const ProceduralGait::Foot& f = gait.foot(leg++);
        const glm::vec3 target = glm::vec3(worldToModel * glm::vec4(f.position, 1.0f));
        const glm::quat footModel = rotationOf(world[ch.end]);
        const glm::vec3 hip = positionOf(world[ch.upper]), knee = positionOf(world[ch.lower]), end = positionOf(world[ch.end]);
        glm::vec3 bend = knee - (hip + end) * 0.5f;
        if (glm::length(bend) < 1e-4f) bend = glm::vec3(0, 0, 1);
        solveTwoBone(model, pose, ch, target, knee + glm::normalize(bend) * 0.5f, weight);
        // The foot keeps its own orientation, tilted to the ground under it.
        const std::vector<glm::mat4> solved = poseToModel(model, pose);
        const glm::vec3 normal = glm::normalize(glm::mat3(worldToModel) * f.normal);
        const glm::quat tilt = glm::slerp(glm::quat(1, 0, 0, 0), rotationBetween(kUp, normal), f.planted ? weight : 0.0f);
        const int parent = model.bones[ch.end].parent;
        const glm::quat parentModel = parent >= 0 ? rotationOf(solved[parent]) : glm::quat(1, 0, 0, 0);
        pose[ch.end].r = glm::normalize(glm::inverse(parentModel) * tilt * footModel);
        world = poseToModel(model, pose);
    }
}

// ---------------------------------------------------------------------
// LegPlacer

LegPlacer::LegPlacer(std::vector<TwoBoneChain> legs, int bodyBone, const Settings& settings)
    : m_legs(std::move(legs)), m_body(bodyBone), m_s(settings), m_footOffset(m_legs.size(), 0.0f) {}

bool LegPlacer::valid() const {
    if (m_legs.empty() || m_body < 0) return false;
    for (const TwoBoneChain& c : m_legs)
        if (!c.valid()) return false;
    return true;
}

void LegPlacer::apply(const ModelData& model, Pose& pose, const SurfaceQuery& ground, float dt, float weight) {
    if (!valid() || m_body >= static_cast<int>(pose.size())) return;
    weight = glm::clamp(weight, 0.0f, 1.0f);
    const size_t n = m_legs.size();
    std::vector<glm::mat4> world = poseToModel(model, pose);
    std::vector<glm::vec3> feet(n);
    std::vector<float> want(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        feet[i] = positionOf(world[m_legs[i].end]);
        glm::vec3 hit, normal;
        if (weight > 0.0f && ground && ground(feet[i] + kUp * m_s.probeUp, hit, normal))
            want[i] = glm::clamp(hit.y, -m_s.maxDrop, m_s.maxRaise) * weight;
    }

    // Which way is forward and left, from where the legs are.
    const glm::vec3 first = (positionOf(world[m_legs[0].upper]) + (n > 1 ? positionOf(world[m_legs[1].upper]) : glm::vec3(0.0f))) *
                            (n > 1 ? 0.5f : 1.0f);
    const glm::vec3 last = n >= 4 ? (positionOf(world[m_legs[n - 2].upper]) + positionOf(world[m_legs[n - 1].upper])) * 0.5f : first;
    const glm::vec3 left = n > 1 ? unitOr(horizontal(positionOf(world[m_legs[0].upper]) - positionOf(world[m_legs[1].upper])),
                                          glm::vec3(1, 0, 0))
                                 : glm::vec3(1, 0, 0);
    const glm::vec3 forward = n >= 4 ? unitOr(horizontal(first - last), glm::cross(left, kUp)) : glm::cross(left, kUp);
    std::vector<float> along(n), across(n);
    for (size_t i = 0; i < n; ++i) {
        along[i] = glm::dot(feet[i], forward);
        across[i] = glm::dot(feet[i], left);
    }
    const float maxTilt = glm::radians(m_s.maxTilt);
    const float pitch = glm::clamp(std::atan(slope(along, want)) * m_s.bodyFollow, -maxTilt, maxTilt);
    const float roll = glm::clamp(std::atan(slope(across, want)) * m_s.bodyFollow, -maxTilt, maxTilt);

    const float k = smoothFactor(m_s.smoothing, dt);
    for (size_t i = 0; i < n; ++i) m_footOffset[i] += (want[i] - m_footOffset[i]) * k;
    m_pitch += (glm::degrees(pitch) - m_pitch) * k;
    m_roll += (glm::degrees(roll) - m_roll) * k;

    const glm::quat tilt = glm::angleAxis(glm::radians(m_roll), forward) * glm::angleAxis(glm::radians(m_pitch), glm::cross(forward, kUp));
    const glm::vec3 pivot = positionOf(world[m_body]);
    // How far the body must come down for the lowest foot to reach.
    float drop = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 moved = pivot + tilt * (feet[i] - pivot);
        drop = std::min(drop, feet[i].y + m_footOffset[i] - moved.y);
    }
    drop = std::max(drop, -m_s.maxDrop);
    m_bodyOffset += (drop - m_bodyOffset) * k;

    bool still = std::abs(m_bodyOffset) < 1e-5f && std::abs(m_pitch) < 1e-4f && std::abs(m_roll) < 1e-4f;
    for (float o : m_footOffset) still = still && std::abs(o) < 1e-5f;
    if (still) return;

    std::vector<glm::quat> footModel(n);
    for (size_t i = 0; i < n; ++i) footModel[i] = rotationOf(world[m_legs[i].end]);
    const glm::mat4 bodyM = glm::translate(glm::mat4(1.0f), pivot + kUp * m_bodyOffset) * glm::mat4_cast(tilt) *
                            glm::translate(glm::mat4(1.0f), -pivot) * world[m_body];
    setModelTransform(model, pose, world, m_body, bodyM);
    world = poseToModel(model, pose);
    for (size_t i = 0; i < n; ++i) {
        const TwoBoneChain& c = m_legs[i];
        const glm::vec3 hip = positionOf(world[c.upper]), knee = positionOf(world[c.lower]), end = positionOf(world[c.end]);
        glm::vec3 bend = knee - (hip + end) * 0.5f;
        if (glm::length(bend) < 1e-4f) bend = forward;
        solveTwoBone(model, pose, c, feet[i] + kUp * m_footOffset[i], knee + glm::normalize(bend) * 0.5f);
        const std::vector<glm::mat4> solved = poseToModel(model, pose);
        const int parent = model.bones[c.end].parent;
        const glm::quat parentModel = parent >= 0 ? rotationOf(solved[parent]) : glm::quat(1, 0, 0, 0);
        pose[c.end].r = glm::normalize(glm::inverse(parentModel) * footModel[i]);
        world = poseToModel(model, pose);
    }
}

// =====================================================================
// Active ragdoll

std::vector<glm::mat4> ragdollTargetsFromPose(const RagdollSkinBinding& binding, const std::vector<glm::mat4>& boneWorld, size_t bodyCount) {
    std::vector<glm::mat4> out(bodyCount, glm::mat4(1.0f));
    std::vector<bool> found(bodyCount, false);
    const size_t n = std::min({ binding.bodyOfBone.size(), binding.boneOffset.size(), boneWorld.size() });
    for (size_t b = 0; b < n; ++b) {
        const int body = binding.bodyOfBone[b];
        if (body < 0 || static_cast<size_t>(body) >= bodyCount || found[body]) continue;
        const glm::mat4 m = boneWorld[b] * glm::inverse(binding.boneOffset[b]);
        // Rigid: bodies have no scale.
        out[body] = glm::translate(glm::mat4(1.0f), positionOf(m)) * glm::mat4_cast(rotationOf(m));
        found[body] = true;
    }
    return out;
}

ActiveRagdoll::ActiveRagdoll(const RagdollDesc& desc, const Settings& settings) : m_desc(desc), m_s(settings) {
    const size_t nb = desc.bodies.size(), nj = desc.joints.size();
    if (nb == 0) return;
    m_bodyJoint.assign(nb, -1);
    m_strength.assign(nj, 1.0f);
    m_adjacent.assign(nj, {});
    for (size_t j = 0; j < nj; ++j) {
        const RagdollJoint& a = desc.joints[j];
        if (a.bodyB >= 0 && static_cast<size_t>(a.bodyB) < nb) m_bodyJoint[a.bodyB] = static_cast<int>(j);
        for (size_t k = 0; k < nj; ++k) {
            if (k == j) continue;
            const RagdollJoint& b = desc.joints[k];
            if (a.bodyA == b.bodyA || a.bodyA == b.bodyB || a.bodyB == b.bodyA || a.bodyB == b.bodyB) m_adjacent[j].push_back(static_cast<int>(k));
        }
    }
    m_pelvis = desc.findBody("pelvis");
    m_torso = desc.findBody("torso");
    if (m_torso < 0) m_torso = desc.findBody("chest");
    if (m_pelvis < 0) m_pelvis = 0;
    m_targets.resize(nb);
    for (size_t b = 0; b < nb; ++b) m_targets[b] = desc.bodies[b].transform;
}

void ActiveRagdoll::setTargets(const RagdollSkinBinding& binding, const std::vector<glm::mat4>& boneWorld) {
    m_targets = ragdollTargetsFromPose(binding, boneWorld, m_desc.bodies.size());
}

void ActiveRagdoll::enter(State s) {
    m_state = s;
    m_time = 0.0f;
    m_calm = 0.0f;
    if (s == State::Fallen) {
        std::fill(m_strength.begin(), m_strength.end(), 0.0f);
        m_balance = 0.0f;
    } else if (s == State::Animated) {
        std::fill(m_strength.begin(), m_strength.end(), 1.0f);
        m_balance = 1.0f;
    }
}

void ActiveRagdoll::hit(int body, const glm::vec3& push) {
    if (!valid() || body < 0 || static_cast<size_t>(body) >= m_desc.bodies.size()) return;
    const float speed = glm::length(push);
    if (!std::isfinite(speed)) return;
    if (m_state == State::GettingUp) {
        enter(State::Fallen);
        return;
    }
    if (m_state == State::Fallen) return;
    if (m_state == State::Animated) enter(State::Active);
    // Weaken the joints at the body, then outward, halving each step.
    std::vector<int> depth(m_strength.size(), -1);
    std::deque<int> open;
    for (size_t j = 0; j < m_desc.joints.size(); ++j)
        if (m_desc.joints[j].bodyA == body || m_desc.joints[j].bodyB == body) {
            depth[j] = 0;
            open.push_back(static_cast<int>(j));
        }
    while (!open.empty()) {
        const int j = open.front();
        open.pop_front();
        if (depth[j] >= m_s.hitSpread) continue;
        for (int k : m_adjacent[j])
            if (depth[k] < 0) {
                depth[k] = depth[j] + 1;
                open.push_back(k);
            }
    }
    const float drop = m_s.hitWeakening * speed;
    for (size_t j = 0; j < m_strength.size(); ++j)
        if (depth[j] >= 0) m_strength[j] = std::max(std::min(m_strength[j], m_s.minStrength),
                                                    m_strength[j] - drop / float(1 << depth[j]));
    m_balance = std::max(0.0f, m_balance - m_s.balanceLoss * speed);
    m_calm = 0.0f;
}

void ActiveRagdoll::knockOut() {
    if (valid()) enter(State::Fallen);
}

void ActiveRagdoll::update(float dt, const std::vector<glm::mat4>& bodyWorld) {
    if (!valid() || dt <= 0.0f) return;
    m_time += dt;
    switch (m_state) {
    case State::Animated:
        return;
    case State::Fallen:
        if (m_time >= m_s.getUpDelay) enter(State::GettingUp);
        return;
    case State::GettingUp:
        if (m_time >= m_s.getUpSeconds) enter(State::Animated);
        return;
    case State::Active:
        break;
    }
    // Balance lost this frame is a fall, before it starts recovering.
    bool fell = m_balance <= 0.0f;
    for (float& s : m_strength) s = std::min(1.0f, s + m_s.recoverPerSecond * dt);
    m_balance = std::min(1.0f, m_balance + m_s.balanceRecover * dt);

    float tilt = 0.0f;
    const size_t nb = m_desc.bodies.size();
    if (bodyWorld.size() == nb && m_targets.size() == nb) {
        const glm::vec3 pelvis = positionOf(bodyWorld[m_pelvis]), pelvisT = positionOf(m_targets[m_pelvis]);
        if (m_torso >= 0) {
            const glm::vec3 now = positionOf(bodyWorld[m_torso]) - pelvis, want = positionOf(m_targets[m_torso]) - pelvisT;
            if (glm::length(now) > 1e-4f && glm::length(want) > 1e-4f)
                tilt = glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(now), glm::normalize(want)), -1.0f, 1.0f)));
        }
        float lowest = pelvisT.y;
        for (const glm::mat4& t : m_targets) lowest = std::min(lowest, positionOf(t).y);
        const float height = std::max(0.1f, pelvisT.y - lowest);
        fell = fell || tilt > m_s.fallTilt || pelvisT.y - pelvis.y > m_s.fallDrop * height;
    }
    if (fell) {
        enter(State::Fallen);
        return;
    }
    bool strong = m_balance >= 0.999f && tilt < 12.0f;
    for (float s : m_strength) strong = strong && s >= 0.999f;
    m_calm = strong ? m_calm + dt : 0.0f;
    if (m_calm >= m_s.calmSeconds) enter(State::Animated);
}

RagdollDrive ActiveRagdoll::drive() const {
    RagdollDrive d;
    if (!physical()) return d;
    d.targets = m_targets;
    d.jointStrength = m_strength;
    d.assistBody = m_pelvis;
    d.assist = m_state == State::Active ? m_s.balanceAssist * m_balance : 0.0f;
    return d;
}

float ActiveRagdoll::getUpBlend() const {
    switch (m_state) {
    case State::Animated: return 1.0f;
    case State::GettingUp: return blendWeight(m_time, m_s.getUpSeconds);
    default: return 0.0f;
    }
}

} // namespace kke
