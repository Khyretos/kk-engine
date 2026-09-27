#include "ProceduralDemoModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace procedural_demo {

namespace {

const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}

std::string envString(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

float wrapDegrees(float d) {
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

kke::ModelBone bone(const std::string& name, int parent, const glm::vec3& offset) {
    kke::ModelBone b;
    b.name = name;
    b.parent = parent;
    b.localRest = glm::translate(glm::mat4(1.0f), offset);
    return b;
}

int addBone(kke::ModelData& m, const std::string& name, int parent, const glm::vec3& offset) {
    m.bones.push_back(bone(name, parent, offset));
    return static_cast<int>(m.bones.size()) - 1;
}

// ------------------------------------------------------------- mesh building

void appendEllipsoid(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::mat4& m, const glm::vec3& color) {
    constexpr int rings = 8, segments = 12;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(m)));
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (int r = 0; r <= rings; ++r) {
        const float phi = glm::pi<float>() * float(r) / float(rings);
        for (int s = 0; s <= segments; ++s) {
            const float theta = glm::two_pi<float>() * float(s) / float(segments);
            const glm::vec3 p(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            kke::Vertex vert{};
            vert.position = glm::vec3(m * glm::vec4(p, 1.0f));
            vert.normal = glm::normalize(normalMatrix * p);
            vert.color = color;
            v.push_back(vert);
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = base + uint32_t(r * (segments + 1) + s), b = a + segments + 1;
            idx.insert(idx.end(), { a, a + 1, b, b, a + 1, b + 1 });
        }
}

void appendCapsule(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& a, const glm::vec3& b, float r,
                   const glm::vec3& color) {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    const glm::vec3 axis = len > 1e-5f ? d / len : kUp;
    const glm::vec3 side = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.9f ? kUp : glm::vec3(1, 0, 0)));
    const glm::vec3 other = glm::cross(axis, side);
    constexpr int segments = 10;
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (int end = 0; end < 2; ++end)
        for (int s = 0; s <= segments; ++s) {
            const float t = glm::two_pi<float>() * float(s) / float(segments);
            const glm::vec3 n = side * std::cos(t) + other * std::sin(t);
            kke::Vertex vert{};
            vert.position = (end ? b : a) + n * r;
            vert.normal = n;
            vert.color = color;
            v.push_back(vert);
        }
    for (int s = 0; s < segments; ++s) {
        const uint32_t i0 = base + uint32_t(s), i1 = i0 + 1, j0 = i0 + segments + 1, j1 = j0 + 1;
        idx.insert(idx.end(), { i0, j0, i1, i1, j0, j1 });
    }
    for (const glm::vec3& c : { a, b }) appendEllipsoid(v, idx, glm::scale(glm::translate(glm::mat4(1.0f), c), glm::vec3(r)), color);
}

void appendBox(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& center, const glm::vec3& half, const glm::vec3& color) {
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : { -1, 1 }) {
            glm::vec3 n(0.0f);
            n[axis] = float(sign);
            const glm::vec3 u = axis == 1 ? glm::vec3(1, 0, 0) : kUp;
            const glm::vec3 w = glm::cross(n, u);
            const uint32_t base = static_cast<uint32_t>(v.size());
            for (int k = 0; k < 4; ++k) {
                const float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sw = k >= 2 ? 1.0f : -1.0f;
                kke::Vertex vert{};
                vert.position = center + (n + u * su + w * sw) * half;
                vert.normal = n;
                vert.color = color;
                v.push_back(vert);
            }
            idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
}

struct Step { glm::vec3 center, half; };
// A flight of steps up to a platform: something for feet to find.
const std::vector<Step>& steps() {
    static const std::vector<Step> s = [] {
        std::vector<Step> out;
        for (int i = 0; i < 4; ++i) {
            const float h = 0.12f * float(i + 1);
            out.push_back({ glm::vec3(3.25f + 0.5f * float(i) + (i == 3 ? 1.0f : 0.0f), h * 0.5f, 0.0f),
                            glm::vec3(i == 3 ? 1.25f : 0.25f, h * 0.5f, 2.0f) });
        }
        return out;
    }();
    return s;
}

} // namespace

ProceduralDemoModule::ProceduralDemoModule() = default;
ProceduralDemoModule::~ProceduralDemoModule() {
    if (m_rigid)
        for (Creature& c : m_creatures)
            if (c.handle) m_rigid->destroyRagdoll(c.handle);
}

std::vector<kke::ModuleDependency> ProceduralDemoModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the ground under the feet, and ragdolls with joint motors (Jolt)" },
             { std::type_index(typeid(kke::OrbitCameraModule)), false, "the camera" } };
}

// ------------------------------------------------------------- ground

float ProceduralDemoModule::groundHeight(float x, float z) const {
    // Flat around the steps, hills further out.
    const float r = std::sqrt(x * x + z * z);
    const float amp = glm::smoothstep(7.0f, 14.0f, r);
    return amp * (0.9f * std::sin(0.28f * x + 0.4f) * std::cos(0.23f * z) + 0.35f * std::sin(0.9f * x) * std::sin(0.8f * z + 1.0f)) +
           0.04f * std::sin(2.3f * x) * std::cos(1.9f * z);
}

bool ProceduralDemoModule::ground(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) const {
    // Only the level: not a ragdoll lying on it.
    const kke::RigidWorld::RayHit h = m_rigid->world().raycast(from, glm::vec3(0, -1, 0), 6.0f, [](kke::RigidWorld::BodyId, kke::RigidWorld::Motion m) {
        return m == kke::RigidWorld::Motion::Static;
    });
    if (!h.hit) return false;
    hit = h.point;
    normal = h.normal;
    return true;
}

void ProceduralDemoModule::buildGround() {
    constexpr float half = 24.0f, step = 0.4f;
    const int n = static_cast<int>(2.0f * half / step) + 1;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    std::vector<glm::vec3> points;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = -half + step * float(i), z = -half + step * float(j);
            const float y = groundHeight(x, z);
            const float e = 0.05f;
            const glm::vec3 normal = glm::normalize(glm::vec3(groundHeight(x - e, z) - groundHeight(x + e, z), 2.0f * e,
                                                              groundHeight(x, z - e) - groundHeight(x, z + e)));
            kke::Vertex vert{};
            vert.position = glm::vec3(x, y, z);
            vert.normal = normal;
            const float t = 0.5f + 0.5f * std::sin(0.7f * x + 1.3f * z) * std::cos(1.1f * x - 0.4f * z);
            vert.color = glm::mix(glm::vec3(0.30f, 0.46f, 0.20f), glm::vec3(0.42f, 0.55f, 0.24f), t);
            v.push_back(vert);
            points.push_back(vert.position);
        }
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const uint32_t a = uint32_t(j * n + i), b = a + 1, c = a + uint32_t(n), d = c + 1;
            idx.insert(idx.end(), { a, c, b, b, c, d });
        }
    kke::RigidWorld& w = m_rigid->world();
    kke::RigidWorld::BodyDesc terrain;
    terrain.shape = kke::RigidWorld::Shape::Mesh;
    terrain.motion = kke::RigidWorld::Motion::Static;
    terrain.points = points;
    terrain.indices = idx;
    terrain.friction = 0.9f;
    w.add(terrain);
    for (const Step& s : steps()) {
        appendBox(v, idx, s.center, s.half, glm::vec3(0.62f, 0.58f, 0.52f));
        kke::RigidWorld::BodyDesc box;
        box.motion = kke::RigidWorld::Motion::Static;
        box.halfExtents = s.half;
        box.position = s.center;
        box.friction = 0.9f;
        w.add(box);
    }
    m_groundMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_groundMesh->upload(v, idx);
}

// ------------------------------------------------------------- creatures

ProceduralDemoModule::Creature ProceduralDemoModule::makeSpider(int legCount, const std::string& name, float size) const {
    Creature c;
    const bool spider = legCount == 8;
    c.kind = spider ? Kind::Spider : Kind::Beetle;
    c.name = name;
    const std::vector<kke::LegDesc> legs = spider ? kke::makeLegs(8, 0.12f * size, 0.09f * size, 0.13f * size)
                                                  : kke::makeLegs(6, 0.2f * size, 0.13f * size, 0.08f * size);
    const float hipHeight = legs[0].hip.y;
    const int root = addBone(c.rig, "Root", -1, glm::vec3(0.0f));
    c.body = addBone(c.rig, "Body", root, glm::vec3(0.0f, hipHeight, 0.0f));
    const int head = addBone(c.rig, "Head", c.body, glm::vec3(0.0f, 0.01f * size, (spider ? 0.1f : 0.16f) * size));
    for (size_t i = 0; i < legs.size(); ++i) {
        const kke::LegDesc& l = legs[i];
        const glm::vec3 knee = kke::kneePosition(l.hip, l.restFoot, l.upper, l.lower, kUp);
        const std::string n = "Leg" + std::to_string(i);
        const int up = addBone(c.rig, n + "Up", c.body, l.hip - glm::vec3(0, hipHeight, 0));
        const int low = addBone(c.rig, n + "Low", up, knee - l.hip);
        const int foot = addBone(c.rig, n + "Foot", low, l.restFoot - knee);
        c.legs.push_back({ up, low, foot });
    }
    const glm::vec3 shell = spider ? glm::vec3(0.18f, 0.12f, 0.1f) : glm::vec3(0.12f, 0.32f, 0.2f);
    const glm::vec3 dark = spider ? glm::vec3(0.1f, 0.08f, 0.07f) : glm::vec3(0.06f, 0.14f, 0.09f);
    if (spider) {
        c.parts.push_back({ c.body, -1, 0.0f, glm::vec3(0.06f, 0.045f, 0.07f) * size, glm::vec3(0.0f), dark });
        c.parts.push_back({ c.body, -1, 0.0f, glm::vec3(0.085f, 0.075f, 0.11f) * size, glm::vec3(0.0f, 0.03f, -0.15f) * size, shell });
        c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.035f) * size, glm::vec3(0.0f), dark });
    } else {
        c.parts.push_back({ c.body, -1, 0.0f, glm::vec3(0.09f, 0.05f, 0.17f) * size, glm::vec3(0.0f, 0.02f, -0.04f) * size, shell });
        c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.05f, 0.035f, 0.045f) * size, glm::vec3(0.0f), dark });
    }
    for (const kke::TwoBoneChain& l : c.legs) {
        c.parts.push_back({ l.upper, l.lower, 0.012f * size, glm::vec3(1.0f), glm::vec3(0.0f), dark });
        c.parts.push_back({ l.lower, l.end, 0.009f * size, glm::vec3(1.0f), glm::vec3(0.0f), dark });
    }
    c.rest = kke::AnimationSet(c.rig).restPose();
    c.gait = kke::ProceduralGait(kke::legsFromSkeleton(c.rig, c.legs));
    kke::LookAt::Settings look;
    look.maxYaw = 50.0f;
    look.maxPitch = 30.0f;
    c.look = kke::LookAt({ { head, 1.0f } }, glm::vec3(0, 0, 1), look);
    c.cruise = spider ? 0.55f : 0.35f;
    c.maxTurn = spider ? 240.0f : 120.0f;
    return c;
}

ProceduralDemoModule::Creature ProceduralDemoModule::makeDog() const {
    Creature c;
    c.kind = Kind::Dog;
    c.name = "dog";
    kke::ModelData& m = c.rig;
    // Quaternius farm-animal bone names, so buildQuadrupedRagdoll and
    // quadrupedLegChains find everything.
    const int root = addBone(m, "Root", -1, glm::vec3(0.0f));
    const int hips = addBone(m, "Hips", root, { 0, 0.5f, -0.25f });
    const int torso = addBone(m, "Torso", hips, { 0, 0.02f, 0.25f });
    const int shoulders = addBone(m, "Shoulders", torso, { 0, 0.0f, 0.25f });
    const int neck = addBone(m, "Neck", shoulders, { 0, 0.08f, 0.06f });
    const int head = addBone(m, "Head", neck, { 0, 0.12f, 0.08f });
    const char* up[4] = { "FrontUpLeg.L", "FrontUpLeg.R", "BackUpLeg.L", "BackUpLeg.R" };
    const char* low[4] = { "FrontLowLeg.L", "FrontLowLeg.R", "BackLowLeg.L", "BackLowLeg.R" };
    const char* foot[4] = { "FrontFoot.L", "FrontFoot.R", "BackFoot.L", "BackFoot.R" };
    for (int i = 0; i < 4; ++i) {
        const bool front = i < 2;
        const float side = i % 2 == 0 ? 0.1f : -0.1f;
        const int u = addBone(m, up[i], front ? shoulders : hips, { side, -0.02f, 0.0f });
        const float seg = front ? 0.25f : 0.24f;
        const int l = addBone(m, low[i], u, { 0, -seg, front ? 0.05f : -0.07f });
        addBone(m, foot[i], l, { 0, -seg, front ? -0.05f : 0.07f });
    }
    int tail = hips;
    for (int i = 1; i <= 4; ++i) tail = addBone(m, "Tail" + std::to_string(i), tail, i == 1 ? glm::vec3(0, 0.04f, -0.06f) : glm::vec3(0, 0.03f, -0.08f));
    c.body = hips;
    c.legs = kke::quadrupedLegChains(m);
    c.tail = kke::findIkChain(m, "Tail1", "Tail4");
    const glm::vec3 coat(0.72f, 0.5f, 0.28f), dark(0.35f, 0.22f, 0.12f), paw(0.92f, 0.86f, 0.76f);
    c.parts.push_back({ hips, shoulders, 0.12f, glm::vec3(1.0f), glm::vec3(0.0f), coat });
    c.parts.push_back({ shoulders, head, 0.07f, glm::vec3(1.0f), glm::vec3(0.0f), coat });
    c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.075f, 0.08f, 0.09f), glm::vec3(0.0f), coat });
    c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.045f, 0.04f, 0.075f), glm::vec3(0.0f, -0.025f, 0.1f), paw });
    c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.02f), glm::vec3(0.0f, -0.01f, 0.175f), glm::vec3(0.05f) });
    for (float s : { 1.0f, -1.0f }) c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.022f, 0.05f, 0.032f), glm::vec3(0.055f * s, 0.07f, -0.02f), dark });
    for (const kke::TwoBoneChain& l : c.legs) {
        c.parts.push_back({ l.upper, l.lower, 0.045f, glm::vec3(1.0f), glm::vec3(0.0f), coat });
        c.parts.push_back({ l.lower, l.end, 0.03f, glm::vec3(1.0f), glm::vec3(0.0f), coat });
        c.parts.push_back({ l.end, -1, 0.0f, glm::vec3(0.035f, 0.025f, 0.045f), glm::vec3(0.0f, 0.0f, 0.015f), paw });
    }
    for (size_t i = 0; i + 1 < c.tail.bones.size(); ++i)
        c.parts.push_back({ c.tail.bones[i], c.tail.bones[i + 1], 0.028f - 0.004f * float(i), glm::vec3(1.0f), glm::vec3(0.0f), coat });
    c.rest = kke::AnimationSet(m).restPose();
    c.gait = kke::ProceduralGait(kke::legsFromSkeleton(m, c.legs));
    c.look = kke::LookAt::quadruped(m);
    c.cruise = 1.2f;
    c.maxTurn = 160.0f;
    c.canRagdoll = true;
    c.mass = 25.0f;
    return c;
}

ProceduralDemoModule::Creature ProceduralDemoModule::makePerson() const {
    Creature c;
    c.kind = Kind::Person;
    c.name = "person";
    kke::ModelData& m = c.rig;
    // Synty / Unreal-style names: buildHumanoidRagdoll and LookAt::humanoid know them.
    const int root = addBone(m, "Root", -1, glm::vec3(0.0f));
    const int pelvis = addBone(m, "Pelvis", root, { 0, 0.97f, 0 });
    const int s1 = addBone(m, "spine_01", pelvis, { 0, 0.1f, 0 });
    const int s2 = addBone(m, "spine_02", s1, { 0, 0.15f, 0 });
    const int s3 = addBone(m, "spine_03", s2, { 0, 0.15f, 0 });
    const int neck = addBone(m, "neck_01", s3, { 0, 0.15f, 0 });
    const int head = addBone(m, "head", neck, { 0, 0.1f, 0 });
    const glm::vec3 skin(0.87f, 0.69f, 0.55f), shirt(0.2f, 0.38f, 0.66f), trousers(0.2f, 0.2f, 0.26f), shoes(0.12f, 0.1f, 0.09f);
    for (int side : { 1, -1 }) {
        const bool left = side > 0;
        const int ua = addBone(m, left ? "UpperArm_L" : "UpperArm_R", s3, { side * 0.2f, 0.05f, 0 });
        const int la = addBone(m, left ? "lowerarm_l" : "lowerarm_r", ua, { side * 0.03f, -0.28f, -0.01f });
        const int hand = addBone(m, left ? "Hand_L" : "Hand_R", la, { 0, -0.25f, 0.03f });
        (left ? c.armL : c.armR) = ua;
        c.parts.push_back({ ua, la, 0.05f, glm::vec3(1.0f), glm::vec3(0.0f), shirt });
        c.parts.push_back({ la, hand, 0.04f, glm::vec3(1.0f), glm::vec3(0.0f), skin });
        c.parts.push_back({ hand, -1, 0.0f, glm::vec3(0.04f, 0.055f, 0.03f), glm::vec3(0.0f, -0.04f, 0.0f), skin });
    }
    for (int side : { 1, -1 }) {
        const bool left = side > 0;
        const int th = addBone(m, left ? "Thigh_L" : "Thigh_R", pelvis, { side * 0.1f, -0.05f, 0 });
        const int ca = addBone(m, left ? "calf_l" : "calf_r", th, { 0, -0.45f, 0.03f });
        const int ft = addBone(m, left ? "Foot_L" : "Foot_R", ca, { 0, -0.43f, -0.03f });
        c.legs.push_back({ th, ca, ft });
        c.parts.push_back({ th, ca, 0.075f, glm::vec3(1.0f), glm::vec3(0.0f), trousers });
        c.parts.push_back({ ca, ft, 0.055f, glm::vec3(1.0f), glm::vec3(0.0f), trousers });
        c.parts.push_back({ ft, -1, 0.0f, glm::vec3(0.05f, 0.035f, 0.11f), glm::vec3(0.0f, 0.0f, 0.05f), shoes });
    }
    c.parts.push_back({ pelvis, s3, 0.15f, glm::vec3(1.0f), glm::vec3(0.0f), shirt });
    c.parts.push_back({ s3, neck, 0.05f, glm::vec3(1.0f), glm::vec3(0.0f), skin });
    c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.095f, 0.115f, 0.105f), glm::vec3(0.0f, 0.08f, 0.01f), skin });
    c.parts.push_back({ head, -1, 0.0f, glm::vec3(0.1f, 0.06f, 0.11f), glm::vec3(0.0f, 0.15f, -0.01f), glm::vec3(0.25f, 0.16f, 0.08f) });
    c.body = pelvis;
    c.rest = kke::AnimationSet(m).restPose();
    c.gait = kke::ProceduralGait(kke::legsFromSkeleton(m, c.legs));
    c.look = kke::LookAt::humanoid(m);
    c.cruise = 1.3f;
    c.maxTurn = 200.0f;
    c.canRagdoll = true;
    c.mass = 70.0f;
    return c;
}

void ProceduralDemoModule::place(Creature& c, const glm::vec3& at, float yaw) {
    c.position = at;
    glm::vec3 hit, normal;
    if (ground(at + kUp * 3.0f, hit, normal)) c.position.y = hit.y;
    c.yaw = yaw;
    c.goal = c.position;
    const glm::mat4 body = glm::translate(glm::mat4(1.0f), c.position) * glm::mat4_cast(glm::angleAxis(glm::radians(yaw), kUp));
    c.gait.reset(body, [this](const glm::vec3& f, glm::vec3& h, glm::vec3& n) { return ground(f, h, n); });
    c.world = kke::poseToModel(c.rig, c.rest);
    for (glm::mat4& w : c.world) w = body * w;
}

// ------------------------------------------------------------- module

void ProceduralDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_orbit = app.getModule<kke::OrbitCameraModule>();
    buildGround();
    m_creatureMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    app.renderer().setClearColor(glm::vec3(0.62f, 0.76f, 0.9f)); // a clear sky

    m_creatures.push_back(makeSpider(8, "spider", 1.0f));
    m_creatures.push_back(makeSpider(6, "beetle", 1.0f));
    m_creatures.push_back(makeDog());
    m_creatures.push_back(makePerson());
    const glm::vec3 starts[] = { { -1.5f, 0, 1.0f }, { -0.5f, 0, 1.8f }, { 0.5f, 0, -1.5f }, { 1.5f, 0, 0.5f } };
    for (size_t i = 0; i < m_creatures.size(); ++i) place(m_creatures[i], starts[i], 90.0f * float(i));

    const std::string gait = envString("KKE_PROC_GAIT");
    if (!gait.empty()) m_dogGait = kke::gaitFromName(gait);
    for (int g = 0; g < 4; ++g)
        if (gaitOf(g) == m_dogGait) m_gaitIndex = g;
    const std::string focus = envString("KKE_PROC_FOCUS");
    for (size_t i = 0; i < m_creatures.size(); ++i)
        if (m_creatures[i].name == focus) m_focus = static_cast<int>(i);
    m_quitAfter = envFloat("KKE_PROC_QUIT", -1.0f);
    m_hitAt = envFloat("KKE_PROC_HIT", -1.0f);
    m_hitSpeed = envFloat("KKE_PROC_HIT_SPEED", 3.0f);
    m_trace = envFloat("KKE_PROC_TRACE", 0.0f) > 0.0f;
    if (m_orbit) {
        m_orbit->setControls(kke::OrbitCameraModule::Controls::Editor);
        m_orbit->setDistanceLimits(0.6f, 30.0f);
        m_orbit->setPitchLimits(-1.45f, -0.05f);
        float yaw = -35.0f, pitch = -25.0f, dist = 6.5f;
        const std::string view = envString("KKE_PROC_VIEW");
        if (!view.empty()) {
            std::stringstream s(view);
            char comma = 0;
            s >> yaw >> comma >> pitch >> comma >> dist;
        }
        m_orbit->setView(glm::vec3(0.5f, 0.3f, 0.0f), dist, glm::radians(pitch), glm::radians(yaw));
    }
    defineInput();
    buildPanel();
    kke::log::get(name())->info("{} creatures, no animation clips: click the ground to call them, click the dog or the person to hit",
                                m_creatures.size());
}

void ProceduralDemoModule::think(Creature& c, float dt) {
    c.time += dt;
    c.wander -= dt;
    c.flee -= dt;
    const size_t index = static_cast<size_t>(&c - m_creatures.data());
    const bool called = m_flagTime >= 0.0f && m_time - m_flagTime < 20.0f;
    if (c.flee > 0.0f) {
        // keep the goal picked when it was clicked
    } else if (called) {
        const float a = glm::two_pi<float>() * float(index) / float(m_creatures.size());
        c.goal = m_flag + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 0.7f;
    } else if (c.wander <= 0.0f || glm::length(glm::vec2(c.goal.x - c.position.x, c.goal.z - c.position.z)) < 0.5f) {
        std::uniform_real_distribution<float> spot(-9.0f, 9.0f), pause(3.0f, 7.0f);
        c.goal = glm::vec3(spot(m_rng), 0.0f, spot(m_rng));
        c.wander = pause(m_rng) + (c.kind == Kind::Dog ? 0.0f : 3.0f);
    }

    float cruise = c.cruise;
    if (c.kind == Kind::Dog) {
        // Walk, trot, gallop, round and round (or what 1/2/3 asked for).
        kke::Gait g = m_dogGait;
        if (g == kke::Gait::Auto) {
            const float t = std::fmod(c.time, 18.0f);
            g = t < 6.0f ? kke::Gait::Walk : t < 12.0f ? kke::Gait::Trot : kke::Gait::Gallop;
        }
        cruise = g == kke::Gait::Walk ? 1.1f : g == kke::Gait::Trot ? 2.6f : 5.5f;
        c.gait.settings().gait = m_dogGait;
    }
    if (c.flee > 0.0f) cruise *= 2.5f;

    const glm::vec3 to = c.goal - c.position;
    const float distance = glm::length(glm::vec2(to.x, to.z));
    float want = distance < 0.35f ? 0.0f : std::min(cruise, distance * 1.2f);
    const float turn = distance < 0.35f ? 0.0f : wrapDegrees(glm::degrees(std::atan2(to.x, to.z)) - c.yaw);
    c.turnRate = glm::clamp(turn * 4.0f, -c.maxTurn, c.maxTurn);
    if (std::abs(turn) > 60.0f) want *= 0.35f;
    const float accel = c.kind == Kind::Dog ? 5.0f : 2.5f;
    c.speed += glm::clamp(want - c.speed, -accel * dt, accel * dt);
}

void ProceduralDemoModule::move(Creature& c, float dt) {
    c.yaw = wrapDegrees(c.yaw + c.turnRate * dt);
    const glm::vec3 forward(std::sin(glm::radians(c.yaw)), 0.0f, std::cos(glm::radians(c.yaw)));
    c.position += forward * c.speed * dt;
    // Stay in the meadow.
    c.position.x = glm::clamp(c.position.x, -20.0f, 20.0f);
    c.position.z = glm::clamp(c.position.z, -20.0f, 20.0f);
    glm::vec3 hit, normal;
    if (ground(c.position + kUp * 1.5f, hit, normal)) c.position.y = hit.y;
}

kke::Pose ProceduralDemoModule::animatedPose(Creature& c, float dt, const glm::mat4& modelWorld) {
    kke::Pose pose = c.rest;
    kke::applyGait(c.rig, pose, c.legs, c.body, c.gait, modelWorld);
    if (c.armL >= 0 && c.armR >= 0) {
        // Arms swing against the legs, more the faster it goes.
        const float amount = glm::radians(30.0f) * std::min(1.0f, c.speed / 1.6f);
        const float s = std::sin(glm::two_pi<float>() * c.gait.phase());
        pose[c.armL].r = glm::normalize(pose[c.armL].r * glm::angleAxis(-s * amount, glm::vec3(1, 0, 0)));
        pose[c.armR].r = glm::normalize(pose[c.armR].r * glm::angleAxis(s * amount, glm::vec3(1, 0, 0)));
    }
    const glm::mat4 toModel = glm::inverse(modelWorld);
    if (c.tail.valid()) {
        // A wag, faster when the camera is close.
        const std::vector<glm::mat4> w = kke::poseToModel(c.rig, pose);
        const glm::vec3 rootPos = positionOf(w[c.tail.bones.front()]);
        const float near = glm::length(m_app->camera().position - c.position) < 4.0f ? 1.0f : 0.0f;
        const float wag = std::sin(c.time * (7.0f + 7.0f * near)) * (0.1f + 0.08f * near);
        const glm::vec3 target = rootPos + glm::vec3(wag, 0.12f, -0.2f);
        const glm::vec3 pole = rootPos + glm::vec3(0.0f, 0.4f, 0.0f);
        kke::solveFabrik(c.rig, pose, c.tail, target, &pole);
    }
    const glm::vec3 eye = glm::vec3(toModel * glm::vec4(m_app->camera().position, 1.0f));
    const bool watching = glm::length(m_app->camera().position - c.position) < 7.0f;
    c.look.apply(c.rig, pose, watching ? &eye : nullptr, dt);
    return pose;
}

void ProceduralDemoModule::animate(Creature& c, float dt) {
    const glm::mat4 body = glm::translate(glm::mat4(1.0f), c.position) * glm::mat4_cast(glm::angleAxis(glm::radians(c.yaw), kUp));
    const glm::vec3 forward(std::sin(glm::radians(c.yaw)), 0.0f, std::cos(glm::radians(c.yaw)));
    c.gait.update(body, forward * c.speed, c.turnRate, [this](const glm::vec3& f, glm::vec3& h, glm::vec3& n) { return ground(f, h, n); }, dt);
    const kke::Pose pose = animatedPose(c, dt, body);
    c.world = kke::poseToModel(c.rig, pose);
    for (glm::mat4& w : c.world) w = body * w;
}

void ProceduralDemoModule::hit(Creature& c, const glm::vec3& point, const glm::vec3& push) {
    if (!c.canRagdoll) {
        // Bugs run for it.
        c.flee = 2.5f;
        glm::vec3 away = c.position - m_app->camera().position;
        away.y = 0.0f;
        c.goal = c.position + (glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(1, 0, 0)) * 4.0f;
        return;
    }
    if (!c.active.physical()) {
        std::string missing;
        c.ragdoll = c.kind == Kind::Dog ? kke::buildQuadrupedRagdoll(c.rig, c.world, c.mass, &missing)
                                        : kke::buildHumanoidRagdoll(c.rig, c.world, c.mass, &missing);
        if (c.ragdoll.bodies.empty()) {
            kke::log::get(name())->warn("{}: no ragdoll (missing bone '{}')", c.name, missing);
            return;
        }
        c.binding = kke::bindSkeletonToRagdoll(c.rig, c.world, c.ragdoll);
        const glm::vec3 forward(std::sin(glm::radians(c.yaw)), 0.0f, std::cos(glm::radians(c.yaw)));
        c.handle = m_rigid->createRagdoll(c.ragdoll, forward * c.speed);
        if (!c.handle) return;
        c.active = kke::ActiveRagdoll(c.ragdoll);
        c.active.setTargets(c.binding, c.world);
        c.speed = 0.0f;
        c.turnRate = 0.0f;
    }
    int nearest = 0;
    float best = 1e9f;
    std::vector<glm::mat4> bodies;
    m_rigid->ragdollBodyTransforms(c.handle, bodies);
    for (size_t b = 0; b < bodies.size(); ++b) {
        const float d = glm::length(positionOf(bodies[b]) - point);
        if (d < best) best = d, nearest = static_cast<int>(b);
    }
    c.active.hit(nearest, push);
    m_rigid->pushRagdollBody(c.handle, nearest, push);
    const int pelvis = c.ragdoll.findBody("pelvis");
    if (pelvis >= 0 && pelvis != nearest) m_rigid->pushRagdollBody(c.handle, pelvis, push * 0.3f);
    kke::log::get(name())->info("{} hit at {:.1f} m/s", c.name, glm::length(push));
}

void ProceduralDemoModule::updatePhysical(Creature& c, float dt) {
    c.time += dt;
    std::vector<glm::mat4> bodies;
    if (!m_rigid->ragdollBodyTransforms(c.handle, bodies)) {
        c.handle = 0;
        c.active = kke::ActiveRagdoll();
        return;
    }
    // Staggering, the muscles pull back toward where it stood; once down,
    // it gets up wherever it ended up.
    const int pelvis = std::max(0, c.ragdoll.findBody("pelvis"));
    const glm::vec3 p = positionOf(bodies[static_cast<size_t>(pelvis)]);
    if (c.active.state() != kke::ActiveRagdoll::State::Active) {
        const float k = 1.0f - std::exp(-6.0f * dt);
        c.position.x += (p.x - c.position.x) * k;
        c.position.z += (p.z - c.position.z) * k;
    }
    glm::vec3 hit, normal;
    if (ground(glm::vec3(c.position.x, c.position.y + 1.5f, c.position.z), hit, normal)) c.position.y = hit.y;
    const glm::mat4 body = glm::translate(glm::mat4(1.0f), c.position) * glm::mat4_cast(glm::angleAxis(glm::radians(c.yaw), kUp));
    // What the muscles aim for: standing where the body is now.
    c.gait.reset(body, [this](const glm::vec3& f, glm::vec3& h, glm::vec3& n) { return ground(f, h, n); });
    const kke::Pose pose = animatedPose(c, dt, body);
    std::vector<glm::mat4> animWorld = kke::poseToModel(c.rig, pose);
    for (glm::mat4& w : animWorld) w = body * w;

    c.active.setTargets(c.binding, animWorld);
    if (m_trace) {
        const auto& t = c.active.targets();
        const int torso = std::max(0, c.ragdoll.findBody(c.kind == Kind::Dog ? "chest" : "torso"));
        const glm::vec3 now = positionOf(bodies[size_t(torso)]) - p, want = positionOf(t[size_t(torso)]) - positionOf(t[size_t(pelvis)]);
        float weakest = 1.0f;
        for (size_t j = 0; j < c.ragdoll.joints.size(); ++j) weakest = std::min(weakest, c.active.jointStrength(int(j)));
        kke::log::get(name())->info("{}: balance {:.2f}, weakest joint {:.2f}, lean {:.0f} deg from the pose, pelvis {:.2f} m (wants {:.2f})", c.name,
                                    c.active.balance(), weakest,
                                    glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(now), glm::normalize(want)), -1.0f, 1.0f))), p.y,
                                    positionOf(t[size_t(pelvis)]).y);
    }
    c.active.update(dt, bodies);
    if (c.active.physical()) {
        const kke::RagdollDrive drive = c.active.drive();
        if (!m_rigid->driveRagdoll(c.handle, drive)) kke::log::get(name())->warn("{}: the physics refused the ragdoll drive", c.name);
        const std::vector<glm::mat4> ragWorld = kke::poseFromRagdoll(c.rig, c.binding, bodies, glm::mat4(1.0f));
        c.world = c.active.state() == kke::ActiveRagdoll::State::GettingUp ? kke::blendPoses(ragWorld, animWorld, c.active.getUpBlend())
                                                                             : ragWorld;
    } else {
        m_rigid->destroyRagdoll(c.handle);
        c.handle = 0;
        c.world = animWorld;
        c.gait.reset(body, [this](const glm::vec3& f, glm::vec3& h, glm::vec3& n) { return ground(f, h, n); });
    }
}

void ProceduralDemoModule::update(const kke::UpdateContext& ctx) {
    readInput();
    const float dt = std::min(ctx.dt, 1.0f / 20.0f);
    m_time += dt;
    for (Creature& c : m_creatures) {
        if (c.handle) {
            updatePhysical(c, dt);
            continue;
        }
        think(c, dt);
        move(c, dt);
        animate(c, dt);
    }
    if (m_hitAt >= 0.0f && !m_hitDone && m_time >= m_hitAt) {
        m_hitDone = true;
        // The focused creature if it can be knocked about, else the person.
        Creature* target = m_focus >= 0 && m_creatures[static_cast<size_t>(m_focus)].canRagdoll ? &m_creatures[static_cast<size_t>(m_focus)] : nullptr;
        for (Creature& c : m_creatures)
            if (!target && c.kind == Kind::Person) target = &c;
        if (target) {
            const glm::vec3 center = positionOf(target->world[static_cast<size_t>(target->body)]) + kUp * 0.1f;
            const glm::vec3 side(std::cos(glm::radians(target->yaw)), 0.0f, -std::sin(glm::radians(target->yaw)));
            hit(*target, center, side * m_hitSpeed + kUp * 0.5f);
        }
    }
    if (m_focus >= 0 && m_orbit) m_orbit->setTarget(m_creatures[static_cast<size_t>(m_focus)].position + kUp * 0.3f);

    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    for (const Creature& c : m_creatures) appendCreature(c, v, idx);
    if (m_flagTime >= 0.0f && m_time - m_flagTime < 20.0f) {
        appendCapsule(v, idx, m_flag, m_flag + kUp * 0.7f, 0.015f, glm::vec3(0.4f, 0.3f, 0.2f));
        appendEllipsoid(v, idx, glm::scale(glm::translate(glm::mat4(1.0f), m_flag + glm::vec3(0.12f, 0.6f, 0.0f)), glm::vec3(0.12f, 0.08f, 0.01f)),
                        glm::vec3(0.9f, 0.2f, 0.15f));
    }
    m_creatureMesh->upload(v, idx);

    if (m_quitAfter > 0.0f) {
        m_logTimer += dt;
        if (m_logTimer >= 2.0f) {
            m_logTimer = 0.0f;
            std::string line;
            for (const Creature& c : m_creatures) {
                const char* state = !c.handle ? "animated"
                                    : c.active.state() == kke::ActiveRagdoll::State::Active ? "staggering"
                                    : c.active.state() == kke::ActiveRagdoll::State::Fallen ? "down"
                                                                                               : "getting up";
                line += fmt::format("{}: {} {:.1f} m/s {} | ", c.name, kke::gaitName(c.gait.gait()), c.speed, state);
            }
            kke::log::get(name())->info("{:.0f} s: {}", m_time, line);
        }
        if (m_time >= m_quitAfter) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void ProceduralDemoModule::appendCreature(const Creature& c, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) const {
    if (c.world.size() != c.rig.bones.size()) return;
    for (const Part& p : c.parts) {
        const glm::mat4& a = c.world[static_cast<size_t>(p.a)];
        if (p.b >= 0) {
            appendCapsule(v, idx, positionOf(a), positionOf(c.world[static_cast<size_t>(p.b)]), p.radius, p.color);
        } else {
            const glm::mat4 m = glm::scale(glm::translate(a, p.offset), p.scale);
            appendEllipsoid(v, idx, m, p.color);
        }
    }
}

void ProceduralDemoModule::render(const kke::RenderContext& ctx) {
    m_view = ctx.view;
    m_proj = ctx.proj;
    m_groundMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    m_creatureMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.55f);
}

void ProceduralDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_groundMesh->drawShadow(ctx);
    m_creatureMesh->drawShadow(ctx);
}

void ProceduralDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    // A controller aims with the middle of the screen (the panel's
    // crosshair); the left stick moves the view there.
    auto action = [&](const char* id, const char* label, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Creatures" });
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("proc.call", "Call everyone to the middle of the screen", SDL_GAMEPAD_BUTTON_SOUTH);
    action("proc.hit", "Hit what's in the middle of the screen", SDL_GAMEPAD_BUTTON_WEST);
    action("proc.hard", "Hit it hard", SDL_GAMEPAD_BUTTON_NORTH);
    m.defineAction({ "proc.gait", "Dog: next gait", "Creatures" });
    m.addBinding(IM::bind("proc.gait", IM::key(SDL_SCANCODE_G)));
    m.addBinding(IM::bind("proc.gait", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
    in->commitDefaults();
}

void ProceduralDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    int w = 0, h = 0;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    const float cx = float(w) * 0.5f, cy = float(h) * 0.5f;
    if (m.pressed("proc.call")) click(cx, cy, false, true);
    if (m.pressed("proc.hit")) click(cx, cy, false, false);
    if (m.pressed("proc.hard")) click(cx, cy, true, false);
    if (m.pressed("proc.gait")) {
        m_gaitIndex = (m_gaitIndex + 1) % 4;
        m_dogGait = gaitOf(m_gaitIndex);
    }
}

// The panel (RmlUi, kke::DemoPanelModule) replaces the old ImGui HUD.
void ProceduralDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Procedural animation");
    s.note("No animation clips: every step is planned.");
    s.hint("{mouse:left} on the ground calls everyone; on the dog or the person hits them (Shift: hard); on a bug makes it run. "
           "{mouse:right} drag turns the view. 0-3 or {proc.gait} dog gait",
           "{proc.call} call everyone to the crosshair  {proc.hit} hit  {proc.hard} hit hard  {proc.gait} dog gait  "
           "{camera.pan} move  {camera.orbit} turn  {camera.zoom} zoom");
    s.choice("Dog gait", &m_gaitIndex, { "Auto", "Walk", "Trot", "Gallop" }, [this] { m_dogGait = gaitOf(m_gaitIndex); });
    s.slider("Hit strength", &m_hitSpeed, 1.0f, 9.0f, "%.1f m/s", {}, 0.5f);
    for (size_t i = 0; i < m_creatures.size(); ++i) {
        s.text([this, i] {
            const Creature& c = m_creatures[i];
            const char* state = !c.handle ? "" : c.active.state() == kke::ActiveRagdoll::State::Active ? " (staggering)"
                                             : c.active.state() == kke::ActiveRagdoll::State::Fallen ? " (down)"
                                                                                                      : " (getting up)";
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s: %s, %.1f m/s, looking %+.0f deg%s", c.name.c_str(), kke::gaitName(c.gait.gait()),
                          static_cast<double>(c.speed), static_cast<double>(c.look.yawDegrees()), state);
            return std::string(buf);
        });
    }
}

kke::Gait ProceduralDemoModule::gaitOf(int index) {
    const kke::Gait gaits[] = { kke::Gait::Auto, kke::Gait::Walk, kke::Gait::Trot, kke::Gait::Gallop };
    return gaits[std::clamp(index, 0, 3)];
}

void ProceduralDemoModule::click(float mouseX, float mouseY, bool hard, bool groundOnly) {
    int w = 0, h = 0;
    if (!SDL_GetWindowSize(m_app->window().handle(), &w, &h) || w <= 0 || h <= 0) return;
    // Vulkan clip space: y down, depth 0..1 (the projection already flips y).
    const glm::vec2 ndc(2.0f * mouseX / float(w) - 1.0f, 2.0f * mouseY / float(h) - 1.0f);
    const glm::mat4 inv = glm::inverse(m_proj * m_view);
    glm::vec4 nearP = inv * glm::vec4(ndc, 0.0f, 1.0f), farP = inv * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec3 origin = glm::vec3(nearP) / nearP.w;
    const glm::vec3 dir = glm::normalize(glm::vec3(farP) / farP.w - origin);

    // A creature under the cursor: the closest one the ray passes near.
    Creature* hitCreature = nullptr;
    glm::vec3 point(0.0f);
    float bestT = 1e9f;
    for (Creature& c : m_creatures) {
        if (groundOnly) break; // the pad's "call": never a hit
        if (c.world.size() != c.rig.bones.size()) continue;
        const glm::vec3 center = positionOf(c.world[static_cast<size_t>(c.body)]);
        const float radius = c.kind == Kind::Person ? 0.45f : c.kind == Kind::Dog ? 0.35f : 0.2f;
        const float t = glm::dot(center - origin, dir);
        if (t <= 0.0f || t >= bestT) continue;
        if (glm::length(origin + dir * t - center) < radius) {
            hitCreature = &c;
            bestT = t;
            point = origin + dir * t;
        }
    }
    if (hitCreature) {
        glm::vec3 push = dir;
        push.y = 0.0f;
        push = (glm::length(push) > 1e-3f ? glm::normalize(push) : glm::vec3(1, 0, 0)) * (hard ? 9.0f : m_hitSpeed) + kUp * 0.5f;
        hit(*hitCreature, point, push);
        return;
    }
    const kke::RigidWorld::RayHit g = m_rigid->world().raycast(origin, dir, 200.0f);
    if (g.hit) {
        m_flag = g.point;
        m_flagTime = m_time;
    }
}

void ProceduralDemoModule::onEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        if ((ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) || m_app->uiCapturesMouse()) return;
        click(event.button.x, event.button.y, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0, false);
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        switch (event.key.key) {
        case SDLK_0: m_gaitIndex = 0; break;
        case SDLK_1: m_gaitIndex = 1; break;
        case SDLK_2: m_gaitIndex = 2; break;
        case SDLK_3: m_gaitIndex = 3; break;
        default: return;
        }
        m_dogGait = gaitOf(m_gaitIndex);
    }
}

} // namespace procedural_demo
