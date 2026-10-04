#pragma once

// Planes touching each other, bullets, and the town the Dogfight mode
// happens over (README.md "Collisions and damage", "Dogfight"). Pure
// maths, no engine: tests/test_flight.cpp checks it on its own, and the
// same seed builds the same town on every machine online.

#include "Course.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace flying {

// ---- the plane's shape for hits: spheres in plane space (forward -Z,
// up +Y, right +X, the origin a gear height over the wheels): the nose,
// the cockpit, the tail, and a row along each wing out to its tip.
struct HitSphere {
    glm::vec3 at{0.0f};
    float radius = 1.0f;
};
struct PlaneShape {
    std::vector<HitSphere> spheres;
    float bound = 0.0f; // m: every sphere is inside this, round the origin
};
// `span`: wing tip to wing tip, m (the built biplane's 8; Synty's stunt plane is wider).
PlaneShape planeShape(float span = 8.0f);

// Two planes touching this step. `a` flew from a0 to a1 and `b` from b0
// to b1 (turned rotA and rotB): where they first came within reach of
// each other (their closest approach, so two planes passing at 180 m/s
// can't skip through each other between frames), and whether any of
// their spheres overlap there.
struct PlaneContact {
    bool hit = false;
    float t = 0.0f;             // 0..1 through the step
    glm::vec3 point{0.0f};      // world, between the two
    glm::vec3 normal{0.0f};     // from b toward a (the way a is pushed)
    float depth = 0.0f;         // m of overlap
};
PlaneContact planesTouch(const PlaneShape& shape, const glm::vec3& a0, const glm::vec3& a1, const glm::quat& rotA, const glm::vec3& b0,
                         const glm::vec3& b1, const glm::quat& rotB);

// A bullet from `from` to `to` this step, a plane at `at` turned `rot`:
// does it go through one of the plane's spheres? `t` is how far along
// (0..1), `point` where (world).
bool bulletHits(const PlaneShape& shape, const glm::vec3& from, const glm::vec3& to, const glm::vec3& at, const glm::quat& rot, float& t,
                glm::vec3& point);

// ---- the town

struct Building {
    glm::vec3 lo{0.0f}, hi{0.0f}; // an upright box (world): the walls and the roof
    int art = -1;                 // which Synty building draws it (-1: a box)
    int turn = 0;                 // quarter turns of that model about Y
    glm::vec3 colour{0.7f};       // a box's walls
    bool tower = false;           // an office tower (windows, a flat roof)
    bool plain = false;           // a plain box in its colour: a rock pillar, a stone bridge, a sky bridge
};

// Every building a plane can hit: the airfield's hangar and control tower
// always; with `district`, a town beside the runway too: office towers in
// its middle (tall enough to fly between), houses and shops round them.
// `houses` are the footprints (x, height, z) of the house models there
// are to use (Synty's POLYGON Town when it is installed); none: boxes.
class Town {
public:
    Town() = default;
    // `district`: the green island's town (Dogfight). The Canyon's pillars
    // and bridges and the Mega City's towers are always there; the city
    // leaves avenues along `rings` (the race course) and a square round
    // each ring.
    Town(const Island& island, bool district, const std::vector<glm::vec3>& houses = {}, const std::vector<Ring>& rings = {});

    const std::vector<Building>& buildings() const { return m_buildings; }
    bool district() const { return m_district; }
    // The highest roof within `margin` m of (x, z) (-1e9: none). The CPU
    // pilots fly over this (with a margin: they don't shave the walls).
    // `under`: only what starts below that height (a plane under a bridge
    // flies on beneath it).
    float roof(float x, float z, float margin = 0.0f, float under = 1e9f) const;
    // A sphere against every building: the deepest overlap, and which way
    // out of it (out of the nearest wall or the roof).
    bool touches(const glm::vec3& center, float radius, glm::vec3& normal, float& depth) const;
    // A bullet: the first wall on the way from `from` to `to` (0..1).
    bool blocks(const glm::vec3& from, const glm::vec3& to, float& t) const;
    // The middle of the town (where the dogfight starts round).
    glm::vec3 centre() const { return m_centre; }

private:
    void index();
    void addCanyon(const Island& island, const std::vector<Ring>& rings);
    void addCity(const Island& island, const std::vector<Ring>& rings);
    std::vector<int> near(float x0, float z0, float x1, float z1) const;
    std::vector<Building> m_buildings;
    bool m_district = false;
    glm::vec3 m_centre{0.0f};
    // A grid of cells (kCell m) listing the buildings over each.
    glm::vec2 m_gridLo{0.0f};
    int m_gridW = 0, m_gridH = 0;
    std::vector<std::vector<int>> m_cells;
};

// Where to aim to hit a target at `target` moving at `targetVelocity`
// with bullets at `speed` from `from` (the first-order lead).
glm::vec3 leadPoint(const glm::vec3& from, const glm::vec3& target, const glm::vec3& targetVelocity, float speed);

} // namespace flying
