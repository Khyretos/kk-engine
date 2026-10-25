#pragma once

// Shared by the two model loaders (ModelAsset.cpp for FBX/OBJ through
// ufbx, GltfAsset.cpp for glTF 2.0 through cgltf). Not a public header.

#include "kke/ModelAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace kke::detail {

// The folders searched for a model's textures: its own folder, then
// "Textures"/"textures" next to it and one level up, then
// options.textureSearchPaths.
std::vector<std::filesystem::path> textureSearchDirs(const std::string& modelPath, const ModelLoadOptions& options);

// `fileName` (any slash style; only its last part counts) in one of
// `dirs`, case-insensitively, else the same name as .png/.tga/.jpg/
// .jpeg/.bmp. Empty when none is found.
std::string findTextureFile(const std::string& fileName, const std::vector<std::filesystem::path>& dirs);

// The options' texture rules for a material whose own albedo map is
// `ownTexture` ("" = it names none) and resolved to mat.albedoTexture
// ("" = not found): assetTexture, textureForMaterial, fallbackTexture.
void applyTextureOptions(ModelMaterial& mat, bool namesTexture, const ModelLoadOptions& options);

// Bounds (rest pose for skinned meshes) and options.clipsInPlace.
void finishModel(ModelData& model, const ModelLoadOptions& options);

// glTF 2.0 (.gltf with its .bin and images, or one .glb). GltfAsset.cpp.
ModelData loadGltfModel(const std::string& path, const ModelLoadOptions& options);

} // namespace kke::detail
