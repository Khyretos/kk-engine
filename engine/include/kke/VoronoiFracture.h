#pragma once

#include "kke/FracturePattern.h"
#include "kke/TetMeshAsset.h"
#include "kke/VoxelTets.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Natural-looking fracture for FEMFX tet meshes: cut the mesh along a
// Voronoi diagram, the technique behind RayFire's Voronoi shatter, Unreal
// Chaos geometry collections and NVIDIA Blast authoring.
//
// FEMFX can only crack along tet faces. The old approach grouped whole
// voxel cells into chunks, so every piece had staircase, axis-aligned
// sides: every material broke into "exact square pieces" (user report).
//
// How it works now (no extra tets, so no extra simulation cost):
//   1. each tet joins the Voronoi cell of the seed nearest its centre;
//   2. stray lumps joined to their cell only by an edge (FEMFX would drop
//      them as single-tet shards) move to the neighbouring cell;
//   3. every vertex on a border between pieces slides onto the Voronoi
//      plane between them (onto the line for 3 pieces, the point for 4),
//      staying on the object's surface, and only as far as keeps every
//      tet well shaped.
// The borders become flat faces at whatever angles the random seeds give
// - a brick breaks into irregular chunks, wood into long splinters, glass
// into radial wedges - roughened where a vertex couldn't move all the way.
// (An exact clip-and-retetrahedralize of the Voronoi cells was tried
// first: 8-13x the tets and a quarter of them slivers. Too expensive.)
// Deterministic: same mesh + same seeds = same pieces on every machine,
// which is what multiplayer needs: send a seed, not the debris.

// Seed points for a fracture pattern, in the mesh's space. The pattern
// measures distance through `metric` (per-axis scale): < 1 on an axis
// stretches cells along it (wood grain).
struct FractureSeeds {
    std::vector<glm::vec3> points;
    glm::vec3 metric{1.0f};
    // Optional second level (clusters of cells): cluster id per seed. A
    // crack between two cells of the same cluster is `clusterStrength`
    // times tougher, so a first hit breaks the object into clusters and
    // a harder one breaks those further (Chaos-style multi-level).
    std::vector<uint32_t> clusterOfSeed;
};

struct FractureSeedOptions {
    FracturePattern pattern = FracturePattern::Voronoi;
    float chunkSize = 0.3f;     // rough piece diameter, metres
    uint32_t seed = 1;          // see fractureSeed()
    size_t maxPieces = 40;      // hard cap: pieces cost tets (FEMFX step time)
    // Where the object will be hit, if known (glass rings centre on it).
    // Otherwise the centre of the object.
    bool hasImpactPoint = false;
    glm::vec3 impactPoint{0.0f};
    // 0 = one level. Otherwise roughly this many cells per cluster.
    int cellsPerCluster = 0;
};

// Combines the scene/world seed with one object's own seed (splitmix).
// A level can set one global seed; every object mixes in its own id, so
// two crates in the same world break differently, but the same world
// breaks the same way on every run and every client.
uint32_t fractureSeed(uint32_t globalSeed, uint32_t objectSeed);

FractureSeeds makeFractureSeeds(const TetMeshData& mesh, const FractureSeedOptions& options);

struct VoronoiCut {
    TetMeshData mesh;                  // same tets as the input, border vertices moved
    std::vector<uint32_t> chunkOfTet;  // piece (seed index) per tet
    std::vector<float> tetStrength;    // fracture threshold multiplier per tet (1 = base)
    size_t snappedVertices = 0;        // border vertices moved onto a Voronoi plane
    size_t mergedFragments = 0;        // tets moved to a neighbouring piece (step 2)
};

// Splits `mesh` into the Voronoi cells of `seeds` (see above). With no
// seeds (or one) the mesh comes back unchanged as a single chunk.
VoronoiCut cutAlongVoronoi(const TetMeshData& mesh, const FractureSeeds& seeds);

// The whole bake: seeds, cut, FEMFX face flags. `flags` are ready for
// PhysicsModule::TetSpawnOptions::tetFlags.
struct BakedFracture {
    FractureSeeds seeds;
    VoronoiCut cut;
    std::vector<uint16_t> flags;
    size_t pieces = 0;
};
BakedFracture bakeFracture(const TetMeshData& mesh, const FractureSeedOptions& options);

// Makes a drawn surface (TriangleSoup glued with embedTriangles) break
// cleanly along the pieces. Each triangle goes with one piece; one that
// straddled a crack used to go whole with the piece under its centre,
// so along every crack one piece had a lip of surface hanging past its
// crack face and the other a notch through which you looked into its
// back-face-culled inside: broken props looked hollow, their faces not
// closing (user report). Here every triangle whose corners lie in
// different pieces (the tets under them) is cut along the Voronoi plane
// between those pieces, the plane the crack faces were snapped onto.
// Where the border is too rough for its plane a triangle stays whole.
// Measured: ~2% of a broken cube's surface hung on the wrong piece,
// now ~0.6% (tests/test_interior_fill.cpp). Cuts add ~60% triangles to
// a pillar with 40 pieces and ~10 ms; the sandbox subdivides half as
// finely as before to pay for it (docs/OPTIMIZATION.md #30, #31).
// Stops adding triangles at `maxTriangles`. Returns the piece of each
// output triangle, for embedTrianglesInPieces().
std::vector<uint32_t> splitSoupAtPieces(TriangleSoup& soup, const TetMeshData& mesh, const std::vector<uint32_t>& chunkOfTet,
                                        const FractureSeeds& seeds, size_t maxTriangles);

// embedTriangles(), but each triangle is glued to a tet of its own piece
// (`pieceOfTriangle`): a triangle cut exactly at a crack can have its
// centre just inside the neighbour's tets where the border is rough.
TetEmbedding embedTrianglesInPieces(const TetMeshData& mesh, const std::vector<glm::vec3>& soupPositions,
                                    const std::vector<uint32_t>& chunkOfTet, const std::vector<uint32_t>& pieceOfTriangle);

} // namespace kke
