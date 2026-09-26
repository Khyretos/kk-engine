#pragma once

#include "kke/Capabilities.h"

#include <vector>

namespace kke {

// Every physics engine at once (kke::IPhysicsWorld, issue #31). Pass
// app.findCapability<IPhysicsWorld>(): a game with FEMFX and Jolt asks
// both, a game with one asks that one, a game with none gets no hit.

// The closest hit over all worlds.
IPhysicsWorld::Hit physicsRaycast(const std::vector<IPhysicsWorld*>& worlds, const glm::vec3& origin, const glm::vec3& direction,
                                  float maxDistance);
// A blast in every world (IPhysicsWorld::physicsBlast); total pushed.
size_t physicsBlast(const std::vector<IPhysicsWorld*>& worlds, const glm::vec3& center, float radius, float speed);
// Sum of every world's stats (step times add: the worlds step one after the other).
IPhysicsWorld::Stats physicsStats(const std::vector<IPhysicsWorld*>& worlds);

// The velocity change a blast gives something at `point` (0 outside the
// radius; straight up at the very centre).
glm::vec3 blastVelocity(const glm::vec3& center, float radius, float speed, const glm::vec3& point);

} // namespace kke
