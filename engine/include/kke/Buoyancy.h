#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <vector>

namespace kke {

// Archimedes for a box in any physics engine (kke_demo's pool floats Jolt
// crates with it): the box is sampled by n x n x n points, each carrying
// its share of the volume. A point below the water surface is pushed up
// by the water it displaces and slowed by drag relative to the water;
// the pushes act at the points, so a tilted box rights itself. The
// caller applies the result as impulses (RigidWorld::addImpulse) every
// fixed step. Same idea as kke::FloatingBodies (which integrates its own
// bodies); this one only computes. tests/test_buoyancy.cpp.
struct BuoyancyBox {
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
};

struct BuoyancySettings {
    float waterDensity = 1000.0f;  // kg/m^3
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float drag = 2.0f;             // 1/s, on the displaced water's mass times the relative velocity
    int samples = 3;               // per axis (27 points)
};

// Water surface height at (x, z), and the water's velocity there
// (currents, waves); a flat still pool returns a constant and zero.
using WaterSurface = std::function<float(float x, float z, glm::vec3& waterVelocity)>;

struct BuoyancyPoint {
    glm::vec3 point{0.0f}; // world
    glm::vec3 force{0.0f}; // N
};

// Forces at the submerged sample points (appended to `out`, which is
// cleared first). Returns the fraction of the box under water (0..1).
float boxBuoyancy(const BuoyancyBox& box, const BuoyancySettings& settings, const WaterSurface& water, std::vector<BuoyancyPoint>& out);

} // namespace kke
