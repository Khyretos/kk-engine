#pragma once

// Physics from C++ (docs/cookbook/physics.md): kke::RigidWorld is Jolt
// behind a small API, the same one the Lua `physics` table uses.
// tests/test_cookbook.cpp runs these in a world of its own.

#include "kke/RigidWorld.h"

namespace cookbook {

// --8<-- [start:crate]
// A crate: a dynamic box, 60 cm on a side, dropped at `at`.
inline kke::RigidWorld::BodyId dropCrate(kke::RigidWorld& world, const glm::vec3& at) {
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Box;
    d.halfExtents = glm::vec3(0.3f); // half the size
    d.position = at;
    d.density = 150.0f;   // kg/m^3: light wood
    d.restitution = 0.1f; // hardly bounces
    return world.add(d);
}
// --8<-- [end:crate]

// --8<-- [start:ground]
// How high the ground is below `from` (the first thing a ray straight down
// hits within 50 m). False when there's nothing there.
inline bool groundBelow(const kke::RigidWorld& world, const glm::vec3& from, float& height) {
    const kke::RigidWorld::RayHit hit = world.raycast(from, glm::vec3(0.0f, -1.0f, 0.0f), 50.0f);
    if (!hit.hit) return false;
    height = hit.point.y;
    return true;
}
// --8<-- [end:ground]

} // namespace cookbook
