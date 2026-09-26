#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// FEMFX <-> Jolt (issue #7, docs/PHYSICS_BRIDGE.md): the two worlds don't
// see each other, so each fixed step the bridge
//   1. mirrors the Jolt bodies near moving FEMFX pieces into FEMFX as
//      kinematic boxes (pieces land on crates, bounce off walls, get
//      shoved by the character), and
//   2. hands back to Jolt the momentum FEMFX's contacts took out of the
//      pieces at those boxes (shards push crates, a slab presses a crate
//      down).
// FEMFX reports no contact forces, so (2) is measured: a vertex at a
// box's surface whose velocity changed, beyond gravity, away from the
// box was pushed by the box, and the box got the opposite push.
// This file is the math, without either engine (tests/test_physics_bridge.cpp).

// A box from the other world. `key` is the other world's id.
struct BridgeBox {
    uint64_t key = 0;
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
    bool movable = false; // dynamic: takes impulses back
};

// World-space bounds of an oriented box.
void boxBounds(const BridgeBox& box, glm::vec3& min, glm::vec3& max);
bool boundsOverlap(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax);

// The part of the box's surface nearest `point`: outward normal (world)
// and signed distance (> 0 outside, < 0 inside).
void boxSurface(const BridgeBox& box, const glm::vec3& point, glm::vec3& normal, float& distance);

// One FEMFX vertex over a step.
struct BridgeVertex {
    glm::vec3 position{0.0f};      // after the step
    glm::vec3 deltaVelocity{0.0f}; // after - before
    float mass = 0.0f;
};

// Impulse (N s, world) the vertices put into the box during a step of
// `dt` seconds under `gravity`: each vertex within `skin` of the surface
// (or inside it) whose velocity change minus gravity's points out of the
// box pushed the box the other way, along that face's normal. `point`
// gets where it acts (the impulse-weighted mean contact point), and the
// return is zero when nothing touched.
glm::vec3 boxContactImpulse(const BridgeBox& box, const std::vector<BridgeVertex>& verts, const glm::vec3& gravity, float dt, float skin,
                            glm::vec3* point = nullptr);

} // namespace kke
