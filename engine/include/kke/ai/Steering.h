#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace kke::ai {

// Craig Reynolds' steering behaviours ("Steering Behaviors For Autonomous
// Characters", GDC 1999) and boids ("Flocks, Herds, and Schools", 1987):
// the standard formulas, on the ground plane (y is up and ignored, so
// animals on hills steer the same as on the flat). Each behaviour returns a
// desired change of velocity (a steering force, in m/s^2 once scaled by
// maxAccel); blend() adds them with weights and truncates, the usual
// "weighted truncated sum". Pure maths, no world: kke::ai::AiWorld and
// games call these. Unit-tested in tests/test_ai.cpp.

struct Mover {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float maxSpeed = 3.0f; // m/s
    float maxAccel = 8.0f; // m/s^2
    float radius = 0.5f;   // for separation and obstacles
};

inline glm::vec3 flat(const glm::vec3& v) { return { v.x, 0.0f, v.z }; }
// Length of `v` limited to `max` (the zero vector stays zero).
glm::vec3 truncate(const glm::vec3& v, float max);

// Straight at the target at full speed.
glm::vec3 seek(const Mover& m, const glm::vec3& target);
// Straight away from the threat at full speed; nothing beyond `panicDistance`
// (0 = always).
glm::vec3 flee(const Mover& m, const glm::vec3& threat, float panicDistance = 0.0f);
// Seek that slows down inside `slowRadius` and stops on the target.
glm::vec3 arrive(const Mover& m, const glm::vec3& target, float slowRadius = 2.0f);
// Seek / flee where a moving target will be (its position plus velocity
// times the time to reach it, capped at `maxPrediction` seconds).
glm::vec3 pursue(const Mover& m, const glm::vec3& targetPos, const glm::vec3& targetVel, float maxPrediction = 1.5f);
glm::vec3 evade(const Mover& m, const glm::vec3& threatPos, const glm::vec3& threatVel, float panicDistance = 0.0f,
                float maxPrediction = 1.5f);

// Wander: a target that jitters around a circle in front of the mover.
// `angle` is the mover's own wander state (radians), updated in place;
// `random01` a random number in [0, 1).
struct WanderParams {
    float distance = 2.0f; // circle centre, metres ahead
    float radius = 1.0f;
    float jitter = 2.5f;   // radians per second
};
glm::vec3 wander(const Mover& m, float& angle, float dt, float random01, const WanderParams& p = {});

// Neighbours for the group behaviours: positions and velocities of the
// others close enough to matter (the caller does the spatial query).
struct Neighbour {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.5f;
};
// Push away from neighbours closer than `distance` (plus both radii), harder the closer.
glm::vec3 separation(const Mover& m, std::span<const Neighbour> near, float distance = 1.0f);
// Match the neighbours' average heading.
glm::vec3 alignment(const Mover& m, std::span<const Neighbour> near);
// Head for the neighbours' centre.
glm::vec3 cohesion(const Mover& m, std::span<const Neighbour> near);

struct FlockWeights {
    float separation = 1.6f;
    float alignment = 0.6f;
    float cohesion = 0.5f;
    float separationDistance = 0.8f; // extra gap beyond the two radii
};
// The three boid rules together, already weighted.
glm::vec3 flock(const Mover& m, std::span<const Neighbour> near, const FlockWeights& w = {});

// Round obstacles (trees, posts, troughs) on the ground plane.
struct CircleObstacle {
    glm::vec3 centre{0.0f};
    float radius = 0.5f;
};
// Steers sideways around the nearest obstacle in a box `lookAhead`
// seconds long in front of the mover (Reynolds' obstacle avoidance).
glm::vec3 avoidObstacles(const Mover& m, std::span<const CircleObstacle> obstacles, float lookAhead = 1.0f);

// Weighted sum of steering forces, truncated to maxAccel.
struct Weighted {
    glm::vec3 force{0.0f};
    float weight = 1.0f;
};
glm::vec3 blend(const Mover& m, std::span<const Weighted> forces);

// Moves `m` by `steering` for `dt` seconds (explicit Euler, speed capped),
// on the ground plane. Returns the new velocity.
glm::vec3 integrate(Mover& m, const glm::vec3& steering, float dt);

// Uniform grid over the ground plane for neighbour queries: O(1) insert,
// a query looks at the cells the radius touches. Rebuilt every frame
// (clear + insert) by whoever owns the movers.
class SpatialGrid {
public:
    explicit SpatialGrid(float cellSize = 4.0f) : m_cell(cellSize) {}
    void clear() { m_cells.clear(); }
    void insert(uint32_t id, const glm::vec3& position);
    // Every id within `radius` of `position` (by cell, then exact
    // distance using `positionOf`). Appends to `out`.
    template <typename PositionOf>
    void query(const glm::vec3& position, float radius, PositionOf&& positionOf, std::vector<uint32_t>& out) const {
        const int x0 = cellOf(position.x - radius), x1 = cellOf(position.x + radius);
        const int z0 = cellOf(position.z - radius), z1 = cellOf(position.z + radius);
        const float r2 = radius * radius;
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) {
                auto it = m_cells.find(key(x, z));
                if (it == m_cells.end()) continue;
                for (uint32_t id : it->second) {
                    const glm::vec3 d = flat(positionOf(id) - position);
                    if (glm::dot(d, d) <= r2) out.push_back(id);
                }
            }
    }
    float cellSize() const { return m_cell; }

private:
    int cellOf(float v) const;
    static uint64_t key(int x, int z) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(z); }
    float m_cell;
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_cells;
};

} // namespace kke::ai
