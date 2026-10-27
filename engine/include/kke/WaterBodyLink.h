#pragma once

#include "kke/MpmWater.h"
#include "kke/RigidWorld.h"

#include <vector>

namespace kke {

// Floats a RigidWorld's bodies in a kke::MpmWater: every step, the bodies
// in the tank go to the water as boxes (spheres, capsules and hulls as
// their bounds), and what the water did to them goes back as velocity
// changes. Their own physics keeps gravity, contacts and the rest.
//
//     link.pull(world, water);   // in fixedUpdate, after the world stepped
//     water.step(dt);
//     link.push(world, water);
//
// Static bodies are left out unless includeStatic (the tank's own walls
// already hold the water); kinematic ones push the water but take nothing
// back. Triangle-mesh bodies are left out (RigidWorld::bodiesInBox).
class WaterBodyLink {
public:
    bool includeStatic = false;
    float friction = 0.1f; // how much water drags along the bodies' sides

    void pull(const RigidWorld& world, MpmWater& water);
    void push(RigidWorld& world, const MpmWater& water) const;

private:
    std::vector<RigidWorld::BodyBox> m_boxes;
};

} // namespace kke
