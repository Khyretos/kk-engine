#include "ClothSystem.h"

#include "kke/ClothGpu.h"
#include "kke/Log.h"

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

namespace kke::detail {

namespace {

constexpr float kAirDensity = 1.225f; // kg/m^3
constexpr float kAssumedStep = 1.0f / 60.0f; // RigidWorld's collision step length
constexpr uint8_t kTangledSteps = 16;         // Cloth::undoneStreak at which a vertex is let go (no sub-steps only)
constexpr int kMaxPasses = 4;                 // protection narrow-phase passes per step, at most
constexpr size_t kHairPart = 64;              // guide strands per hair soft body
constexpr uint32_t kPatchTriangles = 32;      // triangles per patch (ClothSystem::markPatches)
constexpr int64_t kMaxBoxCells = 4096;       // cells one box may cover in the protection's grids
constexpr uint32_t kMaxPatchBits = 8192;      // patches in all at most for the pair bit matrix (8 MB)
// A connected piece of surface whose normals all stay within this of one
// direction for the whole step can't pass through itself (Volino and
// Magnenat-Thalmann 1994; Provot 1997): just under 90 degrees.
constexpr float kFlatEnough = 1.5f;
// While the pass is undoing crossings, each collision step is cut in
// RigidWorld::Settings::clothSubsteps and the pass runs between them, not
// only after the solver's last sub-step. Layers a solid presses together
// (sheets over a ball) are then kept apart as they are pressed, a little
// at a time: fixed all at once afterwards, the solver springs back from
// the fix and the layers work their way through each other.
constexpr uint32_t kCalmUpdates = 30; // updates with no crossing undone before sub-steps stop
constexpr float kMostPerPass = 3.0f;  // the pass moves a vertex at most this x (thickness + a tenth of an edge)

// Sets a flag other worker threads may set too. The Android NDK's libc++
// has no std::atomic_ref yet; the builtin is the same relaxed store.
void setFlag(uint8_t& flag) {
#if defined(__cpp_lib_atomic_ref)
    std::atomic_ref<uint8_t>(flag).store(1, std::memory_order_relaxed);
#else
    __atomic_store_n(&flag, uint8_t(1), __ATOMIC_RELAXED);
#endif
}

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
// The cells a box covers. Only a box that isn't finite, or covers more
// than kMaxBoxCells (a cloth teleported by reset), is left out: anything
// moving fast gets a bigger box, never a skipped one.
bool cellBox(const glm::vec3& lo, const glm::vec3& hi, float inv, glm::ivec3& a, glm::ivec3& b) {
    if (!std::isfinite(lo.x + lo.y + lo.z + hi.x + hi.y + hi.z)) return false;
    auto cellOf = [inv](float v) { return int(std::clamp(std::floor(v * inv), -1.0e6f, 1.0e6f)); };
    a = glm::ivec3(cellOf(lo.x), cellOf(lo.y), cellOf(lo.z));
    b = glm::ivec3(cellOf(hi.x), cellOf(hi.y), cellOf(hi.z));
    const glm::ivec3 span = b - a + 1;
    return int64_t(span.x) * span.y * span.z <= kMaxBoxCells;
}

uint32_t hashCell(int x, int y, int z) { return uint32_t(x) * 92837111u ^ uint32_t(y) * 689287499u ^ uint32_t(z) * 283923481u; }

} // namespace

ClothSystem::ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp, JPH::JobSystem* jobs, int substeps)
    : m_system(system), m_layer(layer), m_temp(temp), m_jobs(jobs) {
    m_substeps = std::clamp(substeps, 1, 16);
    if (const char* e = std::getenv("KKE_CLOTH_GPU_CHECK")) m_gpuCheck = *e == '1';
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
    c.iterations = std::max(1, d.fabric.iterations);
    cs.mNumIterations = uint32_t((c.iterations + m_sub - 1) / m_sub);
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
    buildPatches(c);
    c.stats.vertices = uint32_t(n);
    c.stats.triangles = uint32_t(c.tris.size() / 3);

    auto& stored = m_cloths.emplace(id, std::move(c)).first->second;
    // Jolt needs a skinned pose before the first step, and a hard one:
    // its previous skinned positions start out as NaN.
    if (stored.skinned) setJoints(id, stored.bindPose, true);
    return id;
}

// Hair: every guide strand a chain of vertices held by distance
// constraints (stretch along it, bend across two and three segments). The
// root and the follicle vertex are skinned hard to the head, so the strand
// leaves the scalp the way it grows; the rest may swing as far as `hold`
// lets them from their styled place.
uint32_t ClothSystem::addHair(const HairDesc& d) {
    const size_t per = size_t(hairStrandVertices(d.style));
    const std::vector<glm::vec3> rest = hairRestPose(d);
    const size_t guides = rest.size() / per;
    if (d.roots.empty() || guides == 0) return 0;
    // Parts of neighbouring guides (hairScalp lists them crown to nape):
    // each part is its own soft body, so Jolt steps them on separate
    // threads, and their bounds stay tight.
    std::vector<uint32_t> parts;
    const size_t count = (guides + kHairPart - 1) / kHairPart;
    for (size_t k = 0; k < count; ++k) {
        const size_t first = k * guides / count, last = (k + 1) * guides / count;
        const uint32_t part = addHairPart(d, rest, first, last - first);
        if (part) parts.push_back(part);
    }
    if (parts.empty()) return 0;
    const uint32_t id = m_next++;
    m_hairs.emplace(id, std::move(parts));
    return id;
}

uint32_t ClothSystem::addHairPart(const HairDesc& d, const std::vector<glm::vec3>& all, size_t firstGuide, size_t guideCount) {
    const HairStyle& st = d.style;
    const uint32_t per = uint32_t(hairStrandVertices(st));
    const std::vector<glm::vec3> rest(all.begin() + long(firstGuide * per), all.begin() + long((firstGuide + guideCount) * per));
    const size_t n = rest.size();
    const uint32_t guides = uint32_t(guideCount);
    Cloth c;
    c.hair = true;
    c.strandVerts = per;
    c.level = ClothProtection::Basic; // strands don't collide with each other (kke/Hair.h)
    c.fabric.name = "hair";
    c.fabric.airDrag = st.airDrag;
    c.hairWidth = st.width;
    c.wind = std::max(0.0f, d.wind);
    c.thickness = st.thickness;
    c.rest = rest;
    c.bindPose = { d.bindPose };

    glm::vec3 centroid(0.0f);
    for (const glm::vec3& p : rest) centroid += p;
    centroid /= float(n);

    // Lumped masses: half of each neighbouring segment's length.
    std::vector<float> mass(n, 0.0f);
    std::vector<float> arc(n, 0.0f); // along the strand from the follicle
    for (uint32_t g = 0; g < guides; ++g) {
        const uint32_t b = g * per;
        for (uint32_t k = 0; k + 1 < per; ++k) {
            const float len = glm::length(rest[b + k + 1] - rest[b + k]);
            mass[b + k] += 0.5f * len * st.density;
            mass[b + k + 1] += 0.5f * len * st.density;
            if (k >= 1) arc[b + k + 1] = arc[b + k] + len;
        }
    }
    float meanMass = 0.0f;
    for (float& m : mass) {
        m = std::max(m, 1e-7f);
        meanMass += m;
    }
    meanMass /= float(n);

    JPH::Ref<JPH::SoftBodySharedSettings> s = new JPH::SoftBodySharedSettings;
    s->mVertices.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 local = rest[i] - centroid;
        const bool pinned = i % per < 2;
        s->mVertices[i] = JPH::SoftBodySharedSettings::Vertex(JPH::Float3(local.x, local.y, local.z), JPH::Float3(0, 0, 0), pinned ? 0.0f : 1.0f / mass[i]);
    }
    // Softness -> XPBD compliance as for Fabric: one sub-step fixes
    // 2 / (2 + softness) of the error. Roots are stiffer than tips.
    // Stretch: each segment. Bend: across two segments (and three in a
    // curl, which holds a spiral's turn). Distances only, no orientation:
    // Jolt's Cosserat rods were tried first, but nothing turns the first
    // rod's twist with the head (Jolt has no way to set a rod's
    // orientation), so a quick head turn could flip a strand's rest curve
    // from down to up.
    const float sub = kAssumedStep / float(std::max(1, st.iterations));
    const float scale = sub * sub / meanMass;
    const float stiffRoot = std::clamp(st.stiffRoot, 0.0f, 1.0f);
    const float pulledOut = 1.0f / (1.0f - std::clamp(st.shrinkage, 0.0f, 0.9f));
    for (uint32_t g = 0; g < guides; ++g) {
        const uint32_t b = g * per;
        for (uint32_t k = 0; k + 1 < per; ++k) {
            const float along = per > 3 ? float(k) / float(per - 3) : 1.0f;
            const float bend = st.bend * glm::mix(stiffRoot, 1.0f, std::min(along, 1.0f)) * scale;
            if (k >= 1) s->mEdgeConstraints.emplace_back(b + k, b + k + 1, st.stretch * scale); // root to follicle: both on the head
            if (k + 2 < per) s->mEdgeConstraints.emplace_back(b + k, b + k + 2, bend);
            if (st.curl > 0.0f && k + 3 < per) s->mEdgeConstraints.emplace_back(b + k, b + k + 3, bend);
        }
        // Tethers from the follicle: never longer than maxStretch x the
        // length along the strand (a curl may be pulled out, not through),
        // or, for shrinking coils, than the hair pulled straight.
        for (uint32_t k = 2; k < per; ++k)
            s->mLRAConstraints.emplace_back(b + 1, b + k, std::max(1.0f, st.maxStretch) * pulledOut * arc[b + k]);
    }
    s->CalculateEdgeLengths();

    const glm::mat4 toLocal = glm::translate(glm::mat4(1.0f), -centroid);
    s->mInvBindMatrices.emplace_back(0u, toJ(glm::inverse(toLocal * d.bindPose)));
    const float hold = std::clamp(st.hold, 0.0f, 1.0f);
    for (uint32_t i = 0; i < n; ++i) {
        JPH::SoftBodySharedSettings::Skinned sk;
        sk.mVertex = i;
        if (i % per < 2) {
            sk.mMaxDistance = 0.0f;
        } else if (hold > 0.0f) {
            sk.mMaxDistance = std::max(arc[i] * (1.0f - hold), st.thickness);
        } else {
            continue;
        }
        sk.mWeights[0] = JPH::SoftBodySharedSettings::SkinWeight(0, 1.0f);
        s->mSkinnedConstraints.push_back(sk);
    }
    c.skinned = true;
    s->Optimize();

    JPH::SoftBodyCreationSettings cs(s, JPH::RVec3(centroid.x, centroid.y, centroid.z), JPH::Quat::sIdentity(), m_layer);
    c.iterations = std::max(1, st.iterations);
    cs.mNumIterations = uint32_t((c.iterations + m_sub - 1) / m_sub);
    cs.mLinearDamping = st.damping;
    cs.mFriction = st.friction;
    cs.mGravityFactor = st.gravity;
    cs.mVertexRadius = st.thickness;
    const uint32_t id = m_next++;
    cs.mUserData = id;
    cs.mAllowSleeping = true;
    JPH::BodyInterface& bi = m_system.GetBodyInterface();
    c.body = bi.CreateAndAddSoftBody(cs, JPH::EActivation::Activate);
    if (c.body.IsInvalid()) return 0;
    c.stats.vertices = uint32_t(n);
    m_cloths.emplace(id, std::move(c));
    setJoints(id, { d.bindPose }, true);
    return id;
}

size_t ClothSystem::clothCount() const {
    return size_t(std::count_if(m_cloths.begin(), m_cloths.end(), [](const auto& e) { return !e.second.hair; }));
}

bool ClothSystem::hairPositions(uint32_t id, std::vector<glm::vec3>& out) const {
    auto it = m_hairs.find(id);
    if (it == m_hairs.end()) return false;
    out.clear();
    std::vector<glm::vec3> part;
    for (uint32_t p : it->second) {
        if (!positions(p, part)) return false;
        out.insert(out.end(), part.begin(), part.end());
    }
    return true;
}

void ClothSystem::setHairJoint(uint32_t id, const glm::mat4& head) {
    auto it = m_hairs.find(id);
    if (it == m_hairs.end()) return;
    for (uint32_t p : it->second) setJoints(p, { head });
}

void ClothSystem::resetHair(uint32_t id) {
    auto it = m_hairs.find(id);
    if (it == m_hairs.end()) return;
    for (uint32_t p : it->second) reset(p);
}

void ClothSystem::removeHair(uint32_t id) {
    auto it = m_hairs.find(id);
    if (it == m_hairs.end()) return;
    for (uint32_t p : it->second) remove(p);
    m_hairs.erase(it);
}

HairStats ClothSystem::hairStats(uint32_t id) const {
    auto it = m_hairs.find(id);
    if (it == m_hairs.end()) return {};
    HairStats h;
    h.sleeping = true;
    for (uint32_t p : it->second) {
        auto c = m_cloths.find(p);
        if (c == m_cloths.end()) continue;
        h.vertices += c->second.stats.vertices;
        h.guides += c->second.strandVerts ? c->second.stats.vertices / c->second.strandVerts : 0;
        h.sleeping = h.sleeping && !m_system.GetBodyInterface().IsActive(c->second.body);
    }
    return h;
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
    if (c.hair) return; // hair has one level (kke/Hair.h)
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

// Patches: triangles grown breadth first from a seed across shared edges
// until there are kPatchTriangles, so each is a small, compact piece of
// the surface (a disc a few edges across).
void ClothSystem::buildPatches(Cloth& c) {
    const uint32_t nt = uint32_t(c.tris.size() / 3);
    c.triPatch.assign(nt, UINT32_MAX);
    c.patches = 0;
    if (nt == 0) return;
    // Triangles across each edge (edge key: lower vertex, higher vertex).
    std::vector<std::pair<uint64_t, uint32_t>> byEdge;
    byEdge.reserve(size_t(nt) * 3);
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = c.tris[size_t(t) * 3 + size_t(k)], b = c.tris[size_t(t) * 3 + size_t((k + 1) % 3)];
            byEdge.push_back({ uint64_t(std::min(a, b)) << 32 | std::max(a, b), t });
        }
    std::sort(byEdge.begin(), byEdge.end());
    std::vector<uint32_t> nearStart(nt + 1, 0), near;
    {
        std::vector<std::vector<uint32_t>> lists(nt);
        for (size_t i = 0; i < byEdge.size();) {
            size_t j = i;
            while (j < byEdge.size() && byEdge[j].first == byEdge[i].first) ++j;
            for (size_t a = i; a < j; ++a)
                for (size_t b = i; b < j; ++b)
                    if (a != b) lists[byEdge[a].second].push_back(byEdge[b].second);
            i = j;
        }
        for (uint32_t t = 0; t < nt; ++t) {
            nearStart[t + 1] = nearStart[t] + uint32_t(lists[t].size());
            near.insert(near.end(), lists[t].begin(), lists[t].end());
        }
    }
    std::vector<uint32_t> queue;
    for (uint32_t seed = 0; seed < nt; ++seed) {
        if (c.triPatch[seed] != UINT32_MAX) continue;
        const uint32_t patch = c.patches++;
        queue.assign(1, seed);
        c.triPatch[seed] = patch;
        uint32_t size = 1;
        for (size_t q = 0; q < queue.size() && size < kPatchTriangles; ++q)
            for (uint32_t k = nearStart[queue[q]]; k < nearStart[queue[q] + 1] && size < kPatchTriangles; ++k) {
                const uint32_t o = near[k];
                if (c.triPatch[o] != UINT32_MAX) continue;
                c.triPatch[o] = patch;
                queue.push_back(o);
                ++size;
            }
    }
    // Neighbouring patches: sharing an edge.
    std::vector<std::vector<uint32_t>> patchNear(c.patches);
    for (uint32_t t = 0; t < nt; ++t)
        for (uint32_t k = nearStart[t]; k < nearStart[t + 1]; ++k)
            if (c.triPatch[near[k]] != c.triPatch[t]) patchNear[c.triPatch[t]].push_back(c.triPatch[near[k]]);
    c.patchNearStart.assign(c.patches + 1, 0);
    c.patchNear.clear();
    for (uint32_t p = 0; p < c.patches; ++p) {
        auto& l = patchNear[p];
        std::sort(l.begin(), l.end());
        l.erase(std::unique(l.begin(), l.end()), l.end());
        c.patchNearStart[p + 1] = c.patchNearStart[p] + uint32_t(l.size());
        c.patchNear.insert(c.patchNear.end(), l.begin(), l.end());
    }
    // Each vertex and edge goes with the patch of one triangle it is part of:
    // it lies in that patch's surface, so what holds for the patch holds for it.
    c.vertPatch.assign(c.rest.size(), UINT32_MAX); // UINT32_MAX: in no triangle (a net's threads)
    for (uint32_t t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) c.vertPatch[c.tris[size_t(t) * 3 + size_t(k)]] = c.triPatch[t];
    c.edgePatch.assign(c.edges.size() / 2, 0);
    for (size_t e = 0; e * 2 + 1 < c.edges.size(); ++e) {
        const uint64_t key = uint64_t(std::min(c.edges[e * 2], c.edges[e * 2 + 1])) << 32 | std::max(c.edges[e * 2], c.edges[e * 2 + 1]);
        const auto it = std::lower_bound(byEdge.begin(), byEdge.end(), std::make_pair(key, uint32_t(0)));
        if (it != byEdge.end() && it->first == key) c.edgePatch[e] = c.triPatch[it->second];
    }
}

namespace {

// The narrowest cone (axis, half-angle) holding both cones.
glm::vec4 mergeCones(const glm::vec4& a, const glm::vec4& b) {
    if (a.w >= glm::pi<float>() || b.w >= glm::pi<float>()) return glm::vec4(0, 0, 1, glm::pi<float>());
    const glm::vec3 aa(a), ba(b);
    const float between = std::acos(std::clamp(glm::dot(aa, ba), -1.0f, 1.0f));
    if (between + b.w <= a.w) return a;
    if (between + a.w <= b.w) return b;
    const float half = 0.5f * (a.w + b.w + between);
    if (half >= glm::pi<float>() || between < 1e-6f) return glm::vec4(aa, std::min(half, glm::pi<float>()));
    const float t = half - a.w; // turn a's axis this far towards b's
    const glm::vec3 axis = (std::sin(between - t) * aa + std::sin(t) * ba) / std::sin(between);
    return glm::vec4(glm::normalize(axis), half);
}

} // namespace

// Which pairs of patches could touch this step (the rest of the pass
// looks only at those). Pairs whose swept bounds don't meet can't. And a
// patch, or two neighbouring patches, or two with a patch between them,
// whose triangle normals all stay within kFlatEnough of one direction
// for the whole step (at its start, its end, and the cross term between,
// which bounds every normal in between) is a connected, gently curved
// piece of cloth: it can't pass through itself (Volino and
// Magnenat-Thalmann 1994, Provot 1997). Only small pieces are skipped this
// way: a whole scarf lying in a flat loop, end over start, has all its
// normals pointing up and can still pass through itself, so patches far
// apart on the same cloth are always looked at when their bounds meet.
void ClothSystem::markPatches() {
    m_patchBase.resize(m_active.size() + 1);
    m_patchBase[0] = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) m_patchBase[ci + 1] = m_patchBase[ci] + m_active[ci]->patches;
    const uint32_t total = m_patchBase.back();
    m_patchTotal = total;
    m_patchLo.assign(total, glm::vec3(FLT_MAX));
    m_patchHi.assign(total, glm::vec3(-FLT_MAX));
    m_patchCone.assign(total, glm::vec4(0.0f));
    m_patchLooked.assign(total, 0);
    m_allPairs = total > kMaxPatchBits;
    if (!m_allPairs) m_patchPairs.assign((size_t(total) * total + 63) / 64, 0);
    std::vector<glm::vec3>& sum = m_patchSum;
    sum.assign(total, glm::vec3(0.0f));
    std::vector<float>& lowest = m_patchLowest;
    lowest.assign(total, 1.0f);
    // Bounds, and the sum of every normal the step's triangles have: at its
    // start, its end, and the cross term of the two (the normal in between
    // is a positive mix of the three, so a cone holding them holds it).
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        const size_t nt = c.tris.size() / 3;
        c.triMixed.resize(nt);
        for (size_t t = 0; t < nt; ++t) {
            const uint32_t gp = m_patchBase[ci] + c.triPatch[t];
            const uint32_t i0 = c.tris[t * 3], i1 = c.tris[t * 3 + 1], i2 = c.tris[t * 3 + 2];
            glm::vec3 lo = glm::min(glm::min(c.pos[i0], c.prev[i0]), glm::min(glm::min(c.pos[i1], c.prev[i1]), glm::min(c.pos[i2], c.prev[i2])));
            glm::vec3 hi = glm::max(glm::max(c.pos[i0], c.prev[i0]), glm::max(glm::max(c.pos[i1], c.prev[i1]), glm::max(c.pos[i2], c.prev[i2])));
            m_patchLo[gp] = glm::min(m_patchLo[gp], lo);
            m_patchHi[gp] = glm::max(m_patchHi[gp], hi);
            const glm::vec3 mixed = glm::cross(c.prev[i1] - c.prev[i0], c.pos[i2] - c.pos[i0]) + glm::cross(c.pos[i1] - c.pos[i0], c.prev[i2] - c.prev[i0]);
            const float ml = glm::length(mixed);
            if (c.triN[t] == glm::vec3(0.0f) || ml < 1e-12f) {
                lowest[gp] = -2.0f; // degenerate: no cone
                c.triMixed[t] = glm::vec3(0.0f);
                continue;
            }
            c.triMixed[t] = mixed / ml;
            sum[gp] += c.triN[t] + c.triNPrev[t] + c.triMixed[t];
        }
        for (uint32_t p = 0; p < c.patches; ++p) {
            const uint32_t gp = m_patchBase[ci] + p;
            m_patchLo[gp] -= c.thickness;
            m_patchHi[gp] += c.thickness;
            const float l = glm::length(sum[gp]);
            sum[gp] = l > 1e-6f ? sum[gp] / l : glm::vec3(0.0f);
            if (l <= 1e-6f) lowest[gp] = -2.0f;
        }
        // Cones: the axis the mean normal, the half-angle the widest normal from it.
        for (size_t t = 0; t < nt; ++t) {
            const uint32_t gp = m_patchBase[ci] + c.triPatch[t];
            const glm::vec3 axis = sum[gp];
            lowest[gp] = std::min(lowest[gp], std::min(std::min(glm::dot(c.triN[t], axis), glm::dot(c.triNPrev[t], axis)), glm::dot(c.triMixed[t], axis)));
        }
    }
    for (uint32_t gp = 0; gp < total; ++gp)
        m_patchCone[gp] = lowest[gp] < -1.5f ? glm::vec4(0, 0, 1, glm::pi<float>()) : glm::vec4(sum[gp], std::acos(std::clamp(lowest[gp], -1.0f, 1.0f)));
    auto look = [&](uint32_t a, uint32_t b) {
        m_patchLooked[a] = m_patchLooked[b] = 1;
        if (m_allPairs) return;
        for (const uint64_t bit : { uint64_t(a) * total + b, uint64_t(b) * total + a }) m_patchPairs[bit >> 6] |= uint64_t(1) << (bit & 63);
    };
    // Sweep along x over every patch's bounds; pairs whose bounds meet.
    thread_local std::vector<uint32_t> order;
    order.resize(total);
    for (uint32_t i = 0; i < total; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return m_patchLo[a].x < m_patchLo[b].x; });
    std::vector<uint32_t> clothOf(total);
    for (size_t ci = 0; ci < m_active.size(); ++ci)
        for (uint32_t gp = m_patchBase[ci]; gp < m_patchBase[ci + 1]; ++gp) clothOf[gp] = uint32_t(ci);
    for (size_t i = 0; i < total; ++i) {
        const uint32_t a = order[i];
        // A patch on its own.
        if (m_patchCone[a].w >= kFlatEnough) look(a, a);
        for (size_t j = i + 1; j < total && m_patchLo[order[j]].x <= m_patchHi[a].x; ++j) {
            const uint32_t b = order[j];
            if (glm::any(glm::lessThan(m_patchHi[a], m_patchLo[b])) || glm::any(glm::lessThan(m_patchHi[b], m_patchLo[a]))) continue;
            if (clothOf[a] == clothOf[b]) {
                const Cloth& c = *m_active[clothOf[a]];
                const uint32_t base = m_patchBase[clothOf[a]], la = a - base, lb = b - base;
                const uint32_t* na0 = c.patchNear.data() + c.patchNearStart[la];
                const uint32_t* na1 = c.patchNear.data() + c.patchNearStart[la + 1];
                const glm::vec4 both = mergeCones(m_patchCone[a], m_patchCone[b]);
                if (both.w < kFlatEnough) {
                    // Neighbours, flat enough together.
                    if (std::binary_search(na0, na1, lb)) continue;
                    // A patch between them, the three flat enough together.
                    bool flat = false;
                    const uint32_t* nb0 = c.patchNear.data() + c.patchNearStart[lb];
                    const uint32_t* nb1 = c.patchNear.data() + c.patchNearStart[lb + 1];
                    for (const uint32_t* q = na0; q < na1 && !flat; ++q)
                        if (std::binary_search(nb0, nb1, *q) && mergeCones(both, m_patchCone[base + *q]).w < kFlatEnough) flat = true;
                    if (flat) continue;
                }
            }
            look(a, b);
        }
    }
    // Vertices in no triangle (a net's threads) are always looked at, and
    // so is every patch within their reach.
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        const Cloth& c = *m_active[ci];
        glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
        for (size_t v = 0; v < c.pos.size(); ++v)
            if (c.vertPatch[v] == UINT32_MAX) {
                lo = glm::min(lo, glm::min(c.pos[v], c.prev[v]) - c.meanEdge - 3.0f * c.thickness);
                hi = glm::max(hi, glm::max(c.pos[v], c.prev[v]) + c.meanEdge + 3.0f * c.thickness);
            }
        if (lo.x > hi.x) continue;
        for (uint32_t gp = 0; gp < total; ++gp)
            if (!glm::any(glm::lessThan(m_patchHi[gp], lo)) && !glm::any(glm::lessThan(hi, m_patchLo[gp]))) m_patchLooked[gp] = 1;
    }
    // Vertices of any patch looked at.
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        c.vertLooked.assign(c.pos.size(), 0);
        for (size_t v = 0; v < c.pos.size(); ++v)
            if (c.vertPatch[v] == UINT32_MAX) c.vertLooked[v] = 1;
        for (size_t t = 0; t < c.tris.size() / 3; ++t)
            if (m_patchLooked[m_patchBase[ci] + c.triPatch[t]])
                for (int k = 0; k < 3; ++k) c.vertLooked[c.tris[t * 3 + size_t(k)]] = 1;
    }
}

void ClothSystem::shapeTriangles() {
    for (Cloth* cp : m_active) {
        Cloth& c = *cp;
        const size_t nt = c.tris.size() / 3;
        c.triN.resize(nt);
        c.triNPrev.resize(nt);
        c.triSphere.resize(nt);
        c.triMove.resize(nt);
        for (size_t t = 0; t < nt; ++t) {
            const uint32_t i0 = c.tris[t * 3], i1 = c.tris[t * 3 + 1], i2 = c.tris[t * 3 + 2];
            const glm::vec3 n = glm::cross(c.pos[i1] - c.pos[i0], c.pos[i2] - c.pos[i0]);
            const glm::vec3 np = glm::cross(c.prev[i1] - c.prev[i0], c.prev[i2] - c.prev[i0]);
            const float nl = glm::length(n), npl = glm::length(np);
            c.triN[t] = nl > 1e-12f ? n / nl : glm::vec3(0.0f);
            c.triNPrev[t] = npl > 1e-12f ? np / npl : c.triN[t];
            // Bounding sphere for a cheap reject: about the centroid, holding
            // the triangle now and at the last pass (and so all the way
            // between, seen from its moving centroid), and how far the
            // centroid moved: only moving relative to it brings a vertex closer.
            const glm::vec3 centre = (c.pos[i0] + c.pos[i1] + c.pos[i2]) / 3.0f;
            const glm::vec3 centrePrev = (c.prev[i0] + c.prev[i1] + c.prev[i2]) / 3.0f;
            float r2 = 0.0f;
            for (const uint32_t i : { i0, i1, i2 })
                r2 = std::max(r2, std::max(glm::dot(c.pos[i] - centre, c.pos[i] - centre), glm::dot(c.prev[i] - centrePrev, c.prev[i] - centrePrev)));
            c.triSphere[t] = glm::vec4(centre, std::sqrt(r2));
            c.triMove[t] = centre - centrePrev;
        }
    }
}

// Runs fn over [0, count) in chunks, on Jolt's job system when there's
// one, else right here. (From OnStep this runs inside one of Jolt's jobs;
// waiting on a barrier there is fine: the waiting thread runs the
// barrier's jobs itself.)
void ClothSystem::parallel(uint32_t count, const std::function<void(uint32_t, uint32_t, Worker&)>& fn) {
    const int threads = m_jobs ? std::max(1, m_jobs->GetMaxConcurrency()) : 1;
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

    // --- Broad phase, once per step: who could touch whom ------------------
    // Everything's bounds swept from the last pass to now (+ thickness), in
    // a spatial hash; the pairs whose bounds meet are kept for the passes.
    shapeTriangles();
    markPatches();
    m_triBox.resize(triTotal);
    m_triCloth.resize(triTotal);
    m_triLo.resize(triTotal);
    m_triHi.resize(triTotal);
    double sized = 0.0;
    size_t boxes = 0;
    size_t g = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        c.triBase = uint32_t(g);
        for (size_t t = 0; t < c.tris.size() / 3; ++t, ++g) {
            CellBox& box = m_triBox[g];
            m_triCloth[g] = uint32_t(ci);
            if (!m_patchLooked[m_patchBase[ci] + c.triPatch[t]]) {
                box = CellBox{};
                continue;
            }
            const uint32_t i0 = c.tris[t * 3], i1 = c.tris[t * 3 + 1], i2 = c.tris[t * 3 + 2];
            if (c.triN[t] == glm::vec3(0.0f)) {
                box = CellBox{};
                continue;
            }
            m_triLo[g] = glm::min(glm::min(glm::min(c.pos[i0], c.prev[i0]), glm::min(c.pos[i1], c.prev[i1])), glm::min(c.pos[i2], c.prev[i2])) - c.thickness;
            m_triHi[g] = glm::max(glm::max(glm::max(c.pos[i0], c.prev[i0]), glm::max(c.pos[i1], c.prev[i1])), glm::max(c.pos[i2], c.prev[i2])) + c.thickness;
            box.lo = glm::ivec3(0);
            box.hi = glm::ivec3(0); // valid for now: placed below
            {
                const glm::vec3 ext = m_triHi[g] - m_triLo[g];
                sized += std::max(ext.x, std::max(ext.y, ext.z));
            }
            ++boxes;
        }
    }
    // Cells about as big as the boxes (a box then covers a few cells, and a
    // cell holds a few boxes); cloth moving fast gets bigger cells.
    const float triCell = std::max(cell, boxes ? float(sized / double(boxes)) : cell);
    const float inv = 1.0f / triCell;
    for (size_t gt = 0; gt < triTotal; ++gt)
        if (m_triBox[gt].valid() && !cellBox(m_triLo[gt], m_triHi[gt], inv, m_triBox[gt].lo, m_triBox[gt].hi)) m_triBox[gt] = CellBox{};
    m_triGrid.build(m_triBox);
    m_vertBase.resize(m_active.size() + 1);
    m_vertBase[0] = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) m_vertBase[ci + 1] = m_vertBase[ci] + uint32_t(m_active[ci]->pos.size());
    // The cells each vertex looks in: its bounds swept from the last pass
    // to now (+ thickness). Pinned and asleep vertices don't look (others
    // are pushed off them, from their side).
    m_vertBox.assign(m_vertBase.back(), CellBox{});
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        const Cloth& c = *m_active[ci];
        for (uint32_t v = 0; v < c.pos.size(); ++v) {
            if (c.invMass[v] <= 0.0f || !c.vertLooked[v]) continue;
            CellBox& box = m_vertBox[m_vertBase[ci] + v];
            if (!cellBox(glm::min(c.pos[v], c.prev[v]) - c.thickness, glm::max(c.pos[v], c.prev[v]) + c.thickness, inv, box.lo, box.hi)) box = CellBox{};
        }
    }
    // Edges: two sheets sliding over each other can pass edge through edge
    // with no vertex ever going through a triangle. Their bounds, for both
    // searches.
    m_edgeBox.resize(edgeTotal);
    m_edgeCloth.resize(edgeTotal);
    m_edgeLo.resize(edgeTotal);
    m_edgeHi.resize(edgeTotal);
    m_edgeMoves.resize(edgeTotal);
    m_edgePatch.resize(edgeTotal);
    m_edgeLooked.assign(edgeTotal, 0);
    g = 0;
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        Cloth& c = *m_active[ci];
        c.edgeBase = uint32_t(g);
        for (size_t k = 0; k + 1 < c.edges.size(); k += 2, ++g) {
            const uint32_t i0 = c.edges[k], i1 = c.edges[k + 1];
            m_edgeCloth[g] = uint32_t(ci);
            m_edgePatch[g] = m_patchBase[ci] + c.edgePatch[k / 2];
            m_edgeMoves[g] = 0;
            if (!m_patchLooked[m_edgePatch[g]]) continue;
            m_edgeLo[g] = glm::min(glm::min(c.pos[i0], c.prev[i0]), glm::min(c.pos[i1], c.prev[i1])) - c.thickness;
            m_edgeHi[g] = glm::max(glm::max(c.pos[i0], c.prev[i0]), glm::max(c.pos[i1], c.prev[i1])) + c.thickness;
            m_edgeMoves[g] = uint8_t(c.invMass[i0] > 0.0f || c.invMass[i1] > 0.0f ? 1 : 0);
            m_edgeLooked[g] = 1;
        }
    }
    // The GPU if there is one (the CPU if it can't answer, or to check it).
    const bool gpu = m_gpu && searchOnGpu(triTotal, edgeTotal, cell);
    if (!gpu || m_gpuCheck) {
        std::vector<VtPair> gpuVt;
        std::vector<EePair> gpuEe;
        if (gpu) {
            gpuVt.swap(m_vtPairs);
            gpuEe.swap(m_eePairs);
        }
        searchOnCpu(triTotal, edgeTotal, cell);
        if (gpu) {
            auto vtKey = [](const VtPair& a) { return (uint64_t(a.cloth) << 58) ^ (uint64_t(a.vertex) << 29) ^ a.tri; };
            size_t vtMissing = 0, vtExtra = 0, eeMissing = 0, eeExtra = 0;
            {
                std::vector<uint64_t> a, b, d;
                for (const VtPair& x : m_vtPairs) a.push_back(vtKey(x));
                for (const VtPair& x : gpuVt) b.push_back(vtKey(x));
                std::sort(a.begin(), a.end());
                std::sort(b.begin(), b.end());
                std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
                vtMissing = d.size();
                d.clear();
                std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(d));
                vtExtra = d.size();
                a.clear();
                b.clear();
                d.clear();
                for (const EePair& x : m_eePairs) a.push_back((uint64_t(x.a) << 32) | x.b);
                for (const EePair& x : gpuEe) b.push_back((uint64_t(x.a) << 32) | x.b);
                std::sort(a.begin(), a.end());
                std::sort(b.begin(), b.end());
                std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
                eeMissing = d.size();
                d.clear();
                std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(d));
                eeExtra = d.size();
            }
            ++m_gpuChecks;
            if (vtMissing + vtExtra + eeMissing + eeExtra > 0) ++m_gpuDiffered;
            if (m_gpuChecks % 60 == 1 || vtMissing + vtExtra + eeMissing + eeExtra > 0)
                log::get("ClothGpu")->info("check {}: GPU {} vertex-triangle and {} edge pairs, CPU {} and {}; the GPU missed {} and {}, found {} and {} more "
                                           "({} of {} searches differed)",
                                           m_gpuChecks, gpuVt.size(), gpuEe.size(), m_vtPairs.size(), m_eePairs.size(), vtMissing, eeMissing, vtExtra,
                                           eeExtra, m_gpuDiffered, m_gpuChecks);
            // Go on with the GPU's answer: that is the one being checked.
            m_vtPairs.swap(gpuVt);
            m_eePairs.swap(gpuEe);
        }
    }

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
            // Too far from the triangle, for how far it moved relative to it:
            // can't touch or have crossed it.
            const glm::vec4& sph = o.triSphere[tri];
            const float reach = sph.w + std::max(c.thickness, o.thickness) + glm::length(c.pos[pr.vertex] - c.prev[pr.vertex] - o.triMove[tri]);
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

// The broad phase's queries on the CPU (on Jolt's job system): vertices
// against the triangles' hash, then the edges the vertices found near
// another surface against each other.
void ClothSystem::searchOnCpu(size_t triTotal, size_t edgeTotal, float cell) {
    parallel(m_vertBase.back(), [&](uint32_t begin, uint32_t end, Worker& w) {
        w.stamp.assign(triTotal, UINT32_MAX);
        w.vt.clear();
        size_t ci = size_t(std::upper_bound(m_vertBase.begin(), m_vertBase.end(), begin) - m_vertBase.begin()) - 1;
        for (uint32_t gv = begin; gv < end; ++gv) {
            while (gv >= m_vertBase[ci + 1]) ++ci;
            Cloth& c = *m_active[ci];
            const uint32_t v = gv - m_vertBase[ci];
            if (c.invMass[v] <= 0.0f) continue; // pinned or asleep: others are pushed off it, from their side
            if (!c.vertLooked[v]) continue;
            const bool loose = c.vertPatch[v] == UINT32_MAX;
            const uint32_t vp = loose ? 0u : m_patchBase[ci] + c.vertPatch[v];
            if (!m_vertBox[gv].valid()) continue;
            const glm::ivec3 a = m_vertBox[gv].lo, b = m_vertBox[gv].hi;
            const size_t found = w.vt.size();
            for (int x = a.x; x <= b.x; ++x)
                for (int y = a.y; y <= b.y; ++y)
                    for (int z = a.z; z <= b.z; ++z) {
                        const uint32_t h = hashCell(x, y, z) & m_triGrid.mask;
                        for (uint32_t e = m_triGrid.start[h]; e < m_triGrid.start[h + 1]; ++e) {
                            const uint32_t gt = m_triGrid.items[e];
                            // Seen from the first cell both boxes cover (and only
                            // for its box, not for another cell sharing the hash).
                            if (!firstShared(glm::ivec3(x, y, z), a, m_triBox[gt])) continue;
                            if (w.stamp[gt] == gv) continue; // in the bucket twice
                            w.stamp[gt] = gv;
                            Cloth& o = *m_active[m_triCloth[gt]];
                            const uint32_t tri = gt - o.triBase;
                            if (&o == &c && nearInTopology(c, v, tri)) continue;
                            // Far from the triangle for how far it moved relative to it
                            // (with room for the passes to move things): skip.
                            const glm::vec4& sph = o.triSphere[tri];
                            const float reach = sph.w + 3.0f * std::max(c.thickness, o.thickness) + glm::length(c.pos[v] - c.prev[v] - o.triMove[tri]);
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
                                    setFlag(*flag);
                            }
                            if (d2 > reach * reach) continue;
                            // (The flags above are set whatever the patches: an edge of a
                            // patch looked at may end at a vertex of one that isn't.)
                            if (!loose && !patchPair(vp, m_patchBase[m_triCloth[gt]] + o.triPatch[tri])) continue;
                            w.vt.push_back({ uint32_t(ci), v, gt });
                        }
                    }
            // In triangle order (as the GPU's answer is sorted): the order the
            // narrow phase fixes them in.
            std::sort(w.vt.begin() + long(found), w.vt.end(), [](const VtPair& p, const VtPair& q) { return p.tri < q.tri; });
        }
    });
    m_vtPairs.clear();
    for (const Worker& w : m_workers) m_vtPairs.insert(m_vtPairs.end(), w.vt.begin(), w.vt.end());

    // Only edges near another surface can meet another edge (the vertex
    // queries flagged them): the rest stay out of the hash.
    double edgeSized = 0.0;
    size_t edgeBoxes = 0;
    for (size_t ge = 0; ge < edgeTotal; ++ge) {
        m_edgeBox[ge] = CellBox{};
        if (!m_edgeLooked[ge]) continue;
        const Cloth& c = *m_active[m_edgeCloth[ge]];
        const uint32_t* ends = &c.edges[size_t(ge - c.edgeBase) * 2];
        if (c.nearOther[ends[0]] || c.nearOther[ends[1]]) m_edgeMoves[ge] |= 2;
        if (m_edgeMoves[ge] & 2) {
            m_edgeBox[ge].lo = m_edgeBox[ge].hi = glm::ivec3(0); // placed below
            const glm::vec3 ext = m_edgeHi[ge] - m_edgeLo[ge];
            edgeSized += std::max(ext.x, std::max(ext.y, ext.z));
            ++edgeBoxes;
        }
    }
    const float edgeInv = 1.0f / std::max(cell, edgeBoxes ? float(edgeSized / double(edgeBoxes)) : cell);
    for (size_t ge = 0; ge < edgeTotal; ++ge)
        if (m_edgeBox[ge].valid() && !cellBox(m_edgeLo[ge], m_edgeHi[ge], edgeInv, m_edgeBox[ge].lo, m_edgeBox[ge].hi)) m_edgeBox[ge] = CellBox{};
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
            const size_t found = w.ee.size();
            for (int x = b.lo.x; x <= b.hi.x; ++x)
                for (int y = b.lo.y; y <= b.hi.y; ++y)
                    for (int z = b.lo.z; z <= b.hi.z; ++z) {
                        const uint32_t h = hashCell(x, y, z) & m_edgeGrid.mask;
                        for (uint32_t k = m_edgeGrid.start[h]; k < m_edgeGrid.start[h + 1]; ++k) {
                            const uint32_t gf = m_edgeGrid.items[k];
                            if (!firstShared(glm::ivec3(x, y, z), b.lo, m_edgeBox[gf])) continue;
                            // Each pair once: from the lower edge when both move.
                            if ((m_edgeMoves[gf] == 3 && gf <= ge) || w.stamp[gf] == ge) continue;
                            w.stamp[gf] = ge;
                            if (glm::any(glm::lessThan(m_edgeHi[gf], m_edgeLo[ge])) || glm::any(glm::greaterThan(m_edgeLo[gf], m_edgeHi[ge]))) continue;
                            if (!patchPair(m_edgePatch[ge], m_edgePatch[gf])) continue;
                            Cloth& o = *m_active[m_edgeCloth[gf]];
                            const uint32_t f = gf - o.edgeBase;
                            if (edgesFar(c, e, o, f, 3.0f * std::max(c.thickness, o.thickness))) continue;
                            if (&o == &c && edgesNear(c, e, f)) continue;
                            if (edgesApart(c, e, o, f, 3.0f * std::max(c.thickness, o.thickness))) continue;
                            w.ee.push_back({ ge, gf });
                        }
                    }
            std::sort(w.ee.begin() + long(found), w.ee.end(), [](const EePair& p, const EePair& q) { return p.b < q.b; });
        }
    });
    m_eePairs.clear();
    for (const Worker& w : m_workers) m_eePairs.insert(m_eePairs.end(), w.ee.begin(), w.ee.end());
}

// The same queries on the GPU (kke::ClothGpu, shaders/cloth_pairs.comp):
// everything they read packed into one buffer of 32-bit words, the header
// first (the shader's H_ words). False: the CPU must do it.
bool ClothSystem::searchOnGpu(size_t triTotal, size_t edgeTotal, float cell) {
    const uint32_t verts = m_vertBase.back();
    // Every edge whose patch is looked at, in a hash of its own: which of
    // them are near another surface is only known on the GPU.
    double sized = 0.0;
    size_t boxes = 0;
    for (size_t ge = 0; ge < edgeTotal; ++ge) {
        if (!m_edgeLooked[ge]) continue;
        const glm::vec3 ext = m_edgeHi[ge] - m_edgeLo[ge];
        sized += std::max(ext.x, std::max(ext.y, ext.z));
        ++boxes;
    }
    const float edgeInv = 1.0f / std::max(cell, boxes ? float(sized / double(boxes)) : cell);
    m_edgeAllBox.assign(edgeTotal, CellBox{});
    for (size_t ge = 0; ge < edgeTotal; ++ge)
        if (m_edgeLooked[ge] && !cellBox(m_edgeLo[ge], m_edgeHi[ge], edgeInv, m_edgeAllBox[ge].lo, m_edgeAllBox[ge].hi)) m_edgeAllBox[ge] = CellBox{};
    m_edgeAllGrid.build(m_edgeAllBox);

    std::vector<uint32_t>& w = m_gpuWords;
    constexpr size_t kHeader = 33;
    w.assign(kHeader, 0u);
    auto begin = [&w](size_t word) { w[word] = uint32_t(w.size()); };
    auto f = [](float x) {
        uint32_t u;
        std::memcpy(&u, &x, 4);
        return u;
    };
    auto vec4 = [&w, &f](const glm::vec3& v, float x) { w.insert(w.end(), { f(v.x), f(v.y), f(v.z), f(x) }); };
    auto box = [&w](const CellBox& b) {
        const CellBox c = b.valid() ? b : CellBox{ glm::ivec3(0), glm::ivec3(-1) };
        w.insert(w.end(), { uint32_t(c.lo.x), uint32_t(c.lo.y), uint32_t(c.lo.z), uint32_t(c.hi.x), uint32_t(c.hi.y), uint32_t(c.hi.z) });
    };
    w[0] = verts;
    w[1] = uint32_t(triTotal);
    w[2] = uint32_t(edgeTotal);
    w[3] = m_triGrid.mask;
    w[4] = m_edgeAllGrid.mask;
    w[5] = m_patchTotal;
    w[6] = m_allPairs ? 1u : 0u;
    // (7, 8: how many pairs it may answer, filled in by ClothGpu.)
    begin(9); // positions, w = the cloth's thickness
    for (const Cloth* c : m_active)
        for (const glm::vec3& p : c->pos) vec4(p, c->thickness);
    begin(10); // at the last pass, w = the cloth's mean edge
    for (const Cloth* c : m_active)
        for (const glm::vec3& p : c->prev) vec4(p, c->meanEdge);
    begin(11); // at rest
    for (const Cloth* c : m_active)
        for (const glm::vec3& p : c->rest) vec4(p, 0.0f);
    begin(12); // 1 = can move, 2 = looked at
    for (const Cloth* c : m_active)
        for (size_t v = 0; v < c->pos.size(); ++v) w.push_back((c->invMass[v] > 0.0f ? 1u : 0u) | (c->vertLooked[v] ? 2u : 0u));
    begin(13); // patch (pass-wide), or none
    for (size_t ci = 0; ci < m_active.size(); ++ci)
        for (uint32_t p : m_active[ci]->vertPatch) w.push_back(p == UINT32_MAX ? UINT32_MAX : m_patchBase[ci] + p);
    begin(14); // cloth
    for (size_t ci = 0; ci < m_active.size(); ++ci) w.insert(w.end(), m_active[ci]->pos.size(), uint32_t(ci));
    begin(15);
    for (const CellBox& b : m_vertBox) box(b);
    begin(16); // neighbours: where each vertex's start (pass-wide), then them
    {
        uint32_t at = 0;
        for (const Cloth* c : m_active) {
            for (size_t v = 0; v < c->pos.size(); ++v) w.push_back(at + c->ringStart[v]);
            at += uint32_t(c->ring.size());
        }
        w.push_back(at);
        begin(17);
        for (size_t ci = 0; ci < m_active.size(); ++ci)
            for (uint32_t n : m_active[ci]->ring) w.push_back(m_vertBase[ci] + n);
    }
    begin(18); // triangles: pass-wide vertices, cloth
    for (size_t ci = 0; ci < m_active.size(); ++ci) {
        const Cloth& c = *m_active[ci];
        for (size_t t = 0; t + 2 < c.tris.size(); t += 3)
            w.insert(w.end(), { m_vertBase[ci] + c.tris[t], m_vertBase[ci] + c.tris[t + 1], m_vertBase[ci] + c.tris[t + 2], uint32_t(ci) });
    }
    begin(19);
    for (const Cloth* c : m_active)
        for (const glm::vec4& sph : c->triSphere) vec4(glm::vec3(sph), sph.w);
    begin(20);
    for (const Cloth* c : m_active)
        for (const glm::vec3& m : c->triMove) vec4(m, 0.0f);
    begin(21);
    for (size_t ci = 0; ci < m_active.size(); ++ci)
        for (uint32_t p : m_active[ci]->triPatch) w.push_back(m_patchBase[ci] + p);
    begin(22);
    for (const CellBox& b : m_triBox) box(b);
    begin(23);
    w.insert(w.end(), m_triGrid.start.begin(), m_triGrid.start.end());
    begin(24);
    w.insert(w.end(), m_triGrid.items.begin(), m_triGrid.items.end());
    begin(25); // edges: pass-wide ends, cloth, 1 = can move
    for (size_t ge = 0; ge < edgeTotal; ++ge) {
        const Cloth& c = *m_active[m_edgeCloth[ge]];
        const uint32_t* ends = &c.edges[size_t(ge - c.edgeBase) * 2];
        const uint32_t base = m_vertBase[m_edgeCloth[ge]];
        w.insert(w.end(), { base + ends[0], base + ends[1], m_edgeCloth[ge], uint32_t(m_edgeMoves[ge] & 1) });
    }
    begin(26);
    for (size_t ge = 0; ge < edgeTotal; ++ge) vec4(m_edgeLooked[ge] ? m_edgeLo[ge] : glm::vec3(0.0f), 0.0f);
    begin(27);
    for (size_t ge = 0; ge < edgeTotal; ++ge) vec4(m_edgeLooked[ge] ? m_edgeHi[ge] : glm::vec3(0.0f), 0.0f);
    begin(28);
    w.insert(w.end(), m_edgePatch.begin(), m_edgePatch.end());
    begin(29);
    for (const CellBox& b : m_edgeAllBox) box(b);
    begin(30);
    w.insert(w.end(), m_edgeAllGrid.start.begin(), m_edgeAllGrid.start.end());
    begin(31);
    w.insert(w.end(), m_edgeAllGrid.items.begin(), m_edgeAllGrid.items.end());
    begin(32);
    for (uint64_t bits : m_patchPairs) w.insert(w.end(), { uint32_t(bits), uint32_t(bits >> 32) });
    w.push_back(0u); // (no array may be empty past the end)

    if (!m_gpu->search(w, verts, uint32_t(edgeTotal), m_gpuVt, m_gpuEe, m_gpuFlags)) return false;

    // Sorted (the GPU answers in any order; the narrow phase fixes them in
    // this one, as the CPU finds them), each once.
    std::vector<uint64_t> keys(m_gpuVt.size() / 2);
    for (size_t i = 0; i < keys.size(); ++i) keys[i] = (uint64_t(m_gpuVt[i * 2]) << 32) | m_gpuVt[i * 2 + 1];
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    m_vtPairs.clear();
    size_t ci = 0;
    for (uint64_t k : keys) {
        const uint32_t gv = uint32_t(k >> 32);
        while (gv >= m_vertBase[ci + 1]) ++ci;
        m_vtPairs.push_back({ uint32_t(ci), gv - m_vertBase[ci], uint32_t(k) });
    }
    keys.resize(m_gpuEe.size() / 2);
    for (size_t i = 0; i < keys.size(); ++i) keys[i] = (uint64_t(m_gpuEe[i * 2]) << 32) | m_gpuEe[i * 2 + 1];
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    m_eePairs.clear();
    for (uint64_t k : keys) m_eePairs.push_back({ uint32_t(k >> 32), uint32_t(k) });
    for (size_t c = 0; c < m_active.size(); ++c)
        for (size_t v = 0; v < m_active[c]->pos.size(); ++v) m_active[c]->nearOther[v] = m_gpuFlags[m_vertBase[c] + v] ? 1 : 0;
    return true;
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

// Too far apart now, for how far they moved relative to each other this
// step, to have touched on the way: the distance between two segments
// changes by at most the most any point of one moved relative to the other.
bool ClothSystem::edgesFar(const Cloth& c, uint32_t e, const Cloth& o, uint32_t f, float gap) {
    const uint32_t a0 = c.edges[size_t(e) * 2], a1 = c.edges[size_t(e) * 2 + 1];
    const uint32_t b0 = o.edges[size_t(f) * 2], b1 = o.edges[size_t(f) * 2 + 1];
    const glm::vec3 mb = 0.5f * ((o.pos[b0] - o.prev[b0]) + (o.pos[b1] - o.prev[b1]));
    const float moved = std::max(glm::length(c.pos[a0] - c.prev[a0] - mb), glm::length(c.pos[a1] - c.prev[a1] - mb)) +
                        0.5f * glm::length((o.pos[b0] - o.prev[b0]) - (o.pos[b1] - o.prev[b1]));
    const float reach = gap + moved;
    // First the midpoints (cheap): no closer than they are, less both half lengths.
    const glm::vec3 ma = 0.5f * (c.pos[a0] + c.pos[a1]), mo = 0.5f * (o.pos[b0] + o.pos[b1]);
    const float far = reach + 0.5f * (glm::length(c.pos[a1] - c.pos[a0]) + glm::length(o.pos[b1] - o.pos[b0]));
    if (glm::dot(ma - mo, ma - mo) > far * far) return true;
    float s, t;
    closestSegments(c.pos[a0], c.pos[a1], o.pos[b0], o.pos[b1], s, t);
    const glm::vec3 d = glm::mix(c.pos[a0], c.pos[a1], s) - glm::mix(o.pos[b0], o.pos[b1], t);
    return glm::dot(d, d) > reach * reach;
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
    const bool tangled = tangledAt(std::max(std::max(c.undoneStreak[a0], c.undoneStreak[a1]), std::max(o.undoneStreak[b0], o.undoneStreak[b1])));
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
        if (inside(bc, 0.02f) && !tangledAt(c.undoneStreak[v])) {
            side = sp > 0.0f ? 1.0f : -1.0f;
            crossed = true;
        }
    }
    if (!crossed) {
        if (std::fabs(s) >= thick) return false;
        if (!inside(barycentric(p, a, b, d), 0.0f)) return false;
        const float ref = std::fabs(sp) > 1e-6f && !tangledAt(c.undoneStreak[v]) ? sp : s;
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

// Air on a strand: each segment a cylinder `hairWidth` wide, pushed by the
// air's velocity across it (drag along a hair is tiny). Implicit like
// air(): never more than the relative speed across the segment.
void ClothSystem::airOnStrands(Cloth& c, float dt) {
    if (c.fabric.airDrag <= 0.0f || c.strandVerts < 2) return;
    const glm::vec3 wind = m_wind * c.wind;
    const size_t n = c.pos.size();
    for (size_t b = 0; b + c.strandVerts <= n; b += c.strandVerts)
        for (size_t i = b + 1; i + 1 < b + c.strandVerts; ++i) {
            const glm::vec3 seg = c.pos[i + 1] - c.pos[i];
            const float len = glm::length(seg);
            if (len < 1e-7f) continue;
            const glm::vec3 t = seg / len;
            glm::vec3 rel = 0.5f * (c.vel[i] + c.vel[i + 1]) - wind;
            rel -= t * glm::dot(rel, t);
            const float speed = glm::length(rel);
            if (speed < 1e-6f) continue;
            // Force = 0.5 rho Cd (width x length) |v| v, Cd ~ 1.2 for a cylinder, half to each end.
            const float k = 0.5f * kAirDensity * 1.2f * c.fabric.airDrag * c.hairWidth * len * speed * dt * 0.5f;
            for (size_t v : { i, i + 1 }) {
                const float f = std::min(1.0f, k * c.invMass[v]);
                c.vel[v] -= rel * f;
            }
        }
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
        if (c.fabric.airDrag <= 0.0f || (c.tris.empty() && !c.hair)) continue;
        JPH::Body* body = locks.TryGetBody(c.body);
        if (!body || !body->IsActive()) continue;
        load(c, *body);
        if (c.hair)
            airOnStrands(c, ctx.mDeltaTime);
        else
            air(c, ctx.mDeltaTime);
        store(c, *body);
    }
    m_stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int ClothSystem::beginStep() {
    m_stepMs = 0.0;
    m_undid = false;
    for (auto& [id, c] : m_cloths) {
        c.stats.selfContacts = 0;
        c.stats.crossingsUndone = 0;
    }
    const int sub = m_calm < kCalmUpdates ? m_substeps : 1;
    if (sub != m_sub) {
        m_sub = sub;
        setIterations();
    }
    return m_sub;
}

bool ClothSystem::tangledAt(uint8_t streak) const { return m_substeps == 1 && streak >= kTangledSteps; }

// Each soft body's solver iterations spread over the m_sub sub-steps.
void ClothSystem::setIterations() {
    const JPH::BodyLockInterfaceNoLock& locks = m_system.GetBodyLockInterfaceNoLock(); // between updates
    for (auto& [id, c] : m_cloths)
        if (JPH::Body* body = locks.TryGetBody(c.body)) softOf(*body)->SetNumIterations(uint32_t((c.iterations + m_sub - 1) / m_sub));
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
        m_calm = m_undid ? 0u : std::min(m_calm + 1u, 1u << 20);
        m_stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    m_lastMs = m_stepMs;
}

void ClothSystem::protectAll(const JPH::BodyLockInterface& locks) {
    m_active.clear();
    for (auto& [id, c] : m_cloths) {
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
        c.undoneNow.resize(c.pos.size(), 0);
        m_active.push_back(&c);
    }
    if (m_active.empty()) return;
    protect();
    for (Cloth* c : m_active) {
        if (c->stats.crossingsUndone > 0) m_undid = true;
        for (size_t i = 0; i < c->pos.size(); ++i) {
            // (Counted once per update, however many sub-steps it has.)
            if (m_between) {
                c->undoneStreak[i] = c->undoneNow[i] ? uint8_t(std::min(255, c->undoneStreak[i] + 2)) : uint8_t(std::max(0, c->undoneStreak[i] - 1));
                c->undoneNow[i] = 0;
            }
            // However many contacts pile up on one vertex (a heap of cloth
            // on the floor), the pass moves it no further than the step
            // moved it or anything it touched, plus a little: enough to
            // undo any crossing, never enough to fight the solver and the
            // floor and feed energy into the heap.
            // And never more than a few thicknesses in one pass, however
            // fast things moved: a big jump stretches the fabric around it,
            // the solver springs back from that, and between layers pressed
            // together that can build up until the cloth flies apart.
            const float reach = std::min(c->reach[i] + 3.0f * c->thickness, kMostPerPass * (c->thickness + 0.1f * c->meanEdge));
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
