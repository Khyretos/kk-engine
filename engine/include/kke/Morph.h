#pragma once

#include "kke/ModelAsset.h"

#include <map>
#include <string>
#include <vector>

namespace kke {

// Blend shapes at runtime (ENG-F25, docs/FACIAL_ANIMATION.md). Pure maths on
// kke::ModelMesh::morphs: the mesh at some weights is its rest shape plus
// each target's offsets times its weight. kke::ModelModule does this for a
// skinned instance before skinning, so a face morphs and then moves with its
// bones; kke::FaceRig turns expressions and mouth shapes into the weights.

// The names of every morph target of a model, each once, in the order they first appear.
std::vector<std::string> morphNames(const ModelData& model);

// One weight vector per mesh (sized to that mesh's targets) from weights by name.
// Meshes of a face share names (the head, the eyes and the teeth each have "blink"
// or "jawOpen"), so one name moves every mesh that has it. Names no mesh has are ignored.
std::vector<std::vector<float>> meshMorphWeights(const ModelData& model, const std::map<std::string, float>& byName);

// True when a mesh has targets and any weight is not zero.
bool morphActive(const ModelMesh& mesh, const std::vector<float>& weights);

// The mesh's rest vertices with the targets applied at `weights` (a missing weight is 0,
// extra ones are ignored). Everything but position and normal is the rest vertex. Normals:
// each target that has normal offsets adds them; if a target in use has none, the
// normals of the whole mesh are rebuilt from the morphed triangles. Unit length either way.
std::vector<ModelVertex> morphedVertices(const ModelMesh& mesh, const std::vector<float>& weights);

} // namespace kke
