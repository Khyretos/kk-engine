// How the crowd finds its way: a navigation mesh of the sport center
// (kke::ai::NavMesh, Recast and Detour) built from the same boxes Jolt
// has (the fences, the stands, the hall's walls), so a walker heads for
// a seat or a spot on the promenade along a path round everything,
// instead of straight at it and into the first wall in the way.

#include "TennisModule.h"

#include "kke/Log.h"
#include "kke/ai/NavMesh.h"

#include <array>
#include <cmath>

namespace tennis {

void TennisModule::buildNavMesh() {
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> idx;
    verts.reserve(m_navBoxes.size() * 8);
    idx.reserve(m_navBoxes.size() * 36);
    for (const NavBox& b : m_navBoxes) {
        const glm::mat3 r = glm::mat3_cast(b.rotation);
        const uint32_t base = static_cast<uint32_t>(verts.size());
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 s((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
            verts.push_back(b.centre + r * (s * b.half));
        }
        // Each face counter-clockwise seen from outside (the top seen from above).
        static constexpr std::array<uint32_t, 36> kFaces = { 2, 6, 7, 2, 7, 3, 0, 1, 5, 0, 5, 4, 0, 4, 6, 0, 6, 2,
                                                              1, 3, 7, 1, 7, 5, 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6 };
        for (uint32_t k : kFaces) idx.push_back(base + k);
    }
    kke::ai::NavMeshSettings s;
    s.agentRadius = 0.3f;
    s.agentMaxClimb = 0.2f; // benches and kerbs are things to walk round
    std::string error;
    m_nav = std::make_unique<kke::ai::NavMesh>();
    if (!m_nav->build(verts, idx, s, &error)) {
        kke::log::get(name())->warn("crowd: no navigation mesh ({}); walkers go straight", error);
        m_nav.reset();
        return;
    }
    kke::log::get(name())->info("crowd: navigation mesh of {} polygons from {} boxes", m_nav->polygonCount(), m_navBoxes.size());
}

// Which way to walk now to get to `goal` (unit, flat), or zero once
// within `arrive`. Paths are found when the goal changes and again now
// and then (people move, a walker gets pushed off its line).
glm::vec3 TennisModule::wayTo(Walker& w, const glm::vec3& feet, const glm::vec3& goal, float arrive, float dt) {
    const glm::vec3 flat(goal.x - feet.x, 0.0f, goal.z - feet.z);
    if (glm::length(flat) < arrive) return glm::vec3(0.0f);
    if (!m_nav) return glm::normalize(flat);
    w.repath -= dt;
    if (glm::length(goal - w.pathGoal) > 0.25f || w.repath <= 0.0f || w.pathAt >= w.path.size()) {
        kke::ai::NavMesh::Path p;
        w.path.clear();
        w.pathAt = 0;
        if (m_nav->findPath(feet, goal, p)) w.path = std::move(p.points);
        w.pathGoal = goal;
        w.repath = 2.0f;
        if (w.path.size() > 1) w.pathAt = 1; // [0] is where they stand
    }
    // The next corner; past it once close (the last one: the goal itself,
    // which may be off the mesh, a seat on a bench).
    while (w.pathAt + 1 < w.path.size()) {
        const glm::vec3 c = w.path[w.pathAt];
        if (glm::length(glm::vec2(c.x - feet.x, c.z - feet.z)) > 0.4f) break;
        ++w.pathAt;
    }
    glm::vec3 to = flat;
    if (w.pathAt + 1 < w.path.size()) {
        const glm::vec3 c = w.path[w.pathAt];
        to = { c.x - feet.x, 0.0f, c.z - feet.z };
    }
    const float d = glm::length(to);
    return d > 1e-4f ? to / d : glm::normalize(flat);
}

} // namespace tennis
