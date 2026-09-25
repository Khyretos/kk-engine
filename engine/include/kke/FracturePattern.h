#pragma once

#include "kke/TetMeshAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// How an object breaks — our take on RayFire / APEX / Chaos fracture
// types, done the FEMFX way. FEMFX can only separate tets along tet
// faces, and by default *every* interior face can crack, so everything
// crumbles into single tetrahedra (the "uniform triangles" look). A
// pattern groups tets into chunks, then marks every face *inside* a
// chunk as unbreakable (FM_TET_FLAG_FACEn_FRACTURE_DISABLED); cracks can
// then only run along chunk borders.
//
// Decided when the object is created (baked, like Chaos's pre-fractured
// geometry collections), costs nothing at runtime — and fewer, bigger
// pieces are also *cheaper* to simulate than a cloud of single tets.
enum class FracturePattern : uint8_t {
    Shards,     // every tet separate: fine gravel/ice (the old behaviour)
    Voronoi,    // irregular chunks around random seeds: stone, concrete, brick
    Splinters,  // chunks stretched 4x along the longest axis: wood grain
    Radial,     // rings x sectors around the centre of the widest face: glass, ice sheets
    Solid,      // never breaks apart (combine with plasticity for metal that dents)
};

const char* fracturePatternName(FracturePattern p);

// Chunk id per tet. `chunkSize` is the rough chunk diameter in metres;
// `seed` makes it repeatable (same seed, same cracks).
std::vector<uint32_t> fractureChunks(const TetMeshData& mesh, FracturePattern pattern, float chunkSize, uint32_t seed);

// FEMFX tet flags from chunk ids: FACEn_FRACTURE_DISABLED on every face
// shared by two tets of the same chunk (face n = the face opposite
// corner n, FEMFX's convention).
std::vector<uint16_t> fractureFlagsFromChunks(const TetMeshData& mesh, const std::vector<uint32_t>& chunkOfTet);

// Moves every vertex that isn't on the outer surface by up to
// `amount` (metres) in random directions, rejecting moves that would
// change a tet's volume by more than -40%/+60% (FEMFX's solver blows up
// on badly shaped tets). Keep `amount` <= ~12% of the cell size. Crack faces then look rough
// instead of following the voxel grid, and the outer shape is unchanged.
void jitterInteriorVertices(TetMeshData& mesh, float amount, uint32_t seed);

// Face-connected components: component id per tet. Two tets are
// connected only if they share a whole face (FEMFX can't hold tets
// together through a shared edge or corner).
std::vector<uint32_t> faceConnectedComponents(const TetMeshData& mesh);

// Number of distinct chunks (for stats and tests).
size_t chunkCount(const std::vector<uint32_t>& chunkOfTet);

} // namespace kke
