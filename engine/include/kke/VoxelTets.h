#pragma once

#include "kke/TetMeshAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Turns any triangle mesh into a tetrahedral volume that FEMFX can
// simulate, at runtime, in milliseconds — the engine's answer to "make
// this prop breakable". It replaces the offline CGAL tool for gameplay
// purposes (see README "Content pipeline").
//
// How (the N64 way — a coarse grid, not exact geometry):
//   1. Mark every grid cell a triangle passes through (triangles are
//      point-sampled at half-cell spacing).
//   2. Flood-fill the outside from the grid border; whatever the flood
//      can't reach is solid (the surface cells plus enclosed inside).
//      This works on open, non-watertight meshes — Synty props are full
//      of holes and overlapping parts, which break exact
//      tetrahedralizers (CGAL needs a closed surface). Where the flood
//      leaks in through a hole (an open bottom), cells with surface
//      ahead in 5 of the 6 axis directions still count as inside, so
//      the volume stays solid instead of becoming a hollow shell.
//   3. Each solid cell becomes 6 tetrahedra, split around one shared
//      diagonal (Kuhn split), so neighbouring cells' faces match and
//      the whole thing is one connected body.
// The tet volume is a slightly blocky superset of the mesh. That is fine
// because the tets are never drawn directly: the original mesh rides on
// them (see TetEmbedding), and only fresh crack faces show tet geometry.
//
// Cells are sized per axis to fit the mesh bounds exactly (not
// necessarily cubes), so thin objects stay thin and nothing overhangs.
// Budget: cells are enlarged until at most `maxSolidCells` are solid
// (6 tets each). ~40-80 cells is a good prop on min-spec.
struct VoxelTetMesh {
    TetMeshData mesh;                 // in the input's coordinate space
    float cellSize = 0.0f;            // smallest side of the final cells
    glm::vec3 cellSize3{0.0f};        // per-axis cell size (cells fit the bounds exactly)
    glm::ivec3 dims{0};               // grid cells per axis
    glm::vec3 origin{0.0f};           // min corner of the grid
    size_t solidCells = 0;
};

VoxelTetMesh voxelizeToTets(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& triangleIndices,
                            float cellSize, size_t maxSolidCells);

// Pulls the voxel volume's outer vertices onto the real surface: each
// surface vertex moves to the closest point on the triangles, if that's
// within `maxDistance`, backing off (full, half, quarter move) whenever a
// tet would lose more than half its volume or grow past 150% — FEMFX
// blows up on flattened tets (BUG-043). Result: the physics shape hugs
// the prop, and crack faces no longer poke out of it as fins.
// Returns how many vertices moved.
size_t fitSurfaceToMesh(TetMeshData& tets, const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& triangleIndices,
                        float maxDistance);

// Closest point to p on triangle (a, b, c) (Ericson, Real-Time Collision Detection 5.1.5).
glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

// Points (e.g. a render mesh's vertices) glued to a tet mesh: each point
// stores which tet it's in and its barycentric weights there. As the tets
// deform, move and fracture, the point follows its tet exactly
// (position = sum of weights x current tet corners). This is how a
// Synty prop keeps its own shape, UVs and texture while being simulated.
struct TetEmbedding {
    std::vector<uint32_t> tet;        // per point: index into TetMeshData::tets
    std::vector<glm::vec4> weights;   // per point: barycentric weights of the tet's 4 corners
};

// Every point goes to the tet that contains it; points outside all tets
// (numerical edge cases) go to the tet they are least outside of, and
// are extrapolated from it (still exact under affine motion).
TetEmbedding embedPoints(const TetMeshData& mesh, const std::vector<glm::vec3>& points);

// A triangle soup (every 3 vertices = one triangle, nothing shared) —
// what a breakable surface needs: shared vertices would stretch triangles
// across cracks like rubber; unshared ones let each triangle go with its
// own piece.
struct TriangleSoup {
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    size_t triangleCount() const { return positions.size() / 3; }
};

// Splits triangles in half across their longest edge until no edge is
// longer than `maxEdge` (or `maxTriangles` is reached). Synty walls are
// 12 triangles for 15 m^2 — far too coarse to follow fracture pieces.
void subdivideSoup(TriangleSoup& soup, float maxEdge, size_t maxTriangles);

// Glues whole triangles to tets: each triangle goes to the tet under its
// centre, and all three corners use that tet (weights extrapolated when a
// corner sticks out of it). Affine motion keeps them exact; at a crack
// the triangle leaves with its piece instead of stretching.
TetEmbedding embedTriangles(const TetMeshData& mesh, const std::vector<glm::vec3>& soupPositions);

// Barycentric weights of p in tet (a, b, c, d).
glm::vec4 barycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d);

// Signed volume of tet (a, b, c, d) (positive for this engine's winding).
float tetVolume(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d);

} // namespace kke
