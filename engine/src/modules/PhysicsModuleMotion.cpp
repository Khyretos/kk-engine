// Steering a whole FEMFX object from game code (PhysicsModule.h, "Steering
// a whole object"): where it is, kicks and spin through its vertices'
// velocities, moving it and putting it back to its rest shape. Written for
// the tennis ball (games/tennis), general for any ball or prop a game
// throws around itself.

#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

namespace kke {

namespace {
glm::vec3 toG(const AMD::FmVector3& v) { return glm::vec3(v.x, v.y, v.z); }
AMD::FmVector3 toF(const glm::vec3& v) { return AMD::FmInitVector3(v.x, v.y, v.z); }
} // namespace

// Every piece of an object: its own pieces, or a breakable's parts.
template <typename Fn>
void PhysicsModule::forEachPiece(ObjectHandle handle, Fn&& fn) const {
    auto bit = m_breakables.find(handle);
    if (bit != m_breakables.end()) {
        for (ObjectHandle p : bit->second.parts) forEachPiece(p, fn);
        return;
    }
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) return;
    const uint32_t numPieces = AMD::FmGetNumTetMeshes(*it->second->tetMeshBuffer);
    for (uint32_t m = 0; m < numPieces; ++m)
        if (AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*it->second->tetMeshBuffer, m)) fn(*piece);
}

bool PhysicsModule::objectMotion(ObjectHandle handle, glm::vec3& center, glm::vec3& velocity) const {
    glm::vec3 c(0.0f), v(0.0f);
    float total = 0.0f;
    forEachPiece(handle, [&](AMD::FmTetMesh& piece) {
        const uint32_t n = AMD::FmGetNumVerts(piece);
        if (n == 0) return;
        // Pieces weighted by their vertex count (close enough to their mass
        // for a centre; a ball is one piece anyway).
        const float w = static_cast<float>(n);
        c += toG(AMD::FmGetCenterOfMass(piece)) * w;
        glm::vec3 sum(0.0f);
        for (uint32_t i = 0; i < n; ++i) sum += toG(AMD::FmGetVertVelocity(piece, i));
        v += sum;
        total += w;
    });
    if (total <= 0.0f) return false;
    center = c / total;
    velocity = v / total;
    return true;
}

void PhysicsModule::changeVertexVelocities(ObjectHandle handle,
                                           const std::function<glm::vec3(const glm::vec3& position, const glm::vec3& velocity)>& change) {
    if (!m_scene || !change) return;
    forEachPiece(handle, [&](AMD::FmTetMesh& piece) {
        const uint32_t n = AMD::FmGetNumVerts(piece);
        for (uint32_t i = 0; i < n; ++i)
            AMD::FmSetVertVelocity(m_scene, &piece, i, toF(change(toG(AMD::FmGetVertPosition(piece, i)), toG(AMD::FmGetVertVelocity(piece, i)))));
    });
}

void PhysicsModule::translateObject(ObjectHandle handle, const glm::vec3& delta) {
    if (!m_scene) return;
    forEachPiece(handle, [&](AMD::FmTetMesh& piece) {
        const uint32_t n = AMD::FmGetNumVerts(piece);
        for (uint32_t i = 0; i < n; ++i) AMD::FmSetVertPosition(m_scene, &piece, i, toF(toG(AMD::FmGetVertPosition(piece, i)) + delta));
    });
}

void PhysicsModule::resetObject(ObjectHandle handle, const glm::vec3& center, const glm::vec3& velocity) {
    if (!m_scene) return;
    forEachPiece(handle, [&](AMD::FmTetMesh& piece) {
        const uint32_t n = AMD::FmGetNumVerts(piece);
        if (n == 0) return;
        glm::vec3 rest(0.0f);
        for (uint32_t i = 0; i < n; ++i) rest += toG(AMD::FmGetVertRestPosition(piece, i));
        rest /= static_cast<float>(n);
        AMD::FmResetFromRestPositions(m_scene, &piece, AMD::FmMatrix3::identity(), toF(center - rest), toF(velocity));
    });
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
