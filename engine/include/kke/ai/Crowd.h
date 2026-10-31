#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace kke::ai {

class NavMesh;

// Crowds (ENG-F73, docs/AI.md "Crowds"): hundreds of agents walking a
// navmesh together, steering around each other and around the walls, with
// DetourCrowd (recastnavigation, zlib: the crowd simulation Unity's and
// Unreal's are built on). Each agent has a target or a velocity; update()
// moves them all a step. Cheap enough for a stadium: a few hundred agents
// cost well under a millisecond (benchmarked in tests/test_crowd.cpp).
//
// The crowd keeps a pointer to the navmesh: rebuild the crowd when the
// mesh is rebuilt (obstacles on a dynamic mesh are fine).
struct CrowdAgentParams {
    float radius = 0.35f;
    float height = 1.7f;
    float maxSpeed = 1.4f;        // m/s (a walk; 3.5 a jog)
    float maxAcceleration = 8.0f; // m/s^2
    float separation = 2.0f;      // how hard it keeps its distance from others (0 = not at all)
    int avoidanceQuality = 2;     // 0 (cheap) .. 3 (best): how many velocities it samples to avoid others
};

class Crowd {
public:
    using AgentId = uint32_t; // 0 = none

    // `maxAgents` at once; `maxRadius` the largest agent radius used.
    Crowd(NavMesh& mesh, int maxAgents = 512, float maxRadius = 1.0f);
    ~Crowd();
    Crowd(const Crowd&) = delete;
    Crowd& operator=(const Crowd&) = delete;
    bool valid() const;

    // Places an agent on the mesh nearest `position`; 0 when it's off the
    // mesh or the crowd is full. Ids of removed agents are used again.
    AgentId add(const glm::vec3& position, const CrowdAgentParams& params = {});
    void remove(AgentId id);
    void setParams(AgentId id, const CrowdAgentParams& params);
    // Walk to `target` (snapped to the mesh; false when it's off it).
    bool moveTo(AgentId id, const glm::vec3& target);
    // Walk with this velocity instead (a player-steered or flocking agent).
    bool setVelocity(AgentId id, const glm::vec3& velocity);
    bool stop(AgentId id);

    void update(float dt);

    bool alive(AgentId id) const;
    glm::vec3 position(AgentId id) const;
    glm::vec3 velocity(AgentId id) const;
    // Close enough to its target to stop (within `radius` metres).
    bool arrived(AgentId id, float radius = 0.5f) const;
    size_t count() const;
    std::vector<AgentId> agents() const;
    int capacity() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace kke::ai
