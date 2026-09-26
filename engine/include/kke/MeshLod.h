#pragma once

#include "kke/ModelAsset.h"

namespace kke {

// Cheaper copies of a model for crowds and distance (meshoptimizer, MIT).
// Skinned models stay skinned: simplification only drops vertices, so
// every vertex that's left keeps its own bone weights. Unit-tested in
// tests/test_mesh_lod.cpp.
//
//   kke::ModelData goblin = kke::loadSidekickCharacter(...); // 18k triangles
//   kke::weldModel(goblin);                      // shared vertices merged
//   kke::ModelData far = kke::simplifyModel(goblin, 0.15f); // ~2.7k

// Merges vertices that are identical in every attribute (FBX files store
// one vertex per triangle corner), per mesh part, and optimizes the
// index order for the GPU's vertex cache. Returns vertices removed.
size_t weldModel(ModelData& model);

struct SimplifyOptions {
    // Largest allowed deviation, as a fraction of the mesh's size
    // (0.02 = 2%). The target isn't met when it would take more than this.
    float maxError = 0.02f;
    // Keep the edges of open mesh parts where they are (seams between
    // separately modelled pieces, like a Sidekick character's parts).
    bool lockBorders = false;
    // Weight of normals and UVs against positions (0 = shape only).
    float attributeWeight = 0.5f;
    // Let collapses cross UV and normal seams (hard edges, texture
    // islands). Far more reduction on low-poly art whose every face is its
    // own island (Synty), for a little colour bleed at the seams.
    bool acrossSeams = false;
    // Remove small disconnected pieces (rivets, teeth) once they'd be
    // smaller than the error allows.
    bool prune = false;
};

// A copy with about `ratio` of each mesh part's triangles (0..1), welded
// first. Parts too small to simplify (under 64 triangles) are kept.
ModelData simplifyModel(const ModelData& model, float ratio, const SimplifyOptions& options = {});

} // namespace kke
