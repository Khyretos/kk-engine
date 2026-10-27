#include "kke/WaterBodyLink.h"

namespace kke {

void WaterBodyLink::pull(const RigidWorld& world, MpmWater& water) {
    m_boxes.clear();
    const float margin = water.params().cellSize * 2.0f;
    world.bodiesInBox(water.params().boundsMin - glm::vec3(margin), water.params().boundsMax + glm::vec3(margin), m_boxes);
    std::vector<MpmWater::Body>& bodies = water.bodies();
    bodies.clear();
    for (const RigidWorld::BodyBox& box : m_boxes) {
        if (box.motion == RigidWorld::Motion::Static && !includeStatic) continue;
        if (bodies.size() >= MpmWater::kMaxBodies) break;
        MpmWater::Body b;
        b.position = box.center;
        b.rotation = box.rotation;
        b.halfExtents = box.halfExtents;
        b.velocity = box.velocity;
        b.angularVelocity = box.angularVelocity;
        b.mass = box.motion == RigidWorld::Motion::Dynamic ? box.mass : 0.0f;
        b.friction = friction;
        b.user = box.id;
        bodies.push_back(b);
    }
}

void WaterBodyLink::push(RigidWorld& world, const MpmWater& water) const {
    for (const MpmWater::Body& b : water.bodies()) {
        if (b.mass <= 0.0f) continue;
        if (b.deltaVelocity == glm::vec3(0.0f) && b.deltaAngularVelocity == glm::vec3(0.0f)) continue;
        world.addVelocity(b.user, b.deltaVelocity); // wakes it
        world.setAngularVelocity(b.user, world.angularVelocity(b.user) + b.deltaAngularVelocity);
    }
}

} // namespace kke
