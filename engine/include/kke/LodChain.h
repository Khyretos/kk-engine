#pragma once

#include "kke/ModelAsset.h"

#include <cstdint>
#include <vector>

namespace kke {

// Automatic levels of detail (ENG-F62, docs/LOD.md). A level is a simpler index list over the SAME vertices
// of a mesh part (kke::simplifyIndices only drops vertices from use, never moves or adds any), so it needs no
// vertex data of its own and works for static and skinned parts alike: every vertex left keeps its bone weights.
// Pure logic; kke::ModelModule uploads the levels and draws the one that suits each instance's size on screen.
//
// How far a level may deviate from the mesh is a fraction of the part's size (`errors`); how small an object must
// be on screen before that deviation shrinks to a pixel or so follows from it, so a level is used exactly when its
// difference can no longer be seen: error * height in pixels <= pixelTolerance.
struct LodSettings {
    std::vector<float> ratios{ 0.5f, 0.25f, 0.12f };    // triangles kept, of the full mesh, per level
    std::vector<float> errors{ 0.005f, 0.012f, 0.03f }; // the most each level may deviate, as a fraction of the part's size
    uint32_t minTriangles = 400;                        // a part with fewer gets no levels
    float minReduction = 0.25f;                         // a level must have at least this much fewer triangles than the one before
    float pixelTolerance = 1.0f;                        // how far a level's outline may be off on screen
    float referenceHeight = 1080.0f;                    // pixels: the screen height the thresholds are worked out for
};

struct LodLevel {
    std::vector<uint32_t> indices; // into the part's vertices
    float ratio = 1.0f;            // triangles kept (what was achieved, of the full mesh)
    float error = 0.0f;            // the allowed deviation it was made with
    float switchBelow = 1.0f;      // used when the object is smaller than this fraction of the view's height
};

// The levels below the full mesh, coarser each time, with strictly falling switch sizes. Empty for a part under
// `minTriangles` or one that does not simplify.
std::vector<LodLevel> buildLodChain(const ModelMesh& mesh, const LodSettings& settings = {});

// The fraction of the view's height an object with this bounding radius fills at this distance
// (tanHalfFov: tan of half the vertical field of view). Large when the camera is inside it.
float lodScreenHeight(float radius, float distance, float tanHalfFov);

// The level for an object that fills `screenHeight` of the view: 0 is the full mesh, n is the nth level of the
// chain. `switchBelow` are the chain's switch sizes (falling). `bias` > 1 keeps detail longer, < 1 drops it sooner.
// `hysteresis` (a fraction of each switch size, 0.15 = 15%) stops an object on the line from flickering between
// two levels: going coarser needs the size to fall that far below the line, going finer that far above it.
int selectLod(const std::vector<float>& switchBelow, float screenHeight, int current, float bias = 1.0f, float hysteresis = 0.15f);

} // namespace kke
