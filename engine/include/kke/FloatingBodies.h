#pragma once

#include "kke/Ocean.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Rigid bodies that float, bob, roll and sink in kke::OceanWaves.
//
// Each body is a box sampled by a small grid of points (a "voxel hull",
// 12-80 points). Every step each point asks the ocean how deep it is:
//   buoyancy  = water density * g * (its share of the volume) * how much
//               of it is under water   -> applied AT the point, so a
//               tilted hull gets a righting torque for free;
//   drag      = proportional to the point's velocity relative to the
//               moving water          -> damps bobbing and rolling, and
//               lets waves push things around.
// That is the whole trick behind most game boats (Sea of Thieves, Just
// Cause, Unity/Unreal buoyancy plug-ins): no fluid simulation, just
// Archimedes at sample points on an analytic surface. Density alone
// decides the rest: 500 kg/m^3 wood floats half out, 917 ice floats with
// ~8% showing, 7800 iron sinks to the sea floor.
//
// Deliberately small and separate from FEMFX (which has no fluids):
// semi-implicit Euler, box inertia, bounding-sphere contacts between
// bodies, a flat sea floor. Unit-tested in tests/test_floating_bodies.cpp.
struct FloatingBody {
    glm::vec3 halfExtents{0.5f};
    float density = 500.0f;            // kg/m^3
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
    float linearDrag = 1.5f;           // 1/s at full submersion (water resistance)
    // Extra drag on *vertical* motion relative to the water: a real hull
    // loses heave energy making waves (radiation damping). Without it a
    // light boat bobs with a damping ratio of ~0.06 and gets launched off
    // crests (measured: 2.7 m into the air, then capsized). 6/s gives
    // ~0.3 for the demo boat.
    float heaveDrag = 6.0f;
    float angularDrag = 2.0f;          // extra rotational damping in water
    // Where the weight acts, relative to the box centre (body space). A
    // boat's ballast/keel sits low (e.g. -0.3 m): gravity then pulls it
    // upright whenever it heels — real ballast, not a fake righting force.
    glm::vec3 centerOfMassOffset{0.0f};
    bool alive = true;

    // derived by FloatingBodies::add()
    float mass = 0.0f;
    glm::vec3 inertiaBody{0.0f};       // diagonal box inertia
    std::vector<glm::vec3> samplePoints; // local space
    float pointVolume = 0.0f;
    float submerged = 0.0f;            // 0..1, from the last step (for effects)
    float impactSpeed = 0.0f;          // downward speed on entering water this step (splashes)

    // accumulated by applyForce, cleared every step
    glm::vec3 force{0.0f}, torque{0.0f};

    glm::mat4 transform() const;
};

class FloatingBodies {
public:
    float waterDensity = 1025.0f;      // sea water
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float seaFloorY = -6.0f;

    // Returns the body's index (stable until clear()).
    size_t add(const glm::vec3& halfExtents, float density, const glm::vec3& position,
               const glm::quat& orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    void remove(size_t index) { if (index < m_bodies.size()) m_bodies[index].alive = false; }
    void clear() { m_bodies.clear(); }

    // A force at a world-space point (engines, rudders, explosions).
    void applyForce(size_t index, const glm::vec3& force, const glm::vec3& worldPoint);

    void step(float dt, const OceanWaves& ocean, float time);

    std::vector<FloatingBody>& bodies() { return m_bodies; }
    const std::vector<FloatingBody>& bodies() const { return m_bodies; }

private:
    std::vector<FloatingBody> m_bodies;
};

} // namespace kke
