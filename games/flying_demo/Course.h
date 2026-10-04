#pragma once

// The island the Flying demo happens over, and its ring course
// (README.md "The island and the rings"). Pure maths: the same seed
// makes the same island and rings on every machine (online, the host
// sends only the seed), and tests/test_flight.cpp flies it.

#include "Flight.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace flying {

// The runway: a flat strip at the island's middle, along Z (take off
// toward -Z, i.e. heading 0).
struct Runway {
    glm::vec3 start{0.0f, 6.0f, 380.0f}; // the end you take off from
    float length = 760.0f;
    float width = 30.0f;
    float height() const { return start.y; }
};

struct Ring {
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f, 0.0f, -1.0f}; // the way through (from the previous ring toward the next)
    float radius = 12.0f;
};

// What kind of place: the green island, a desert canyon (a plateau with a
// winding gorge through it, the race down the gorge), or a mega city
// (towers on a flat island, the race down avenues between them). The map
// is in the seed's top byte, so the host's seed is all a guest needs.
enum class Map : uint8_t { Island, Canyon, City };
constexpr uint32_t mapSeed(Map map, uint32_t n) { return (static_cast<uint32_t>(map) << 24) | (n & 0xFFFFFFu); }

class Island {
public:
    static constexpr float kRadius = 1500.0f; // m to where the sea starts (the green island)
    static constexpr float kSea = 0.0f;       // the water's height
    static constexpr float kPlateau = 190.0f; // Canyon: the plateau's height (and its runway's)
    static constexpr float kFloor = 14.0f;    // Canyon: the gorge's floor
    static constexpr float kCityGround = 6.0f;

    explicit Island(uint32_t seed = 1);
    uint32_t seed() const { return m_seed; }
    Map map() const { return m_map; }
    const Runway& runway() const { return m_runway; }
    // How far the land goes (past it, the sea).
    float landRadius() const;

    // Canyon: the gorge's middle line is r = gorgeRadius(a) around the
    // middle (a: the angle from +X toward +Z), gorgeHalfWidth(a) to each
    // wall at the floor. Distance from (x, z) to the middle line:
    float gorgeRadius(float a) const;
    float gorgeHalfWidth(float a) const;
    float gorgeDistance(float x, float z) const;
    // The point on the middle line at angle `a`, at the floor, and the way
    // along the gorge there (growing a).
    glm::vec3 gorgePoint(float a) const;
    glm::vec3 gorgeTangent(float a) const;

    // The land under (x, z) (below kSea: the sea floor).
    float terrain(float x, float z) const;
    // What a plane hits there: the land, or the sea's surface.
    float surface(float x, float z) const;
    bool onRunway(float x, float z, float margin = 0.0f) const;
    Ground ground() const;

    // `count` rings in a loop around the island, `radius` m each: over
    // hills, through the valley by the mountain, low over the sea. Each is
    // at least `clearance` above what's under it.
    std::vector<Ring> rings(int count, float radius, float clearance = 45.0f) const;

private:
    float hills(float x, float z) const;
    float islandTerrain(float x, float z) const;
    float canyonTerrain(float x, float z) const;
    float cityTerrain(float x, float z) const;
    float flattenRunway(float x, float z, float h) const;
    std::vector<Ring> gorgeRings(int count, float radius) const;
    uint32_t m_seed;
    Map m_map = Map::Island;
    Runway m_runway;
    float m_phase[6] = {};
    glm::vec2 m_peak{0.0f};    // the mountain
    float m_peakHeight = 260.0f;
};

// Did a plane going from `from` to `to` this step fly through `ring`
// (the right way, inside it)?
bool throughRing(const Ring& ring, const glm::vec3& from, const glm::vec3& to);

// Where a CPU pilot aims for its next ring (README.md "CPU pilots"):
// lined up in front of it, a point a little ahead of it closing on the
// centre, so it arrives along the ring's normal. Beside it or past it
// (a miss), it first flies out to a point well in front and turns back
// from there, as a pilot sets up an approach: a plane can't turn on the
// spot. Keeps which of the two it is doing, so it doesn't dither.
class RingPilot {
public:
    glm::vec3 aim(const Ring& ring, const glm::vec3& from);
    void reset() { m_settingUp = m_final = false; }
    bool settingUp() const { return m_settingUp; }
    // On the last stretch to the ring: the ground check only looks for
    // what's right under it (a ring can hang in front of a hillside).
    bool final() const { return m_final; }
    float floor(float cruise) const { return m_final ? 12.0f : cruise; }

private:
    bool m_settingUp = false, m_final = false;
};

} // namespace flying
