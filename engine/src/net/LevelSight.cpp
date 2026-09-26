#include "kke/net/LevelSight.h"

namespace kke::net {

Visibility::RayClear levelSight(const RigidWorld& world) {
    return [&world](const glm::vec3& from, const glm::vec3& to) {
        const glm::vec3 d = to - from;
        const float length = glm::length(d);
        if (length < 1e-4f) return true;
        const RigidWorld::RayHit hit =
            world.raycast(from, d / length, length, [](RigidWorld::BodyId, RigidWorld::Motion m) { return m == RigidWorld::Motion::Static; });
        return !hit.hit;
    };
}

} // namespace kke::net
