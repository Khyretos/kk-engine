#include "ClothSystem.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>

namespace kke::detail {

namespace {

constexpr float kAirDensity = 1.225f; // kg/m^3
constexpr float kAssumedStep = 1.0f / 60.0f; // RigidWorld's collision step length

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

uint64_t cellKey(int x, int y, int z) {
    return (uint64_t(uint32_t(x) & 0x1fffffu) << 42) | (uint64_t(uint32_t(y) & 0x1fffffu) << 21) | uint64_t(uint32_t(z) & 0x1fffffu);
}

} // namespace

ClothSystem::ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp)
    : m_system(system), m_layer(layer), m_temp(temp) {
    m_system.AddStepListener(this);
}

ClothSystem::~ClothSystem() {
    m_system.RemoveStepListener(this);
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
    s->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Dihedral);
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
    c.stats.vertices = uint32_t(n);
    c.stats.triangles = uint32_t(c.tris.size() / 3);

    const uint32_t id = m_next++;
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
    for (auto& [id, c] : m_cloths) bi.ActivateBody(c.body);
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
        const glm::vec3 rel = (c.vel[i0] + c.vel[i1] + c.vel[i2]) / 3.0f - m_wind;
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
void ClothSystem::protect() {
    m_cells.clear();
    float cell = 0.0f;
    for (size_t ci = 0; ci < m_active.size(); ++ci) cell = std::max(cell, std::max(m_active[ci]->meanEdge, 4.0f * m_active[ci]->thickness));
    if (cell <= 0.0f) return;
    m_cellSize = cell;
    const float inv = 1.0f / cell;
    auto cellOf = [inv](float v) { return int(std::floor(v * inv)); };

    // Triangles into the grid by their swept, inflated bounds.
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        const Cloth& c = *m_active[ci];
        for (size_t t = 0; t + 2 < c.tris.size(); t += 3) {
            glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = c.tris[t + size_t(k)];
                lo = glm::min(lo, glm::min(c.pos[v], c.prev[v]));
                hi = glm::max(hi, glm::max(c.pos[v], c.prev[v]));
            }
            lo -= glm::vec3(c.thickness);
            hi += glm::vec3(c.thickness);
            const int x0 = cellOf(lo.x), x1 = cellOf(hi.x), y0 = cellOf(lo.y), y1 = cellOf(hi.y), z0 = cellOf(lo.z), z1 = cellOf(hi.z);
            if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 512) continue; // flung across the room in one step: nothing sane to test
            for (int x = x0; x <= x1; ++x)
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z) m_cells.push_back({ cellKey(x, y, z), uint32_t(ci), uint32_t(t / 3) });
        }
    }
    std::sort(m_cells.begin(), m_cells.end(), [](const CellEntry& a, const CellEntry& b) { return a.cell < b.cell; });

    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        for (uint32_t v = 0; v < c.pos.size(); ++v) {
            if (c.invMass[v] <= 0.0f) continue;
            const glm::vec3 lo = glm::min(c.pos[v], c.prev[v]) - glm::vec3(c.thickness);
            const glm::vec3 hi = glm::max(c.pos[v], c.prev[v]) + glm::vec3(c.thickness);
            const int x0 = cellOf(lo.x), x1 = cellOf(hi.x), y0 = cellOf(lo.y), y1 = cellOf(hi.y), z0 = cellOf(lo.z), z1 = cellOf(hi.z);
            if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 64) continue;
            for (int x = x0; x <= x1; ++x)
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z) {
                        const uint64_t key = cellKey(x, y, z);
                        auto range = std::equal_range(m_cells.begin(), m_cells.end(), CellEntry{ key, 0, 0 },
                                                      [](const CellEntry& a, const CellEntry& b) { return a.cell < b.cell; });
                        for (auto e = range.first; e != range.second; ++e) {
                            Cloth& o = *m_active[e->cloth];
                            if (&o == &c && nearInTopology(c, v, e->tri)) continue;
                            const uint32_t* t = &o.tris[size_t(e->tri) * 3];
                            const float thick = std::max(c.thickness, o.thickness);
                            glm::vec3& p = c.pos[v];
                            const glm::vec3 &a = o.pos[t[0]], &b = o.pos[t[1]], &d = o.pos[t[2]];
                            glm::vec3 nrm = glm::cross(b - a, d - a);
                            const float nl = glm::length(nrm);
                            if (nl < 1e-12f) continue;
                            nrm /= nl;
                            const float s = glm::dot(p - a, nrm);
                            // Which side it was on: the same test with last pass's positions.
                            const glm::vec3 &pa = o.prev[t[0]], &pb = o.prev[t[1]], &pd = o.prev[t[2]];
                            glm::vec3 pn = glm::cross(pb - pa, pd - pa);
                            const float pnl = glm::length(pn);
                            const float sp = pnl > 1e-12f ? glm::dot(c.prev[v] - pa, pn / pnl) : s;
                            float side = 0.0f;
                            bool crossed = false;
                            if (sp * s < 0.0f && std::fabs(sp) > 1e-7f) {
                                // Where along the step it met the plane, and was it inside the triangle then?
                                const float k = sp / (sp - s);
                                const glm::vec3 bc = barycentric(glm::mix(c.prev[v], p, k), glm::mix(pa, a, k), glm::mix(pb, b, k), glm::mix(pd, d, k));
                                if (inside(bc, 0.02f)) {
                                    side = sp > 0.0f ? 1.0f : -1.0f;
                                    crossed = true;
                                }
                            }
                            if (!crossed) {
                                if (std::fabs(s) >= thick) continue;
                                if (!inside(barycentric(p, a, b, d), 0.0f)) continue;
                                const float ref = std::fabs(sp) > 1e-6f ? sp : s;
                                side = ref >= 0.0f ? 1.0f : -1.0f;
                            }
                            const glm::vec3 bc = glm::clamp(barycentric(p, a, b, d), glm::vec3(0.0f), glm::vec3(1.0f));
                            const float wp = c.invMass[v];
                            const float wa = o.invMass[t[0]], wb = o.invMass[t[1]], wd = o.invMass[t[2]];
                            const float wsum = wp + bc.x * bc.x * wa + bc.y * bc.y * wb + bc.z * bc.z * wd;
                            if (wsum <= 0.0f) continue;
                            // Position: s -> side * thick.
                            const float lambda = (side * thick - s) / wsum;
                            p += nrm * (wp * lambda);
                            o.pos[t[0]] -= nrm * (wa * bc.x * lambda);
                            o.pos[t[1]] -= nrm * (wb * bc.y * lambda);
                            o.pos[t[2]] -= nrm * (wd * bc.z * lambda);
                            // Velocity: no more approach along the normal.
                            const glm::vec3 vt = o.vel[t[0]] * bc.x + o.vel[t[1]] * bc.y + o.vel[t[2]] * bc.z;
                            const float vrel = glm::dot(c.vel[v] - vt, nrm);
                            if (vrel * side < 0.0f) {
                                const float j = -vrel / wsum;
                                c.vel[v] += nrm * (wp * j);
                                o.vel[t[0]] -= nrm * (wa * bc.x * j);
                                o.vel[t[1]] -= nrm * (wb * bc.y * j);
                                o.vel[t[2]] -= nrm * (wd * bc.z * j);
                            }
                            ++c.stats.selfContacts;
                            if (crossed) ++c.stats.crossingsUndone;
                        }
                    }
        }
    }
}

void ClothSystem::OnStep(const JPH::PhysicsStepListenerContext& ctx) {
    if (m_cloths.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    // Jolt holds every body lock during a step listener: no locking here.
    const JPH::BodyLockInterfaceNoLock& locks = m_system.GetBodyLockInterfaceNoLock();
    m_active.clear();
    for (auto& [id, c] : m_cloths) {
        c.stats.selfContacts = 0;
        c.stats.crossingsUndone = 0;
        c.stepBody = nullptr;
        JPH::Body* body = locks.TryGetBody(c.body);
        if (!body) continue;
        const bool active = body->IsActive();
        if (!active && c.level != ClothProtection::Full) continue;
        load(c, *body);
        if (active) {
            c.stepBody = body;
            air(c, ctx.mDeltaTime);
        } else {
            // Asleep: still an obstacle for the others, but nothing of it moves.
            std::fill(c.invMass.begin(), c.invMass.end(), 0.0f);
            std::fill(c.vel.begin(), c.vel.end(), glm::vec3(0.0f));
        }
        if (c.level == ClothProtection::Full) {
            if (!c.prevValid || c.prev.size() != c.pos.size()) c.prev = c.pos;
            m_active.push_back(&c);
        }
    }
    if (!m_active.empty()) protect();
    for (auto& [id, c] : m_cloths) {
        if (c.stepBody) store(c, *c.stepBody);
        if (c.level == ClothProtection::Full && !c.pos.empty()) {
            c.prev = c.pos;
            c.prevValid = true;
        }
    }
    m_stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace kke::detail
