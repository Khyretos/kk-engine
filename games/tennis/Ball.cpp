#include "Ball.h"

#include "kke/PhysicsBridge.h"
#include "kke/modules/PhysicsModule.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace tennis {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSeamHalf = 0.065f; // the seam's half width, radians (about 3 mm on a real ball)
constexpr int kLookLevel = 5;       // icosphere subdivisions: 20480 triangles, the seam a clean line

// The seam of a tennis ball: two lobes that meet in an S, a closed curve
// splitting the ball into two equal halves (a + b = 1 keeps it on the
// unit sphere).
glm::vec3 seamAt(float t) {
    constexpr float a = 0.7f, b = 0.3f;
    return { a * std::cos(t) + b * std::cos(3.0f * t), a * std::sin(t) - b * std::sin(3.0f * t), 2.0f * std::sqrt(a * b) * std::sin(2.0f * t) };
}

// The FEMFX body's tets (the physics demo's rubber ball, tennis ball sized).
const kke::TetMeshData& tetMesh() {
    static const kke::TetMeshData mesh = kke::PhysicsModule::buildSphere(4, kBallRadius);
    return mesh;
}

// The angle from the seam to the unit direction `d`.
float seamAngle(const glm::vec3& d) {
    constexpr int kSteps = 720;
    float best = -1.0f;
    for (int i = 0; i < kSteps; ++i) best = std::max(best, glm::dot(d, seamAt(2.0f * kPi * static_cast<float>(i) / kSteps)));
    return std::acos(std::clamp(best, -1.0f, 1.0f));
}

// Felt over rubber: soft enough to flatten visibly on a hard hit and
// wobble back. Stiffer (6e4) or finer (5 cells) and FEMFX's solver
// loses the steering and starts to jitter on a ball this small.
kke::Material ballMaterial() {
    kke::Material m;
    m.density = 400.0f;
    m.stiffness = 1.0e4f;
    m.poissonsRatio = 0.45f;
    m.fractureStressThreshold = 1.0e9f; // never breaks
    m.plasticYieldThreshold = 1.0e9f;   // never dents for good
    m.plasticCreep = 0.0f;
    m.roughness = 0.95f;
    return m;
}

glm::quat yawQuat(float degrees) { return glm::angleAxis(glm::radians(degrees), glm::vec3(0, 1, 0)); }

} // namespace

Ball::Ball(kke::PhysicsModule& physics, const CourtPlace& place) : m_physics(physics), m_place(place) {
    const kke::TetMeshData& mesh = tetMesh();
    kke::PhysicsModule::TetSpawnOptions o;
    o.drawOnlyCracks = true; // never drawn: appendLook draws it (it never cracks)
    const glm::vec3 start = m_place.toWorld({ 0.0f, 1.0f, 0.0f });
    m_handle = m_physics.spawnTetMeshWithOptions(mesh, start, ballMaterial(), o);
    m_pos = { 0.0f, 1.0f, 0.0f };
}

namespace {
// The look's sphere, colours and how it's glued to the tets: the same for every ball.
struct Look {
    std::vector<glm::vec3> normals;     // rest: unit directions
    std::vector<glm::vec3> colours;
    std::vector<uint32_t> indices;
    kke::TetEmbedding embedding;
};

const Look& look() {
    static const Look l = [] {
        Look out;
        // An icosphere: an icosahedron, each triangle split in four kLookLevel times.
        const float g = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<glm::vec3> v = { { -1, g, 0 }, { 1, g, 0 }, { -1, -g, 0 }, { 1, -g, 0 }, { 0, -1, g }, { 0, 1, g },
                                     { 0, -1, -g }, { 0, 1, -g }, { g, 0, -1 }, { g, 0, 1 }, { -g, 0, -1 }, { -g, 0, 1 } };
        for (glm::vec3& p : v) p = glm::normalize(p);
        std::vector<uint32_t> tri = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                                      3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
        for (int level = 0; level < kLookLevel; ++level) {
            std::unordered_map<uint64_t, uint32_t> mid;
            auto middle = [&](uint32_t a, uint32_t b) {
                const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
                if (const auto it = mid.find(key); it != mid.end()) return it->second;
                v.push_back(glm::normalize(v[a] + v[b]));
                return mid[key] = static_cast<uint32_t>(v.size() - 1);
            };
            std::vector<uint32_t> next;
            next.reserve(tri.size() * 4);
            for (size_t t = 0; t < tri.size(); t += 3) {
                const uint32_t a = tri[t], b = tri[t + 1], c = tri[t + 2];
                const uint32_t ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
                next.insert(next.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
            }
            tri = std::move(next);
        }
        // Felt (optic yellow, a little darker in the groove beside the seam) and the white seam.
        const glm::vec3 felt(0.80f, 0.88f, 0.22f), seam(0.95f, 0.95f, 0.91f);
        out.colours.reserve(v.size());
        for (const glm::vec3& d : v) {
            const float a = seamAngle(d);
            const float w = std::clamp((kSeamHalf - a) / 0.012f, 0.0f, 1.0f);
            const float groove = 1.0f - 0.15f * std::max(0.0f, 1.0f - std::abs(a - kSeamHalf - 0.02f) / 0.03f);
            out.colours.push_back(glm::mix(felt * groove, seam, w));
        }
        out.normals = v;
        std::vector<glm::vec3> points(v.size());
        for (size_t i = 0; i < v.size(); ++i) points[i] = v[i] * kBallRadius;
        out.embedding = kke::embedPoints(tetMesh(), points);
        // Front faces wound like the engine's (cross(b - a, c - a) outward).
        for (size_t t = 0; t < tri.size(); t += 3)
            if (glm::dot(glm::cross(v[tri[t + 1]] - v[tri[t]], v[tri[t + 2]] - v[tri[t]]), v[tri[t]]) < 0.0f) std::swap(tri[t + 1], tri[t + 2]);
        out.indices = std::move(tri);
        return out;
    }();
    return l;
}
} // namespace

void Ball::appendLook(std::vector<kke::Vertex>& vertices, std::vector<uint32_t>& indices) const {
    const Look& l = look();
    if (!m_handle || !m_physics.deformEmbedded(m_handle, l.embedding, l.normals, m_lookPos, m_lookNormal) || m_lookPos.size() != l.normals.size()) return;
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    for (size_t i = 0; i < m_lookPos.size(); ++i) vertices.push_back({ m_lookPos[i], l.colours[i], m_lookNormal[i], { 0.0f, 0.0f } });
    for (uint32_t k : l.indices) indices.push_back(base + k);
}

Ball::~Ball() {
    if (m_handle) m_physics.removeObject(m_handle);
}

void Ball::courtBoxes(const CourtPlace& place, uint64_t keyBase, std::vector<kke::BridgeBox>& out) {
    const glm::quat q = yawQuat(place.yawDegrees);
    auto add = [&](const glm::vec3& centre, const glm::vec3& half) {
        kke::BridgeBox b;
        b.key = keyBase + out.size();
        b.center = place.toWorld(centre);
        b.rotation = q;
        b.halfExtents = half;
        out.push_back(b);
    };
    // Walls just outside the fence, up to the roof; the roof over them.
    constexpr float t = 1.0f; // half thickness: nothing slips through between two steps
    const float h = kLidHeight * 0.5f;
    add({ kFenceHalfX + t, h, 0.0f }, { t, h, kFenceHalfZ + 2.0f * t });
    add({ -kFenceHalfX - t, h, 0.0f }, { t, h, kFenceHalfZ + 2.0f * t });
    add({ 0.0f, h, kFenceHalfZ + t }, { kFenceHalfX + 2.0f * t, h, t });
    add({ 0.0f, h, -kFenceHalfZ - t }, { kFenceHalfX + 2.0f * t, h, t });
    add({ 0.0f, kLidHeight + t, 0.0f }, { kFenceHalfX + 2.0f * t, t, kFenceHalfZ + 2.0f * t });
    // The net: a few steps following its sag, 3 cm thick.
    constexpr int kNetSteps = 7;
    for (int i = 0; i < kNetSteps; ++i) {
        const float x0 = -kPostX + 2.0f * kPostX * static_cast<float>(i) / kNetSteps;
        const float x1 = -kPostX + 2.0f * kPostX * static_cast<float>(i + 1) / kNetSteps;
        const float top = netHeight(0.5f * (x0 + x1));
        add({ 0.5f * (x0 + x1), top * 0.5f, 0.0f }, { 0.5f * (x1 - x0), top * 0.5f, 0.015f });
    }
}

glm::vec3 Ball::bodyPosition() const {
    glm::vec3 c, v;
    if (!m_handle || !m_physics.objectMotion(m_handle, c, v)) return m_pos;
    return m_place.toLocal(c);
}

void Ball::place(const glm::vec3& local, const glm::vec3& velocity) {
    m_pos = local;
    m_vel = velocity;
    m_pull = 0.0f;
    m_rolling = false;
    if (m_handle) m_physics.resetObject(m_handle, m_place.toWorld(local), m_place.dirToWorld(velocity));
}

void Ball::strike(const glm::vec3& velocity, const glm::vec3& spin, float pull, float squash) {
    m_vel = velocity;
    m_pull = pull;
    m_rolling = false;
    if (!m_handle) return;
    const glm::vec3 wv = m_place.dirToWorld(velocity), ws = m_place.dirToWorld(spin);
    glm::vec3 centre, bodyVel;
    if (!m_physics.objectMotion(m_handle, centre, bodyVel)) return;
    // The body jumps to the flight if it lagged, then every vertex gets the
    // new velocity, turning about the centre (spin), and squeezed along
    // the hit (the strings flatten it; FEMFX springs it back).
    const glm::vec3 want = m_place.toWorld(m_pos);
    if (glm::length(want - centre) > 0.02f) {
        m_physics.translateObject(m_handle, want - centre);
        centre = want;
    }
    const float speed = glm::length(wv);
    const glm::vec3 n = speed > 1e-4f ? wv / speed : glm::vec3(0, 1, 0);
    const float squeeze = std::clamp(squash, 0.0f, 1.0f) * 1.2f;
    m_physics.changeVertexVelocities(m_handle, [&](const glm::vec3& p, const glm::vec3&) {
        const glm::vec3 r = p - centre;
        return wv + glm::cross(ws, r) - n * (squeeze * glm::dot(r, n) / kBallRadius);
    });
}

void Ball::follow(const Flight& target, bool rolling) {
    if (glm::length(target.pos - m_pos) < 0.01f && glm::length(target.vel - m_vel) < 0.05f) return;
    m_pos = target.pos;
    m_vel = target.vel;
    m_pull = target.gravity - kGravity;
    m_rolling = rolling;
}

void Ball::step(float dt) {
    const float r = kBallRadius;
    // Small steps, so a 60 m/s serve can't pass the net tape (3 cm) or the
    // fence between two of them.
    const int n = std::clamp(static_cast<int>(std::ceil(glm::length(m_vel) * dt / 0.02f)), 1, 16);
    const float h = dt / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const glm::vec3 was = m_pos;
        if (m_rolling) {
            // Along the court, slowing (felt on acrylic).
            const float s = glm::length(glm::vec2(m_vel.x, m_vel.z));
            const float keep = s > 1e-4f ? std::max(0.0f, s - 0.6f * h) / s : 0.0f;
            m_vel = glm::vec3(m_vel.x * keep, 0.0f, m_vel.z * keep);
            m_pos += m_vel * h;
        } else {
            const Flight f{ m_pos, m_vel, kGravity + m_pull };
            m_pos = f.at(h);
            m_vel = f.velocityAt(h);
        }

        // The court.
        if (!m_rolling && m_pos.y < r && m_vel.y < 0.0f) {
            const float impact = -m_vel.y;
            m_pos.y = r;
            m_vel = glm::vec3(m_vel.x * m_bounce.keepAlong, impact * m_bounce.restitution, m_vel.z * m_bounce.keepAlong);
            m_pull *= kPullAfterBounce; // the court takes most of the spin's bite
            if (impact > 0.6f) m_events.push_back({ Event::Kind::Bounce, { m_pos.x, 0.0f, m_pos.z } });
            if (m_vel.y < 0.4f) m_rolling = true;
        }
        // The net: the tape and the mesh under it, post to post.
        if ((was.z < 0.0f) != (m_pos.z < 0.0f)) {
            const float t = was.z / (was.z - m_pos.z);
            const glm::vec3 at = was + (m_pos - was) * t;
            if (std::abs(at.x) < kPostX + r && at.y - r < netHeight(at.x)) {
                const bool tape = at.y > netHeight(at.x) - r;
                if (tape && m_vel.y > -2.0f) {
                    // Clipped the tape: it rolls over, much slower (a let on
                    // a serve, live in a rally).
                    m_vel = glm::vec3(m_vel.x * 0.5f, std::max(1.2f, std::abs(m_vel.y) * 0.4f), m_vel.z * 0.3f);
                    m_pos = at + glm::vec3(0.0f, r * 0.5f, 0.0f);
                } else {
                    // Into the mesh: it takes the ball and drops it on this side.
                    const float side = was.z < 0.0f ? -1.0f : 1.0f;
                    m_pos = glm::vec3(at.x, std::max(at.y, r), side * (r + 0.03f));
                    m_vel = glm::vec3(m_vel.x * 0.2f, std::min(m_vel.y, 0.0f) * 0.2f, -m_vel.z * 0.08f);
                    m_pull = 0.0f;
                    m_rolling = false;
                }
                m_events.push_back({ Event::Kind::Net, at });
            }
        }
        // The fence and the roof: the chain link takes most of it.
        bool out = false;
        if (std::abs(m_pos.x) > kFenceHalfX - r) {
            m_pos.x = std::copysign(kFenceHalfX - r, m_pos.x);
            m_vel = glm::vec3(-m_vel.x * 0.15f, m_vel.y * 0.4f, m_vel.z * 0.4f);
            out = true;
        }
        if (std::abs(m_pos.z) > kFenceHalfZ - r) {
            m_pos.z = std::copysign(kFenceHalfZ - r, m_pos.z);
            m_vel = glm::vec3(m_vel.x * 0.4f, m_vel.y * 0.4f, -m_vel.z * 0.15f);
            out = true;
        }
        if (m_pos.y > kLidHeight - r) {
            m_pos.y = kLidHeight - r;
            m_vel.y = -std::abs(m_vel.y) * 0.3f;
            out = true;
        }
        if (out) {
            m_pull = 0.0f;
            m_rolling = false;
            m_events.push_back({ Event::Kind::Out, m_pos });
        }
    }
    steerBody(dt);
}

void Ball::steerBody(float dt) {
    if (!m_handle) return;
    glm::vec3 c, v;
    if (!m_physics.objectMotion(m_handle, c, v)) return;
    const glm::vec3 pos = m_place.toLocal(c), vel = m_place.dirToLocal(v);
    const glm::vec3 err = m_pos - pos;
    if (glm::length(err) > 0.6f) {
        // Too far behind (a place, a teleport, a snag): jump there.
        m_physics.translateObject(m_handle, m_place.dirToWorld(err));
        m_physics.changeVertexVelocities(m_handle, [&](const glm::vec3&, const glm::vec3& vv) { return vv - v + m_place.dirToWorld(m_vel); });
        return;
    }
    // Every vertex gets the same push: the flight's velocity plus a pull
    // onto its path. Only the whole ball moves; the squash and the wobble
    // (each vertex against the others) stay FEMFX's own.
    const float blend = 1.0f - std::exp(-25.0f * dt);
    const glm::vec3 want = m_vel + err * 20.0f;
    const glm::vec3 dv = m_place.dirToWorld((want - vel) * blend);
    m_physics.changeVertexVelocities(m_handle, [&](const glm::vec3&, const glm::vec3& vv) { return vv + dv; });
}

std::vector<Ball::Event> Ball::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

} // namespace tennis
