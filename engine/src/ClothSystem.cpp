#include "ClothSystem.h"

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>

namespace kke::detail {

namespace {

constexpr float kAirDensity = 1.225f; // kg/m^3
constexpr float kAssumedStep = 1.0f / 60.0f; // RigidWorld's collision step length
constexpr uint8_t kTangledSteps = 16;         // Cloth::undoneStreak at which a vertex is let go
constexpr int kMaxPasses = 4;                 // protection narrow-phase passes per step, at most

JPH::Vec3 toJ(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 toG(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
JPH::Mat44 toJ(const glm::mat4& m) {
    return JPH::Mat44(JPH::Vec4(m[0][0], m[0][1], m[0][2], m[0][3]), JPH::Vec4(m[1][0], m[1][1], m[1][2], m[1][3]),
                      JPH::Vec4(m[2][0], m[2][1], m[2][2], m[2][3]), JPH::Vec4(m[3][0], m[3][1], m[3][2], m[3][3]));
}
glm::mat4 toG(const JPH::RMat44& m) {
    glm::mat4 out(1.0f);
    for (int c = 0; c < 3; ++c) out[c] = glm::vec4(toG(m.GetColumn3(c)), 0.0f);
    const JPH::RVec3 t = m.GetTranslation();
    out[3] = glm::vec4(float(t.GetX()), float(t.GetY()), float(t.GetZ()), 1.0f);
    return out;
}

JPH::SoftBodyMotionProperties* softOf(JPH::Body& b) { return static_cast<JPH::SoftBodyMotionProperties*>(b.GetMotionProperties()); }

// Barycentric coordinates of p's projection onto triangle abc.
glm::vec3 barycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    if (std::fabs(den) < 1e-20f) return glm::vec3(-1.0f);
    const float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
    return glm::vec3(1.0f - v - w, v, w);
}

bool inside(const glm::vec3& bc, float margin) { return bc.x >= -margin && bc.y >= -margin && bc.z >= -margin; }

// Closest points of segments p0p1 and q0q1: p0 + s (p1 - p0) and q0 + t (q1 - q0)
// (Ericson, Real-Time Collision Detection 5.1.9).
void closestSegments(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& q0, const glm::vec3& q1, float& s, float& t) {
    const glm::vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    s = t = 0.0f;
    if (a <= 1e-12f && e <= 1e-12f) return;
    if (a <= 1e-12f) {
        t = std::clamp(f / e, 0.0f, 1.0f);
        return;
    }
    const float c = glm::dot(d1, r);
    if (e <= 1e-12f) {
        s = std::clamp(-c / a, 0.0f, 1.0f);
        return;
    }
    const float b = glm::dot(d1, d2), den = a * e - b * b;
    s = den > 1e-12f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
    t = (b * s + f) / e;
    if (t < 0.0f) {
        t = 0.0f;
        s = std::clamp(-c / a, 0.0f, 1.0f);
    } else if (t > 1.0f) {
        t = 1.0f;
        s = std::clamp((b - c) / a, 0.0f, 1.0f);
    }
}

// Cells of the protection pass's hash covering box lo..hi, or false when
// the box is not finite or spans more than maxCells (flung across the
// room in one step: nothing sane to test).
bool cellBox(const glm::vec3& lo, const glm::vec3& hi, float inv, int maxCells, glm::ivec3& a, glm::ivec3& b) {
    if (!std::isfinite(lo.x + lo.y + lo.z + hi.x + hi.y + hi.z)) return false;
    auto cellOf = [inv](float v) { return int(std::clamp(std::floor(v * inv), -1.0e6f, 1.0e6f)); };
    a = glm::ivec3(cellOf(lo.x), cellOf(lo.y), cellOf(lo.z));
    b = glm::ivec3(cellOf(hi.x), cellOf(hi.y), cellOf(hi.z));
    const glm::ivec3 span = b - a + 1;
    return span.x <= maxCells && span.y <= maxCells && span.z <= maxCells && span.x * span.y * span.z <= maxCells;
}

uint32_t hashCell(int x, int y, int z) { return uint32_t(x) * 92837111u ^ uint32_t(y) * 689287499u ^ uint32_t(z) * 283923481u; }

} // namespace

ClothSystem::ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp, JPH::JobSystem* jobs)
    : m_system(system), m_layer(layer), m_temp(temp), m_jobs(jobs) {
    m_system.AddStepListener(this);
    m_system.SetSoftBodyContactListener(this);
}

ClothSystem::~ClothSystem() {
    m_system.RemoveStepListener(this);
    m_system.SetSoftBodyContactListener(nullptr);
    JPH::BodyInterface& bi = m_system.GetBodyInterface();
    for (auto& [id, c] : m_cloths) {
        bi.RemoveBody(c.body);
        bi.DestroyBody(c.body);
    }
}

uint32_t ClothSystem::add(const ClothDesc& d) {
    const ClothMesh& mesh = d.mesh;
    const size_t n = mesh.positions.size();
    if (n < 2) return 0;
    Cloth c;
    c.level = d.protection;
    c.fabric = d.fabric;
    c.wind = std::max(0.0f, d.wind);
    c.rest = mesh.positions;
    c.tris = mesh.indices;
    c.bindPose = d.bindPose.empty() ? std::vector<glm::mat4>{ glm::mat4(1.0f) } : d.bindPose;

    glm::vec3 centroid(0.0f);
    for (const glm::vec3& p : mesh.positions) centroid += p;
    centroid /= float(n);

    // Lumped masses: a third of each adjacent triangle's area (threads of
    // a net: half of each thread's length times a nominal width).
    std::vector<float> mass(n, 0.0f);
    c.restArea.resize(c.tris.size() / 3);
    double edgeSum = 0.0;
    size_t edgeCount = 0;
    for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
        const glm::vec3 &a = c.rest[c.tris[t]], &b = c.rest[c.tris[t + 1]], &e = c.rest[c.tris[t + 2]];
        const float area = 0.5f * glm::length(glm::cross(b - a, e - a));
        c.restArea[t / 3] = area;
        for (int k = 0; k < 3; ++k) mass[c.tris[t + size_t(k)]] += area * d.fabric.density / 3.0f;
        edgeSum += double(glm::length(b - a) + glm::length(e - b) + glm::length(a - e));
        edgeCount += 3;
    }
    for (size_t i = 0; i + 1 < mesh.lines.size(); i += 2) {
        const float len = glm::length(c.rest[mesh.lines[i]] - c.rest[mesh.lines[i + 1]]);
        edgeSum += double(len);
        ++edgeCount;
        if (c.tris.empty())
            for (int k = 0; k < 2; ++k) mass[mesh.lines[i + size_t(k)]] += 0.5f * len * 0.01f * d.fabric.density;
    }
    c.meanEdge = edgeCount ? float(edgeSum / double(edgeCount)) : 0.05f;
    float meanMass = 0.0f;
    for (float& m : mass) {
        m = std::max(m, 1e-6f);
        meanMass += m;
    }
    meanMass /= float(n);
    if (d.contactMass > 0.0f) c.contactInvMassScale = std::min(1.0f, meanMass / d.contactMass);
    // Thicker than half an edge and neighbouring vertices would be pushed
    // apart by the self-collision pass even when the cloth lies flat.
    c.thickness = std::min(d.fabric.thickness, 0.45f * c.meanEdge);

    std::vector<bool> pinned(n, false);
    for (uint32_t p : d.pinned)
        if (p < n) pinned[p] = true;

    JPH::Ref<JPH::SoftBodySharedSettings> s = new JPH::SoftBodySharedSettings;
    s->mVertices.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 local = mesh.positions[i] - centroid;
        s->mVertices[i] = JPH::SoftBodySharedSettings::Vertex(JPH::Float3(local.x, local.y, local.z), JPH::Float3(0, 0, 0),
                                                               pinned[i] ? 0.0f : 1.0f / mass[i]);
    }
    for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
        JPH::SoftBodySharedSettings::Face f(c.tris[t], c.tris[t + 1], c.tris[t + 2]);
        if (!f.IsDegenerate()) s->AddFace(f);
    }
    // Softness -> XPBD compliance: alpha = softness * w * dt^2, so one
    // sub-step fixes 2 / (2 + softness) of the error (see Fabric).
    const float sub = kAssumedStep / float(std::max(1, d.fabric.iterations));
    const float scale = sub * sub / meanMass;
    const bool tethers = d.protection != ClothProtection::Off && !d.pinned.empty();
    JPH::SoftBodySharedSettings::VertexAttributes attr(d.fabric.stretch * scale, d.fabric.shear * scale, d.fabric.bend * scale,
                                                       tethers ? JPH::SoftBodySharedSettings::ELRAType::GeodesicDistance
                                                               : JPH::SoftBodySharedSettings::ELRAType::None,
                                                       std::max(1.0f, d.fabric.maxStretch));
    s->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
    // Threads with no triangle (a net's loose ends): plain edges.
    if (c.tris.empty()) {
        for (size_t i = 0; i + 1 < mesh.lines.size(); i += 2)
            s->mEdgeConstraints.emplace_back(mesh.lines[i], mesh.lines[i + 1], d.fabric.stretch * scale);
        s->CalculateEdgeLengths();
    }

    // Skinning: pins follow joint pinJoint; skinned vertices their weights.
    const bool anySkin = std::any_of(d.skin.begin(), d.skin.end(), [](const ClothDesc::SkinVertex& v) { return v.maxDistance >= 0.0f; });
    if (!d.pinned.empty() || anySkin) {
        const glm::mat4 toLocal = glm::translate(glm::mat4(1.0f), -centroid);
        for (size_t j = 0; j < c.bindPose.size(); ++j)
            s->mInvBindMatrices.emplace_back(uint32_t(j), toJ(glm::inverse(toLocal * c.bindPose[j])));
        const float backStop = d.protection == ClothProtection::Off ? FLT_MAX : d.backStop;
        for (uint32_t i = 0; i < n; ++i) {
            JPH::SoftBodySharedSettings::Skinned sk;
            sk.mVertex = i;
            if (pinned[i]) {
                sk.mMaxDistance = 0.0f;
                sk.mWeights[0] = JPH::SoftBodySharedSettings::SkinWeight(std::min<uint32_t>(d.pinJoint, uint32_t(c.bindPose.size() - 1)), 1.0f);
            } else if (i < d.skin.size() && d.skin[i].maxDistance >= 0.0f) {
                const ClothDesc::SkinVertex& v = d.skin[i];
                sk.mMaxDistance = v.maxDistance;
                sk.mBackStopDistance = backStop;
                sk.mBackStopRadius = 0.5f;
                for (int k = 0; k < 4; ++k)
                    if (v.weights[k] > 0.0f && v.joints[k] < c.bindPose.size())
                        sk.mWeights[k] = JPH::SoftBodySharedSettings::SkinWeight(v.joints[k], v.weights[k]);
                sk.NormalizeWeights();
            } else {
                continue;
            }
            s->mSkinnedConstraints.push_back(sk);
        }
        s->CalculateSkinnedConstraintNormals();
        c.skinned = !s->mSkinnedConstraints.empty();
    }
    s->Optimize();

    JPH::SoftBodyCreationSettings cs(s, JPH::RVec3(centroid.x, centroid.y, centroid.z), JPH::Quat::sIdentity(), m_layer);
    cs.mNumIterations = uint32_t(std::max(1, d.fabric.iterations));
    cs.mLinearDamping = d.fabric.damping;
    cs.mFriction = d.fabric.friction;
    cs.mGravityFactor = d.gravity;
    cs.mVertexRadius = d.protection == ClothProtection::Off ? 0.0f : c.thickness;
    cs.mFacesDoubleSided = true;
    const uint32_t id = m_next++;
    cs.mUserData = id; // OnSoftBodyContactValidate finds the cloth by it
    cs.mAllowSleeping = true;
    JPH::BodyInterface& bi = m_system.GetBodyInterface();
    c.body = bi.CreateAndAddSoftBody(cs, JPH::EActivation::Activate);
    if (c.body.IsInvalid()) return 0;

    // Neighbour rings: a vertex never collides with triangles touching it
    // or its neighbours (they're its own fabric, not something to avoid).
    std::vector<std::vector<uint32_t>> rings(n);
    auto link = [&](uint32_t a, uint32_t b) { rings[a].push_back(b); rings[b].push_back(a); };
    for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
        link(c.tris[t], c.tris[t + 1]);
        link(c.tris[t + 1], c.tris[t + 2]);
        link(c.tris[t + 2], c.tris[t]);
    }
    c.ringStart.assign(n + 1, 0);
    for (size_t i = 0; i < n; ++i) {
        std::sort(rings[i].begin(), rings[i].end());
        rings[i].erase(std::unique(rings[i].begin(), rings[i].end()), rings[i].end());
        c.ringStart[i + 1] = c.ringStart[i] + uint32_t(rings[i].size());
    }
    for (auto& r : rings) c.ring.insert(c.ring.end(), r.begin(), r.end());
    // Every edge once (for the edge-against-edge half of the pass).
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j : rings[i])
            if (j > i) {
                c.edges.push_back(i);
                c.edges.push_back(j);
            }
    c.stats.vertices = uint32_t(n);
    c.stats.triangles = uint32_t(c.tris.size() / 3);

    auto& stored = m_cloths.emplace(id, std::move(c)).first->second;
    // Jolt needs a skinned pose before the first step, and a hard one:
    // its previous skinned positions start out as NaN.
    if (stored.skinned) setJoints(id, stored.bindPose, true);
    return id;
}

void ClothSystem::remove(uint32_t id) {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end()) return;
    JPH::BodyInterface& bi = m_system.GetBodyInterface();
    bi.RemoveBody(it->second.body);
    bi.DestroyBody(it->second.body);
    m_cloths.erase(it);
}

bool ClothSystem::positions(uint32_t id, std::vector<glm::vec3>& out) const {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end()) return false;
    JPH::BodyLockRead lock(m_system.GetBodyLockInterface(), it->second.body);
    if (!lock.Succeeded()) return false;
    const JPH::Body& b = lock.GetBody();
    const JPH::RMat44 com = b.GetCenterOfMassTransform();
    const auto& verts = static_cast<const JPH::SoftBodyMotionProperties*>(b.GetMotionProperties())->GetVertices();
    out.resize(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const JPH::RVec3 w = com * verts[i].mPosition;
        out[i] = glm::vec3(float(w.GetX()), float(w.GetY()), float(w.GetZ()));
    }
    return true;
}

void ClothSystem::setJoints(uint32_t id, const std::vector<glm::mat4>& joints, bool hard) {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end() || !it->second.skinned || joints.empty()) return;
    Cloth& c = it->second;
    JPH::BodyLockWrite lock(m_system.GetBodyLockInterface(), c.body);
    if (!lock.Succeeded()) return;
    JPH::Body& b = lock.GetBody();
    const JPH::RMat44 com = b.GetCenterOfMassTransform();
    const glm::mat4 toCom = glm::inverse(toG(com));
    std::vector<JPH::Mat44> rel(c.bindPose.size(), JPH::Mat44::sIdentity());
    for (size_t j = 0; j < rel.size(); ++j) rel[j] = toJ(toCom * joints[std::min(j, joints.size() - 1)]);
    softOf(b)->SkinVertices(com, rel.data(), uint32_t(rel.size()), hard, m_temp);
    if (!b.IsActive()) {
        lock.ReleaseLock();
        m_system.GetBodyInterface().ActivateBody(c.body);
    }
}

void ClothSystem::setProtection(uint32_t id, ClothProtection level) {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end()) return;
    Cloth& c = it->second;
    c.level = level;
    c.prevValid = false;
    // The vertex radius is live; the tethers and back-stops are built in
    // (Jolt keeps constraints in shared settings): Off turns off what it can.
    JPH::BodyLockWrite lock(m_system.GetBodyLockInterface(), c.body);
    if (!lock.Succeeded()) return;
    JPH::SoftBodyMotionProperties* mp = softOf(lock.GetBody());
    mp->SetVertexRadius(level == ClothProtection::Off ? 0.0f : c.thickness);
}

ClothProtection ClothSystem::protection(uint32_t id) const {
    auto it = m_cloths.find(id);
    return it == m_cloths.end() ? ClothProtection::Off : it->second.level;
}

void ClothSystem::reset(uint32_t id) {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end()) return;
    Cloth& c = it->second;
    {
        JPH::BodyLockWrite lock(m_system.GetBodyLockInterface(), c.body);
        if (!lock.Succeeded()) return;
        JPH::Body& b = lock.GetBody();
        const JPH::RMat44 inv = b.GetCenterOfMassTransform().InversedRotationTranslation();
        auto& verts = softOf(b)->GetVertices();
        for (size_t i = 0; i < verts.size() && i < c.rest.size(); ++i) {
            verts[i].mPosition = JPH::Vec3(inv * JPH::RVec3(c.rest[i].x, c.rest[i].y, c.rest[i].z));
            verts[i].mPreviousPosition = verts[i].mPosition;
            verts[i].mVelocity = JPH::Vec3::sZero();
        }
    }
    c.prevValid = false;
    if (c.skinned) setJoints(id, c.bindPose, true);
    m_system.GetBodyInterface().ActivateBody(c.body);
}

ClothStats ClothSystem::stats(uint32_t id) const {
    auto it = m_cloths.find(id);
    if (it == m_cloths.end()) return {};
    ClothStats s = it->second.stats;
    s.sleeping = !m_system.GetBodyInterface().IsActive(it->second.body);
    return s;
}

void ClothSystem::setWind(const glm::vec3& v) {
    if (v == m_wind) return;
    m_wind = v;
    JPH::BodyInterface& bi = m_system.GetBodyInterface();
    for (auto& [id, c] : m_cloths)
        if (c.wind > 0.0f) bi.ActivateBody(c.body);
}

void ClothSystem::load(Cloth& c, JPH::Body& body) {
    const JPH::RMat44 com = body.GetCenterOfMassTransform();
    const auto& verts = softOf(body)->GetVertices();
    const size_t n = verts.size();
    c.pos.resize(n);
    c.vel.resize(n);
    c.invMass.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const JPH::RVec3 w = com * verts[i].mPosition;
        c.pos[i] = glm::vec3(float(w.GetX()), float(w.GetY()), float(w.GetZ()));
        c.vel[i] = toG(com.Multiply3x3(verts[i].mVelocity));
        c.invMass[i] = verts[i].mInvMass;
    }
}

void ClothSystem::store(Cloth& c, JPH::Body& body) {
    const JPH::RMat44 inv = body.GetCenterOfMassTransform().InversedRotationTranslation();
    auto& verts = softOf(body)->GetVertices();
    for (size_t i = 0; i < verts.size() && i < c.pos.size(); ++i) {
        if (verts[i].mInvMass <= 0.0f) continue;
        verts[i].mPosition = JPH::Vec3(inv * JPH::RVec3(c.pos[i].x, c.pos[i].y, c.pos[i].z));
        verts[i].mVelocity = inv.Multiply3x3(toJ(c.vel[i]));
    }
}

// Air resistance on every triangle: drag along its normal from the air's
// velocity relative to the cloth (wind minus motion). It's what makes silk
// float down slowly and a flag stand out in the wind. Applied implicitly
// (never more than the relative normal speed), so a feather-light fabric
// can't be flung by it.
void ClothSystem::air(Cloth& c, float dt) {
    if (c.fabric.airDrag <= 0.0f || c.tris.empty()) return;
    for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
        const uint32_t i0 = c.tris[t], i1 = c.tris[t + 1], i2 = c.tris[t + 2];
        const glm::vec3 cr = glm::cross(c.pos[i1] - c.pos[i0], c.pos[i2] - c.pos[i0]);
        const float len = glm::length(cr);
        if (len < 1e-12f) continue;
        const glm::vec3 nrm = cr / len;
        const float area = 0.5f * len;
        const glm::vec3 rel = (c.vel[i0] + c.vel[i1] + c.vel[i2]) / 3.0f - m_wind * c.wind;
        const float vn = glm::dot(rel, nrm);
        // Force = 0.5 rho Cd A |vn| vn, shared by the three vertices.
        const float k = 0.5f * kAirDensity * c.fabric.airDrag * area * std::fabs(vn) * dt / 3.0f;
        for (uint32_t i : { i0, i1, i2 }) {
            const float f = std::min(1.0f, k * c.invMass[i]);
            c.vel[i] -= nrm * (vn * f);
        }
    }
}

bool ClothSystem::nearInTopology(const Cloth& c, uint32_t v, uint32_t tri) const {
    const uint32_t* t = &c.tris[size_t(tri) * 3];
    const uint32_t* r0 = c.ring.data() + c.ringStart[v];
    const uint32_t* r1 = c.ring.data() + c.ringStart[v + 1];
    for (int k = 0; k < 3; ++k) {
        if (t[k] == v || std::binary_search(r0, r1, t[k])) return true;
    }
    return false;
}

// Self and cloth-vs-cloth collision for Full cloths: vertices against
// triangles. Keeps every vertex `thickness` from every other part of the
// fabric, and puts a vertex that crossed a triangle since the last pass
// back on the side it came from (with the relative approach speed
// removed, so it doesn't cross again next step).
//
// Broad phase: a dense spatial hash (Teschner et al. 2003, as in Mueller's
// "Ten Minute Physics" cloth), filled by counting sort: triangles go in
// by their swept, inflated bounds, each vertex looks in the cells its own
// swept bounds cover. Triangle normals (now and at the last pass) are
// computed once per pass, not once per test.
void ClothSystem::Grid::build(const std::vector<CellBox>& boxes) {
    size_t entries = 0;
    for (const CellBox& b : boxes)
        if (b.valid()) entries += size_t(b.hi.x - b.lo.x + 1) * size_t(b.hi.y - b.lo.y + 1) * size_t(b.hi.z - b.lo.z + 1);
    // Counting sort into a table of 2 x entries buckets.
    uint32_t size = 1024;
    while (size < entries * 2 && size < (1u << 24)) size <<= 1;
    mask = size - 1;
    start.assign(size + 1, 0);
    for (const CellBox& b : boxes)
        for (int x = b.lo.x; x <= b.hi.x; ++x)
            for (int y = b.lo.y; y <= b.hi.y; ++y)
                for (int z = b.lo.z; z <= b.hi.z; ++z) ++start[(hashCell(x, y, z) & mask) + 1];
    for (uint32_t i = 0; i < size; ++i) start[i + 1] += start[i];
    items.resize(entries);
    fill.assign(start.begin(), start.end() - 1);
    for (uint32_t i = 0; i < boxes.size(); ++i) {
        const CellBox& b = boxes[i];
        for (int x = b.lo.x; x <= b.hi.x; ++x)
            for (int y = b.lo.y; y <= b.hi.y; ++y)
                for (int z = b.lo.z; z <= b.hi.z; ++z) items[fill[hashCell(x, y, z) & mask]++] = i;
    }
}

void ClothSystem::shapeTriangles() {
    for (Cloth* cp : m_active) {
        Cloth& c = *cp;
        const size_t nt = c.tris.size() / 3;
        c.triN.resize(nt);
        c.triNPrev.resize(nt);
        c.triSphere.resize(nt);
        for (size_t t = 0; t < nt; ++t) {
            const uint32_t i0 = c.tris[t * 3], i1 = c.tris[t * 3 + 1], i2 = c.tris[t * 3 + 2];
            const glm::vec3 n = glm::cross(c.pos[i1] - c.pos[i0], c.pos[i2] - c.pos[i0]);
            const glm::vec3 np = glm::cross(c.prev[i1] - c.prev[i0], c.prev[i2] - c.prev[i0]);
            const float nl = glm::length(n), npl = glm::length(np);
            c.triN[t] = nl > 1e-12f ? n / nl : glm::vec3(0.0f);
            c.triNPrev[t] = npl > 1e-12f ? np / npl : c.triN[t];
            // Bounding sphere (centroid, farthest corner) for a cheap reject.
            const glm::vec3 centre = (c.pos[i0] + c.pos[i1] + c.pos[i2]) / 3.0f;
            const float r2 = std::max(std::max(glm::dot(c.pos[i0] - centre, c.pos[i0] - centre), glm::dot(c.pos[i1] - centre, c.pos[i1] - centre)),
                                      glm::dot(c.pos[i2] - centre, c.pos[i2] - centre));
            c.triSphere[t] = glm::vec4(centre, std::sqrt(r2));
        }
    }
}

// Runs fn over [0, count) in chunks, on Jolt's job system when there's
// one and the pass runs between steps (endStep), else right here.
void ClothSystem::parallel(uint32_t count, const std::function<void(uint32_t, uint32_t, Worker&)>& fn) {
    const int threads = (m_jobs && m_between) ? std::max(1, m_jobs->GetMaxConcurrency()) : 1;
    if (m_workers.size() < size_t(threads)) m_workers.resize(size_t(threads));
    for (Worker& w : m_workers) {
        w.vt.clear();
        w.ee.clear();
    }
    if (threads == 1 || count < 2048) {
        fn(0, count, m_workers[0]);
        return;
    }
    JPH::JobSystem::Barrier* barrier = m_jobs->CreateBarrier();
    const uint32_t chunk = (count + uint32_t(threads) - 1) / uint32_t(threads);
    for (int t = 0; t < threads; ++t) {
        const uint32_t begin = uint32_t(t) * chunk, end = std::min(count, begin + chunk);
        if (begin >= end) break;
        Worker* w = &m_workers[size_t(t)];
        JPH::JobHandle job = m_jobs->CreateJob("ClothProtection", JPH::Color::sGreen, [&fn, begin, end, w] { fn(begin, end, *w); });
        barrier->AddJob(job);
    }
    m_jobs->WaitForJobs(barrier);
    m_jobs->DestroyBarrier(barrier);
}

void ClothSystem::protect() {
    float cell = 0.0f;
    size_t triTotal = 0, edgeTotal = 0;
    for (Cloth* c : m_active) {
        cell = std::max(cell, std::max(c->meanEdge, 3.0f * c->thickness));
        triTotal += c->tris.size() / 3;
        edgeTotal += c->edges.size() / 2;
    }
    if (cell <= 0.0f || triTotal == 0) return;
    const float inv = 1.0f / cell;

    // --- Broad phase, once per step: who could touch whom ------------------
    // Everything's bounds swept from the last pass to now (+ thickness), in
    // a spatial hash; the pairs whose bounds meet are kept for the passes.
    shapeTriangles();
    m_triBox.resize(triTotal);
    m_triCloth.resize(triTotal);
    size_t g = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        c.triBase = uint32_t(g);
        for (size_t t = 0; t < c.tris.size() / 3; ++t, ++g) {
            const uint32_t i0 = c.tris[t * 3], i1 = c.tris[t * 3 + 1], i2 = c.tris[t * 3 + 2];
            const glm::vec3 lo = glm::min(glm::min(glm::min(c.pos[i0], c.prev[i0]), glm::min(c.pos[i1], c.prev[i1])), glm::min(c.pos[i2], c.prev[i2]));
            const glm::vec3 hi = glm::max(glm::max(glm::max(c.pos[i0], c.prev[i0]), glm::max(c.pos[i1], c.prev[i1])), glm::max(c.pos[i2], c.prev[i2]));
            CellBox& box = m_triBox[g];
            m_triCloth[g] = uint32_t(ci);
            if (c.triN[t] == glm::vec3(0.0f) || !cellBox(lo - c.thickness, hi + c.thickness, inv, 64, box.lo, box.hi)) box = CellBox{};
        }
    }
    m_triGrid.build(m_triBox);
    m_vertBase.resize(m_active.size() + 1);
    m_vertBase[0] = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) m_vertBase[ci + 1] = m_vertBase[ci] + uint32_t(m_active[ci]->pos.size());
    parallel(m_vertBase.back(), [&](uint32_t begin, uint32_t end, Worker& w) {
        w.stamp.assign(triTotal, UINT32_MAX);
        w.vt.clear();
        size_t ci = size_t(std::upper_bound(m_vertBase.begin(), m_vertBase.end(), begin) - m_vertBase.begin()) - 1;
        for (uint32_t gv = begin; gv < end; ++gv) {
            while (gv >= m_vertBase[ci + 1]) ++ci;
            Cloth& c = *m_active[ci];
            const uint32_t v = gv - m_vertBase[ci];
            if (c.invMass[v] <= 0.0f) continue; // pinned or asleep: others are pushed off it, from their side
            glm::ivec3 a, b;
            if (!cellBox(glm::min(c.pos[v], c.prev[v]) - c.thickness, glm::max(c.pos[v], c.prev[v]) + c.thickness, inv, 27, a, b)) continue;
            for (int x = a.x; x <= b.x; ++x)
                for (int y = a.y; y <= b.y; ++y)
                    for (int z = a.z; z <= b.z; ++z) {
                        const uint32_t h = hashCell(x, y, z) & m_triGrid.mask;
                        for (uint32_t e = m_triGrid.start[h]; e < m_triGrid.start[h + 1]; ++e) {
                            const uint32_t gt = m_triGrid.items[e];
                            if (w.stamp[gt] == gv) continue; // already seen from another cell
                            w.stamp[gt] = gv;
                            Cloth& o = *m_active[m_triCloth[gt]];
                            const uint32_t tri = gt - o.triBase;
                            if (&o == &c && nearInTopology(c, v, tri)) continue;
                            // Far from the triangle (with room for the passes to move things): skip.
                            const glm::vec4& sph = o.triSphere[tri];
                            const float reach = sph.w + 3.0f * std::max(c.thickness, o.thickness) + c.motion[v];
                            const glm::vec3 dc = c.pos[v] - glm::vec3(sph);
                            const float d2 = glm::dot(dc, dc);
                            // Within an edge of it: its edges may meet the triangle's (edge pass below).
                            const float edgeReach = reach + c.meanEdge;
                            if (d2 > edgeReach * edgeReach) continue;
                            // (Its own fabric a few threads away doesn't count: that's always this near.)
                            const uint32_t* t = &o.tris[size_t(tri) * 3];
                            const glm::vec3 apart = c.rest[v] - o.rest[t[0]];
                            if (&o != &c || glm::dot(apart, apart) > 9.0f * c.meanEdge * c.meanEdge) {
                                for (uint8_t* flag : { &c.nearOther[v], &o.nearOther[t[0]], &o.nearOther[t[1]], &o.nearOther[t[2]] })
                                    std::atomic_ref<uint8_t>(*flag).store(1, std::memory_order_relaxed);
                            }
                            if (d2 > reach * reach) continue;
                            w.vt.push_back({ uint32_t(ci), v, gt });
                        }
                    }
        }
    });
    m_vtPairs.clear();
    for (const Worker& w : m_workers) m_vtPairs.insert(m_vtPairs.end(), w.vt.begin(), w.vt.end());

    // Edges: two sheets sliding over each other can pass edge through edge
    // with no vertex ever going through a triangle.
    m_edgeBox.resize(edgeTotal);
    m_edgeCloth.resize(edgeTotal);
    m_edgeLo.resize(edgeTotal);
    m_edgeHi.resize(edgeTotal);
    m_edgeMoves.resize(edgeTotal);
    g = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        c.edgeBase = uint32_t(g);
        for (size_t k = 0; k + 1 < c.edges.size(); k += 2, ++g) {
            const uint32_t i0 = c.edges[k], i1 = c.edges[k + 1];
            m_edgeLo[g] = glm::min(glm::min(c.pos[i0], c.prev[i0]), glm::min(c.pos[i1], c.prev[i1])) - c.thickness;
            m_edgeHi[g] = glm::max(glm::max(c.pos[i0], c.prev[i0]), glm::max(c.pos[i1], c.prev[i1])) + c.thickness;
            m_edgeCloth[g] = uint32_t(ci);
            m_edgeMoves[g] = uint8_t((c.invMass[i0] > 0.0f || c.invMass[i1] > 0.0f ? 1 : 0) | (c.nearOther[i0] || c.nearOther[i1] ? 2 : 0));
            if (!cellBox(m_edgeLo[g], m_edgeHi[g], inv, 27, m_edgeBox[g].lo, m_edgeBox[g].hi)) m_edgeBox[g] = CellBox{};
        }
    }
    m_edgeGrid.build(m_edgeBox);
    parallel(uint32_t(edgeTotal), [&](uint32_t begin, uint32_t end, Worker& w) {
        w.stamp.assign(edgeTotal, UINT32_MAX);
        w.ee.clear();
        for (uint32_t ge = begin; ge < end; ++ge) {
            // Only edges that can move, near another surface (found by the
            // vertex pass): nothing else can meet another edge this step.
            if (m_edgeMoves[ge] != 3) continue;
            const CellBox& b = m_edgeBox[ge];
            Cloth& c = *m_active[m_edgeCloth[ge]];
            const uint32_t e = ge - c.edgeBase;
            for (int x = b.lo.x; x <= b.hi.x; ++x)
                for (int y = b.lo.y; y <= b.hi.y; ++y)
                    for (int z = b.lo.z; z <= b.hi.z; ++z) {
                        const uint32_t h = hashCell(x, y, z) & m_edgeGrid.mask;
                        for (uint32_t k = m_edgeGrid.start[h]; k < m_edgeGrid.start[h + 1]; ++k) {
                            const uint32_t gf = m_edgeGrid.items[k];
                            // Each pair once: from the lower edge when both move.
                            if ((m_edgeMoves[gf] == 3 && gf <= ge) || !(m_edgeMoves[gf] & 2) || w.stamp[gf] == ge) continue;
                            w.stamp[gf] = ge;
                            if (glm::any(glm::lessThan(m_edgeHi[gf], m_edgeLo[ge])) || glm::any(glm::greaterThan(m_edgeLo[gf], m_edgeHi[ge]))) continue;
                            Cloth& o = *m_active[m_edgeCloth[gf]];
                            const uint32_t f = gf - o.edgeBase;
                            if (&o == &c && edgesNear(c, e, f)) continue;
                            if (edgesApart(c, e, o, f, 3.0f * std::max(c.thickness, o.thickness))) continue;
                            w.ee.push_back({ ge, gf });
                        }
                    }
        }
    });
    m_eePairs.clear();
    for (const Worker& w : m_workers) m_eePairs.insert(m_eePairs.end(), w.ee.begin(), w.ee.end());

    // --- Narrow phase, repeated while it undoes crossings -------------------
    // One pass fixes pairs one after another; in a stack (a throw landing
    // on a sheet on a blanket) fixing one pair can push another through,
    // so it runs again, up to kMaxPasses, until a pass undoes nothing.
    for (Cloth* c : m_active) c->movedIn.assign(c->pos.size(), 0);
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        m_pass = uint16_t(pass + 1);
        if (pass > 0) shapeTriangles();
        uint32_t undone = 0;
        // After the first pass, only what the last pass moved can have changed.
        const uint16_t last = uint16_t(pass);
        for (const VtPair& pr : m_vtPairs) {
            Cloth& c = *m_active[pr.cloth];
            Cloth& o = *m_active[m_triCloth[pr.tri]];
            const uint32_t tri = pr.tri - o.triBase;
            if (pass > 0) {
                const uint32_t* t = &o.tris[size_t(tri) * 3];
                if (c.movedIn[pr.vertex] != last && o.movedIn[t[0]] != last && o.movedIn[t[1]] != last && o.movedIn[t[2]] != last) continue;
            }
            // Too far from the triangle now and at the last pass: can't touch or have crossed it.
            const glm::vec4& sph = o.triSphere[tri];
            const float reach = sph.w + std::max(c.thickness, o.thickness) + glm::length(c.pos[pr.vertex] - c.prev[pr.vertex]);
            const glm::vec3 dc = c.pos[pr.vertex] - glm::vec3(sph);
            if (glm::dot(dc, dc) > reach * reach) continue;
            undone += testVertexTriangle(c, pr.vertex, o, tri) ? 1u : 0u;
        }
        for (const EePair& pr : m_eePairs) {
            Cloth& c = *m_active[m_edgeCloth[pr.a]];
            Cloth& o = *m_active[m_edgeCloth[pr.b]];
            if (pass > 0) {
                const uint32_t* ea = &c.edges[size_t(pr.a - c.edgeBase) * 2];
                const uint32_t* eb = &o.edges[size_t(pr.b - o.edgeBase) * 2];
                if (c.movedIn[ea[0]] != last && c.movedIn[ea[1]] != last && o.movedIn[eb[0]] != last && o.movedIn[eb[1]] != last) continue;
            }
            undone += testEdgeEdge(c, pr.a - c.edgeBase, o, pr.b - o.edgeBase) ? 1u : 0u;
        }
        if (undone == 0) break;
    }
}

// Cheap reject: two edges can only pass through each other when the
// tetrahedron they span turns inside out (its volume changes sign), and
// can only be closer than gap when their lines are.
bool ClothSystem::edgesApart(const Cloth& c, uint32_t e, const Cloth& o, uint32_t f, float gap) {
    const uint32_t a0 = c.edges[size_t(e) * 2], a1 = c.edges[size_t(e) * 2 + 1];
    const uint32_t b0 = o.edges[size_t(f) * 2], b1 = o.edges[size_t(f) * 2 + 1];
    const glm::vec3 uab = glm::cross(c.pos[a1] - c.pos[a0], o.pos[b1] - o.pos[b0]);
    const float vol = glm::dot(o.pos[b0] - c.pos[a0], uab);
    const float volPrev = glm::dot(o.prev[b0] - c.prev[a0], glm::cross(c.prev[a1] - c.prev[a0], o.prev[b1] - o.prev[b0]));
    return vol * volPrev > 0.0f && vol * vol >= gap * gap * glm::dot(uab, uab);
}

bool ClothSystem::edgesNear(const Cloth& c, uint32_t e, uint32_t f) const {
    const uint32_t a0 = c.edges[size_t(e) * 2], a1 = c.edges[size_t(e) * 2 + 1];
    const uint32_t b0 = c.edges[size_t(f) * 2], b1 = c.edges[size_t(f) * 2 + 1];
    for (uint32_t a : { a0, a1 }) {
        const uint32_t* r0 = c.ring.data() + c.ringStart[a];
        const uint32_t* r1 = c.ring.data() + c.ringStart[a + 1];
        for (uint32_t b : { b0, b1 })
            if (a == b || std::binary_search(r0, r1, b)) return true;
    }
    return false;
}

bool ClothSystem::testEdgeEdge(Cloth& c, uint32_t e, Cloth& o, uint32_t f) {
    const uint32_t a0 = c.edges[size_t(e) * 2], a1 = c.edges[size_t(e) * 2 + 1];
    const uint32_t b0 = o.edges[size_t(f) * 2], b1 = o.edges[size_t(f) * 2 + 1];
    const float thick = std::max(c.thickness, o.thickness);
    if (edgesApart(c, e, o, f, thick)) return false;
    const glm::vec3 ua = c.pos[a1] - c.pos[a0], ub = o.pos[b1] - o.pos[b0];
    const glm::vec3 uab = glm::cross(ua, ub);
    const float vol = glm::dot(o.pos[b0] - c.pos[a0], uab);
    const float volPrev = glm::dot(o.prev[b0] - c.prev[a0], glm::cross(c.prev[a1] - c.prev[a0], o.prev[b1] - o.prev[b0]));
    const float uab2 = glm::dot(uab, uab);
    // Which side of each other they were on: the closest points at the last pass.
    float sp, tp;
    closestSegments(c.prev[a0], c.prev[a1], o.prev[b0], o.prev[b1], sp, tp);
    const glm::vec3 dPrev = glm::mix(c.prev[a0], c.prev[a1], sp) - glm::mix(o.prev[b0], o.prev[b1], tp);
    const float lPrev = glm::length(dPrev);
    float s, t;
    closestSegments(c.pos[a0], c.pos[a1], o.pos[b0], o.pos[b1], s, t);
    const glm::vec3 d = glm::mix(c.pos[a0], c.pos[a1], s) - glm::mix(o.pos[b0], o.pos[b1], t);
    const float l = glm::length(d);
    // Only edges meeting in their middles: at an end, the vertex against
    // triangle test is the one that sees it.
    auto middle = [](float u) { return u > 0.02f && u < 0.98f; };
    glm::vec3 n;
    float gap;
    bool crossed = false;
    const bool tangled = std::max(std::max(c.undoneStreak[a0], c.undoneStreak[a1]), std::max(o.undoneStreak[b0], o.undoneStreak[b1])) >= kTangledSteps;
    // Nearly parallel edges have no well-defined closest points to compare.
    const bool crossing = vol * volPrev <= 0.0f && uab2 > 0.04f * glm::dot(ua, ua) * glm::dot(ub, ub);
    if (crossing && lPrev > 1e-6f && !tangled && middle(sp) && middle(tp) && middle(s) && middle(t) && glm::dot(d, dPrev) < 0.0f) {
        // Went through each other since the last pass: back to the side they came from.
        n = dPrev / lPrev;
        gap = glm::dot(d, n);
        crossed = true;
    } else {
        if (l >= thick || l < 1e-7f || !middle(s) || !middle(t)) return false;
        n = d / l;
        gap = l;
    }
    const float w0 = c.invMass[a0] * (1.0f - s), w1 = c.invMass[a1] * s;
    const float w2 = o.invMass[b0] * (1.0f - t), w3 = o.invMass[b1] * t;
    const float wsum = w0 * (1.0f - s) + w1 * s + w2 * (1.0f - t) + w3 * t;
    if (wsum <= 0.0f) return false;
    const float lambda = (thick - gap) / wsum;
    c.movedIn[a0] = c.movedIn[a1] = o.movedIn[b0] = o.movedIn[b1] = m_pass;
    const float moved = std::max(std::max(c.motion[a0], c.motion[a1]), std::max(o.motion[b0], o.motion[b1]));
    for (float* r : { &c.reach[a0], &c.reach[a1], &o.reach[b0], &o.reach[b1] }) *r = std::max(*r, moved);
    c.pos[a0] += n * (w0 * lambda);
    c.pos[a1] += n * (w1 * lambda);
    o.pos[b0] -= n * (w2 * lambda);
    o.pos[b1] -= n * (w3 * lambda);
    // Velocity: no more approach along n.
    const glm::vec3 va = glm::mix(c.vel[a0], c.vel[a1], s), vb = glm::mix(o.vel[b0], o.vel[b1], t);
    const float vrel = glm::dot(va - vb, n);
    if (vrel < 0.0f) {
        const float j = -vrel / wsum;
        c.vel[a0] += n * (w0 * j);
        c.vel[a1] += n * (w1 * j);
        o.vel[b0] -= n * (w2 * j);
        o.vel[b1] -= n * (w3 * j);
    }
    ++c.stats.selfContacts;
    if (crossed) {
        ++c.stats.crossingsUndone;
        c.undoneNow[a0] = c.undoneNow[a1] = 1;
        o.undoneNow[b0] = o.undoneNow[b1] = 1;
    }
    return crossed;
}

bool ClothSystem::testVertexTriangle(Cloth& c, uint32_t v, Cloth& o, uint32_t tri) {
    const uint32_t* t = &o.tris[size_t(tri) * 3];
    const float thick = std::max(c.thickness, o.thickness);
    glm::vec3& p = c.pos[v];
    const glm::vec3 &a = o.pos[t[0]], &b = o.pos[t[1]], &d = o.pos[t[2]];
    const glm::vec3& nrm = o.triN[tri];
    const float s = glm::dot(p - a, nrm);
    // Which side it was on: the same test with last pass's positions.
    const glm::vec3 &pa = o.prev[t[0]], &pb = o.prev[t[1]], &pd = o.prev[t[2]];
    const float sp = glm::dot(c.prev[v] - pa, o.triNPrev[tri]);
    // A triangle that turned over (crumpled in a heap) flips the sign of
    // every distance to it: that's not the vertex crossing it.
    const bool signFlip = sp * s < 0.0f && std::fabs(sp) > 1e-7f && glm::dot(nrm, o.triNPrev[tri]) > 0.0f;
    if (!signFlip && std::fabs(s) >= thick) return false; // the common case: nowhere near
    float side = 0.0f;
    bool crossed = false;
    if (signFlip) {
        // Where along the step it met the plane, and was it inside the triangle then?
        const float k = sp / (sp - s);
        const glm::vec3 bc = barycentric(glm::mix(c.prev[v], p, k), glm::mix(pa, a, k), glm::mix(pb, b, k), glm::mix(pd, d, k));
        if (inside(bc, 0.02f) && c.undoneStreak[v] < kTangledSteps) {
            side = sp > 0.0f ? 1.0f : -1.0f;
            crossed = true;
        }
    }
    if (!crossed) {
        if (std::fabs(s) >= thick) return false;
        if (!inside(barycentric(p, a, b, d), 0.0f)) return false;
        const float ref = std::fabs(sp) > 1e-6f && c.undoneStreak[v] < kTangledSteps ? sp : s;
        side = ref >= 0.0f ? 1.0f : -1.0f;
    }
    const glm::vec3 bc = glm::clamp(barycentric(p, a, b, d), glm::vec3(0.0f), glm::vec3(1.0f));
    const float wp = c.invMass[v];
    const float wa = o.invMass[t[0]], wb = o.invMass[t[1]], wd = o.invMass[t[2]];
    const float wsum = wp + bc.x * bc.x * wa + bc.y * bc.y * wb + bc.z * bc.z * wd;
    if (wsum <= 0.0f) return false;
    // Position: s -> side * thick.
    const float lambda = (side * thick - s) / wsum;
    c.movedIn[v] = o.movedIn[t[0]] = o.movedIn[t[1]] = o.movedIn[t[2]] = m_pass;
    const float moved = std::max(std::max(c.motion[v], o.motion[t[0]]), std::max(o.motion[t[1]], o.motion[t[2]]));
    for (float* r : { &c.reach[v], &o.reach[t[0]], &o.reach[t[1]], &o.reach[t[2]] }) *r = std::max(*r, moved);
    p += nrm * (wp * lambda);
    o.pos[t[0]] -= nrm * (wa * bc.x * lambda);
    o.pos[t[1]] -= nrm * (wb * bc.y * lambda);
    o.pos[t[2]] -= nrm * (wd * bc.z * lambda);
    // Velocity: no more approach along the normal.
    const glm::vec3 vt = o.vel[t[0]] * bc.x + o.vel[t[1]] * bc.y + o.vel[t[2]] * bc.z;
    const float vrel = glm::dot(c.vel[v] - vt, nrm);
    float pushed = std::fabs(lambda) / m_dt; // how hard they were pressed together (as a speed x mass)
    if (vrel * side < 0.0f) {
        const float j = -vrel / wsum;
        c.vel[v] += nrm * (wp * j);
        o.vel[t[0]] -= nrm * (wa * bc.x * j);
        o.vel[t[1]] -= nrm * (wb * bc.y * j);
        o.vel[t[2]] -= nrm * (wd * bc.z * j);
        pushed += std::fabs(j);
    }
    // Friction (Coulomb): cloth on cloth grips as the two fabrics do, so a
    // throw stays on the blanket instead of sliding off it.
    const float mu = std::sqrt(c.fabric.friction * o.fabric.friction);
    const glm::vec3 vt2 = o.vel[t[0]] * bc.x + o.vel[t[1]] * bc.y + o.vel[t[2]] * bc.z;
    glm::vec3 slide = c.vel[v] - vt2;
    slide -= nrm * glm::dot(slide, nrm);
    const float slideLen = glm::length(slide);
    if (mu > 0.0f && slideLen > 1e-6f) {
        // The impulse that would stop the sliding, capped at mu x the normal one.
        const float j = std::min(slideLen / wsum, mu * pushed);
        const glm::vec3 dir = slide / slideLen;
        c.vel[v] -= dir * (wp * j);
        o.vel[t[0]] += dir * (wa * bc.x * j);
        o.vel[t[1]] += dir * (wb * bc.y * j);
        o.vel[t[2]] += dir * (wd * bc.z * j);
    }
    ++c.stats.selfContacts;
    if (crossed) {
        ++c.stats.crossingsUndone;
        c.undoneNow[v] = 1;
    }
    return crossed;
}

// Called from Jolt's worker threads while it steps: only reads m_cloths
// (which nothing changes during a step).
JPH::SoftBodyValidateResult ClothSystem::OnSoftBodyContactValidate(const JPH::Body& softBody, const JPH::Body&, JPH::SoftBodyContactSettings& settings) {
    auto it = m_cloths.find(uint32_t(softBody.GetUserData()));
    if (it != m_cloths.end()) settings.mInvMassScale1 = it->second.contactInvMassScale;
    return JPH::SoftBodyValidateResult::AcceptContact;
}

// Before each collision step: air on the cloth that moves, and the
// protection pass for all but the first collision step of an update
// (that one had its pass at the end of the last update).
void ClothSystem::OnStep(const JPH::PhysicsStepListenerContext& ctx) {
    if (m_cloths.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    m_dt = std::max(ctx.mDeltaTime, 1e-5f);
    // Jolt holds every body lock during a step listener: no locking here.
    const JPH::BodyLockInterfaceNoLock& locks = m_system.GetBodyLockInterfaceNoLock();
    if (!m_protectedAfterStep) protectAll(locks);
    m_protectedAfterStep = false;
    for (auto& [id, c] : m_cloths) {
        if (c.fabric.airDrag <= 0.0f || c.tris.empty()) continue;
        JPH::Body* body = locks.TryGetBody(c.body);
        if (!body || !body->IsActive()) continue;
        load(c, *body);
        air(c, ctx.mDeltaTime);
        store(c, *body);
    }
    m_stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void ClothSystem::endStep() {
    // The pass runs after the step too, so what's drawn (and what the game
    // reads) is the cloth with every crossing the step made already undone.
    if (!m_cloths.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        m_between = true;
        protectAll(m_system.GetBodyLockInterfaceNoLock()); // between steps: nothing else touches the bodies
        m_between = false;
        m_protectedAfterStep = true;
        m_stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    m_lastMs = m_stepMs;
}

void ClothSystem::protectAll(const JPH::BodyLockInterface& locks) {
    m_active.clear();
    for (auto& [id, c] : m_cloths) {
        c.stats.selfContacts = 0;
        c.stats.crossingsUndone = 0;
        c.stepBody = nullptr;
        if (c.level != ClothProtection::Full) continue;
        JPH::Body* body = locks.TryGetBody(c.body);
        if (!body) continue;
        load(c, *body);
        if (body->IsActive()) {
            c.stepBody = body;
        } else {
            // Asleep: still an obstacle for the others, but nothing of it moves.
            std::fill(c.invMass.begin(), c.invMass.end(), 0.0f);
            std::fill(c.vel.begin(), c.vel.end(), glm::vec3(0.0f));
        }
        if (!c.prevValid || c.prev.size() != c.pos.size()) c.prev = c.pos;
        c.solved = c.pos;
        c.motion.resize(c.pos.size());
        for (size_t i = 0; i < c.pos.size(); ++i) c.motion[i] = glm::length(c.pos[i] - c.prev[i]);
        c.reach = c.motion;
        c.nearOther.assign(c.pos.size(), 0);
        c.undoneStreak.resize(c.pos.size(), 0);
        c.undoneNow.assign(c.pos.size(), 0);
        m_active.push_back(&c);
    }
    if (m_active.empty()) return;
    protect();
    for (Cloth* c : m_active) {
        for (size_t i = 0; i < c->pos.size(); ++i) {
            c->undoneStreak[i] = c->undoneNow[i] ? uint8_t(std::min(255, c->undoneStreak[i] + 2)) : uint8_t(std::max(0, c->undoneStreak[i] - 1));
            // However many contacts pile up on one vertex (a heap of cloth
            // on the floor), the pass moves it no further than the step
            // moved it or anything it touched, plus a little: enough to
            // undo any crossing, never enough to fight the solver and the
            // floor and feed energy into the heap.
            const float reach = c->reach[i] + 3.0f * c->thickness;
            const glm::vec3 d = c->pos[i] - c->solved[i];
            const float l2 = glm::dot(d, d);
            if (l2 > reach * reach) c->pos[i] = c->solved[i] + d * (reach / std::sqrt(l2));
        }
        if (c->stepBody) store(*c, *c->stepBody);
        c->prev = c->pos;
        c->prevValid = true;
    }
}

} // namespace kke::detail
