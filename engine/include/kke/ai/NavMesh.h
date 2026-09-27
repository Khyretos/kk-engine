#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kke::ai {

// Navigation mesh: where agents can walk, and paths across it. Built with
// Recast and queried with Detour (recastnavigation, zlib licence: the
// navmesh library Unity, Unreal, Godot and O3DE all started from; see
// docs/DEPENDENCIES.md), so this file is only a thin, engine-shaped
// wrapper: glm vectors in, corner lists out.
//
// Build it once from the level's collision triangles (ground, walls,
// fences, buildings; y up, metres, counter-clockwise from above is the
// top). Anything too steep, too low to stand under or too thin for the
// agent's radius is left out automatically, which is what makes a
// navmesh better than a grid: fences, doorways and hills come for free.
//
// Queries are not thread-safe (Detour's query keeps a node pool); use one
// NavMesh per thread or lock around it. Unit-tested in tests/test_ai.cpp.
struct NavMeshSettings {
    float cellSize = 0.25f;     // metres, horizontal resolution
    float cellHeight = 0.2f;    // metres, vertical resolution
    float agentHeight = 1.6f;   // how low a roof it walks under
    float agentRadius = 0.35f;  // how close to walls it walks
    float agentMaxClimb = 0.45f;// step height (kerbs, low steps)
    float agentMaxSlope = 45.0f;// degrees
    float regionMinSize = 8.0f; // islands smaller than this (cells^0.5) are dropped
    float regionMergeSize = 20.0f;
    float edgeMaxLength = 12.0f;
    float edgeMaxError = 1.3f;
    int vertsPerPoly = 6;
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;
};

class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(NavMesh&&) noexcept;
    NavMesh& operator=(NavMesh&&) noexcept;
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // Builds from triangles (three indices each). False with a reason
    // when nothing walkable came out or Recast failed.
    bool build(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, const NavMeshSettings& settings = {},
               std::string* error = nullptr);
    bool valid() const;
    const NavMeshSettings& settings() const;

    // Detour's own binary (fast to load; cache it next to a level).
    std::vector<uint8_t> save() const;
    bool load(std::span<const uint8_t> data, std::string* error = nullptr);

    // The closest point on the mesh within `searchExtents` (half sizes).
    bool nearestPoint(const glm::vec3& p, glm::vec3& out, const glm::vec3& searchExtents = { 2.0f, 4.0f, 2.0f }) const;

    // Corners of the shortest path (start and end included, snapped to the
    // mesh). When `to` can't be reached, the path goes to the closest
    // reachable point and `partial` is set. False when either end is off
    // the mesh.
    struct Path {
        std::vector<glm::vec3> points;
        bool partial = false;
    };
    bool findPath(const glm::vec3& from, const glm::vec3& to, Path& out) const;

    // Slides from `from` towards `to` along the surface, stopping at walls
    // (with the height of the ground there). Cheap: for per-frame movement.
    glm::vec3 moveAlongSurface(const glm::vec3& from, const glm::vec3& to) const;

    // Can you walk in a straight line from `from` to `to`? When not,
    // `hitFraction` is how far along (0..1) the first wall is.
    bool walkable(const glm::vec3& from, const glm::vec3& to, float* hitFraction = nullptr) const;

    // A random reachable point roughly within `radius` of `centre`.
    // `random01` returns numbers in [0, 1).
    bool randomPointNear(const glm::vec3& centre, float radius, const std::function<float()>& random01, glm::vec3& out) const;

    // The mesh as triangles for debug drawing (lifted a little off the ground).
    void debugTriangles(std::vector<glm::vec3>& out, float lift = 0.05f) const;
    size_t polygonCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace kke::ai
