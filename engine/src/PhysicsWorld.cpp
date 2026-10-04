#include "kke/PhysicsWorld.h"

#include <algorithm>

namespace kke {

IPhysicsWorld::Hit physicsRaycast(const std::vector<IPhysicsWorld*>& worlds, const glm::vec3& origin, const glm::vec3& direction,
                                  float maxDistance) {
    IPhysicsWorld::Hit best;
    for (const IPhysicsWorld* w : worlds) {
        if (!w) continue;
        IPhysicsWorld::Hit h = w->physicsRaycast(origin, direction, best.hit ? best.distance : maxDistance);
        if (h.hit && (!best.hit || h.distance < best.distance)) {
            best = h;
            best.world = w;
        }
    }
    return best;
}

size_t physicsBlast(const std::vector<IPhysicsWorld*>& worlds, const glm::vec3& center, float radius, float speed) {
    size_t pushed = 0;
    for (IPhysicsWorld* w : worlds)
        if (w) pushed += w->physicsBlast(center, radius, speed);
    return pushed;
}

IPhysicsWorld::Stats physicsStats(const std::vector<IPhysicsWorld*>& worlds) {
    IPhysicsWorld::Stats total;
    for (const IPhysicsWorld* w : worlds) {
        if (!w) continue;
        const IPhysicsWorld::Stats s = w->physicsStats();
        total.bodies += s.bodies;
        total.awake += s.awake;
        total.stepMs += s.stepMs;
    }
    return total;
}

glm::vec3 blastVelocity(const glm::vec3& center, float radius, float speed, const glm::vec3& point) {
    if (radius <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 d = point - center;
    const float dist = glm::length(d);
    if (dist >= radius) return glm::vec3(0.0f);
    const glm::vec3 dir = dist > 1e-4f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    return dir * speed * (1.0f - dist / radius);
}

glm::vec3 blastedVelocity(const glm::vec3& velocity, const glm::vec3& dv) {
    const float push = glm::length(dv);
    if (push <= 0.0f) return velocity;
    const glm::vec3 dir = dv / push;
    return velocity + dir * std::max(0.0f, push - glm::dot(velocity, dir));
}

} // namespace kke
