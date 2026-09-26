// PhysicsModule's rubble handoff and its IPhysicsWorld side (issue #31,
// docs/PHYSICS_BRIDGE.md): small broken-off pieces of breakables leave
// FEMFX for the rigid-body world, and the queries every physics engine
// answers (kke/PhysicsWorld.h).

#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/PhysicsWorld.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>

namespace kke {

namespace {
glm::vec3 toG(const AMD::FmVector3& v) { return glm::vec3(v.x, v.y, v.z); }
AMD::FmVector3 toF(const glm::vec3& v) { return AMD::FmInitVector3(v.x, v.y, v.z); }

// Möller-Trumbore, both faces; distance along the unit ray or -1.
float rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 e1 = b - a, e2 = c - a;
    const glm::vec3 p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-12f) return -1.0f;
    const float inv = 1.0f / det;
    const glm::vec3 t = o - a;
    const float u = glm::dot(t, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const glm::vec3 q = glm::cross(t, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    return glm::dot(e2, q) * inv;
}

// Slab test: does the ray enter min..max before maxDistance?
bool rayHitsBox(const glm::vec3& o, const glm::vec3& d, float maxDistance, const glm::vec3& lo, const glm::vec3& hi) {
    float t0 = 0.0f, t1 = maxDistance;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        float a = (lo[i] - o[i]) / d[i], b = (hi[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}
} // namespace

bool PhysicsModule::isRubbleCandidate(const SpawnedTet& part, float maxSize, uint32_t maxTets, float maxChunkSize) const {
    if (part.breakable == kInvalidHandle || !part.tetMesh || part.bakedTetOf.empty()) return false;
    auto bit = m_breakables.find(part.breakable);
    if (bit == m_breakables.end()) return false;
    // Broken off: not the whole object, and done splitting.
    if (part.bakedTetOf.size() >= bit->second.mesh.tets.size()) return false;
    if (part.breakGrace > 0 || part.breakPending >= 0) return false;
    if (AMD::FmIsTetMeshSleeping(*part.tetMesh)) return false;
    const glm::vec3 extent = toG(AMD::FmGetMaxPosition(*part.tetMesh)) - toG(AMD::FmGetMinPosition(*part.tetMesh));
    const float size = std::max({ extent.x, extent.y, extent.z });
    if (size <= maxSize && part.numTets <= maxTets) return true;
    if (size > maxChunkSize) return false;
    // One baked chunk: nothing left in it to break.
    const BreakGraph& graph = bit->second.graph;
    const uint32_t chunk = graph.chunkOf(part.bakedTetOf.front());
    for (uint32_t t : part.bakedTetOf)
        if (graph.chunkOf(t) != chunk) return false;
    return true;
}

void PhysicsModule::rubbleCandidates(float maxSize, uint32_t maxTets, float maxChunkSize, std::vector<ObjectHandle>& out) const {
    for (const auto& [bh, b] : m_breakables) {
        if (b.parts.size() < 2) continue;
        for (ObjectHandle ph : b.parts) {
            auto it = m_objects.find(ph);
            if (it != m_objects.end() && isRubbleCandidate(*it->second, maxSize, maxTets, maxChunkSize)) out.push_back(ph);
        }
    }
}

bool PhysicsModule::describeRubble(ObjectHandle piece, RubblePiece& out) const {
    auto it = m_objects.find(piece);
    if (it == m_objects.end() || !isRubbleCandidate(*it->second, FLT_MAX, UINT32_MAX, FLT_MAX)) return false;
    const AMD::FmTetMesh& mesh = *it->second->tetMesh;
    const uint32_t n = AMD::FmGetNumVerts(mesh);
    if (n < 4) return false;
    // Rigid motion of the vertices: mass centre, momentum, angular
    // momentum about the centre and the inertia that goes with it.
    float mass = 0.0f;
    glm::vec3 c(0.0f), p(0.0f);
    for (uint32_t v = 0; v < n; ++v) {
        const float m = AMD::FmGetVertMass(mesh, v);
        mass += m;
        c += toG(AMD::FmGetVertPosition(mesh, v)) * m;
        p += toG(AMD::FmGetVertVelocity(mesh, v)) * m;
    }
    if (mass <= 0.0f) return false;
    c /= mass;
    const glm::vec3 vel = p / mass;
    glm::vec3 l(0.0f);
    glm::mat3 inertia(0.0f);
    out.hull.clear();
    out.hull.reserve(n);
    for (uint32_t v = 0; v < n; ++v) {
        const float m = AMD::FmGetVertMass(mesh, v);
        const glm::vec3 r = toG(AMD::FmGetVertPosition(mesh, v)) - c;
        l += glm::cross(r, (toG(AMD::FmGetVertVelocity(mesh, v)) - vel) * m);
        inertia += (glm::mat3(glm::dot(r, r)) - glm::outerProduct(r, r)) * m;
        out.hull.push_back(r);
    }
    out.handle = piece;
    out.center = c;
    out.velocity = vel;
    out.angularVelocity = std::fabs(glm::determinant(inertia)) > 1e-15f ? glm::inverse(inertia) * l : glm::vec3(0.0f);
    out.mass = mass;
    out.material = it->second->material;
    const glm::vec3 size = toG(AMD::FmGetMaxPosition(mesh)) - toG(AMD::FmGetMinPosition(mesh));
    out.size = std::max({ size.x, size.y, size.z });
    return true;
}

bool PhysicsModule::convertToRubble(ObjectHandle piece) {
    RubblePiece info;
    if (!describeRubble(piece, info)) return false;
    SpawnedTet& part = *m_objects.at(piece);
    Breakable& b = m_breakables.at(part.breakable);
    const glm::vec3 centre = info.center * m_renderScale;

    Rubble r;
    r.breakable = part.breakable;
    r.material = part.material;
    r.textureSet = part.textureSet;
    r.position = info.center;
    bool truncated = false;
    buildSurface(part, r.verts, truncated);
    if (r.verts.empty()) return false;
    for (Vertex& v : r.verts) v.position -= centre;
    // Where the breakable's embedded render points sit on each tet, frozen.
    const AMD::FmTetMesh& mesh = *part.tetMesh;
    r.tetCorners.resize(part.bakedTetOf.size());
    r.tetNormal.resize(part.bakedTetOf.size());
    for (uint32_t t = 0; t < part.bakedTetOf.size(); ++t) {
        const AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(mesh, t);
        glm::vec3 x[4];
        for (int k = 0; k < 4; ++k) x[k] = toG(AMD::FmGetVertPosition(mesh, ids.ids[k])) * m_renderScale;
        for (int k = 0; k < 4; ++k) r.tetCorners[t][k] = x[k] - centre;
        const glm::mat3 f = glm::mat3(x[1] - x[0], x[2] - x[0], x[3] - x[0]) * b.restInverse[part.bakedTetOf[t]];
        r.tetNormal[t] = std::fabs(glm::determinant(f)) > 1e-12f ? glm::transpose(glm::inverse(f)) : glm::mat3(1.0f);
    }
    if (m_app) {
        r.vertexBuffer = std::make_unique<Buffer>(m_app->device(), sizeof(Vertex) * r.verts.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                  VMA_MEMORY_USAGE_CPU_TO_GPU);
        r.vertexBuffer->upload(r.verts.data(), sizeof(Vertex) * r.verts.size());
    }
    const ObjectHandle bh = part.breakable;
    // Out of FEMFX. partOfTet keeps pointing at the handle: deformEmbedded
    // finds it in m_rubble from now on.
    removeObject(piece);
    m_breakables.at(bh).rubble.push_back(piece);
    m_rubble.emplace(piece, std::move(r));
    return true;
}

void PhysicsModule::setRubbleTransform(ObjectHandle piece, const glm::vec3& position, const glm::quat& rotation) {
    auto it = m_rubble.find(piece);
    if (it == m_rubble.end()) return;
    it->second.position = position;
    it->second.rotation = glm::normalize(rotation);
}

void PhysicsModule::retireRubble(Rubble& r) {
    // The last frames in flight may still draw from it.
    if (r.vertexBuffer && m_app) m_app->renderer().retire(std::move(r.vertexBuffer));
    r.vertexBuffer.reset();
}

void PhysicsModule::removeRubble(ObjectHandle piece) {
    auto it = m_rubble.find(piece);
    if (it == m_rubble.end()) return;
    auto bit = m_breakables.find(it->second.breakable);
    if (bit != m_breakables.end()) {
        auto& list = bit->second.rubble;
        list.erase(std::remove(list.begin(), list.end(), piece), list.end());
    }
    retireRubble(it->second);
    m_rubble.erase(it);
}

void PhysicsModule::clearRubble() {
    for (auto& [handle, r] : m_rubble) retireRubble(r);
    m_rubble.clear();
    for (auto& [bh, b] : m_breakables) b.rubble.clear();
}

glm::mat4 PhysicsModule::rubbleModel(const Rubble& r) const {
    return glm::translate(glm::mat4(1.0f), r.position * m_renderScale) * glm::mat4_cast(r.rotation);
}

// ---- IPhysicsWorld

IPhysicsWorld::Stats PhysicsModule::physicsStats() const {
    Stats s;
    for (const auto& [handle, obj] : m_objects) {
        const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint32_t m = 0; m < n; ++m) {
            const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!piece) continue;
            ++s.bodies;
            if (!AMD::FmIsTetMeshSleeping(*piece)) ++s.awake;
        }
    }
    s.bodies += m_ragdolls.size();
    s.stepMs = m_lastStepMsAvg;
    return s;
}

IPhysicsWorld::Hit PhysicsModule::physicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    Hit best;
    if (glm::length(direction) < 1e-9f || maxDistance <= 0.0f) return best;
    const glm::vec3 d = glm::normalize(direction);
    // Physics units (pieces are drawn at x renderScale).
    const float s = m_renderScale > 0.0f ? m_renderScale : 1.0f;
    const glm::vec3 o = origin / s;
    float nearest = maxDistance / s;
    for (const auto& [handle, obj] : m_objects) {
        const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint32_t m = 0; m < n; ++m) {
            const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!piece || !rayHitsBox(o, d, nearest, toG(AMD::FmGetMinPosition(*piece)), toG(AMD::FmGetMaxPosition(*piece)))) continue;
            const uint32_t faces = AMD::FmGetNumExteriorFaces(*piece);
            for (uint32_t f = 0; f < faces; ++f) {
                uint32_t tetId = 0, faceId = 0;
                AMD::FmGetExteriorFace(&tetId, &faceId, *piece, f);
                const AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(*piece, tetId);
                const glm::vec3 a = toG(AMD::FmGetVertPosition(*piece, ids.ids[3 - faceId]));
                const glm::vec3 b = toG(AMD::FmGetVertPosition(*piece, ids.ids[(5 - faceId) % 4]));
                const glm::vec3 c = toG(AMD::FmGetVertPosition(*piece, ids.ids[(faceId + 2) % 4]));
                const float t = rayTriangle(o, d, a, b, c);
                if (t < 0.0f || t >= nearest) continue;
                nearest = t;
                glm::vec3 nrm = glm::cross(b - a, c - a);
                nrm = glm::length(nrm) > 1e-12f ? glm::normalize(nrm) : -d;
                if (glm::dot(nrm, d) > 0.0f) nrm = -nrm;
                best.hit = true;
                best.normal = nrm;
                best.body = handle;
            }
        }
    }
    if (!best.hit) return best;
    best.distance = nearest * s;
    best.point = origin + d * best.distance;
    best.world = this;
    return best;
}

size_t PhysicsModule::physicsBlast(const glm::vec3& center, float radius, float speed) {
    if (!m_scene || radius <= 0.0f) return 0;
    const float s = m_renderScale > 0.0f ? m_renderScale : 1.0f;
    const glm::vec3 c = center / s;
    const float r = radius / s;
    const float v = speed / s;
    size_t pushed = 0;
    for (auto& [handle, obj] : m_objects) {
        const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint32_t m = 0; m < n; ++m) {
            AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!piece) continue;
            glm::vec3 lo = toG(AMD::FmGetMinPosition(*piece)), hi = toG(AMD::FmGetMaxPosition(*piece));
            if (!boundsOverlap(lo, hi, c - glm::vec3(r), c + glm::vec3(r))) continue;
            // Per vertex, so a blast next to a wall bends and cracks it
            // rather than moving it as one.
            bool any = false;
            const uint32_t verts = AMD::FmGetNumVerts(*piece);
            for (uint32_t i = 0; i < verts; ++i) {
                const glm::vec3 dv = blastVelocity(c, r, v, toG(AMD::FmGetVertPosition(*piece, i)));
                if (glm::dot(dv, dv) <= 0.0f) continue;
                AMD::FmSetVertVelocity(m_scene, piece, i, toF(toG(AMD::FmGetVertVelocity(*piece, i)) + dv)); // wakes it
                any = true;
            }
            pushed += any ? 1 : 0;
        }
    }
    for (auto& [handle, rd] : m_ragdolls) {
        for (AMD::FmRigidBody* body : rd.bodies) {
            const AMD::FmRigidBodyState st = AMD::FmGetState(*body);
            const glm::vec3 dv = blastVelocity(c, r, v, toG(st.pos));
            if (glm::dot(dv, dv) <= 0.0f) continue;
            AMD::FmSetVelocity(m_scene, body, toF(toG(st.vel) + dv));
            ++pushed;
        }
    }
    return pushed;
}

void PhysicsModule::physicsBoundsInBox(const glm::vec3& min, const glm::vec3& max, std::vector<std::pair<glm::vec3, glm::vec3>>& out) const {
    const float s = m_renderScale > 0.0f ? m_renderScale : 1.0f;
    for (const auto& [handle, obj] : m_objects) {
        const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint32_t m = 0; m < n; ++m) {
            const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!piece) continue;
            const glm::vec3 lo = toG(AMD::FmGetMinPosition(*piece)) * s, hi = toG(AMD::FmGetMaxPosition(*piece)) * s;
            if (boundsOverlap(lo, hi, min, max)) out.emplace_back(lo, hi);
        }
    }
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
