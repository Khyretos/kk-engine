#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// CPU-side model data loaded from FBX or OBJ (via ufbx) — the art-pack
// import path, e.g. Synty. No GPU types here: kke::ModelModule turns this
// into buffers and textures, and this half stays unit-testable
// (tests/test_model_asset.cpp).
//
// Conventions after loading, whatever the source file used: meters,
// right-handed, +Y up, counter-clockwise front faces, triangles only.

struct ModelVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    // Skinned meshes only: up to 4 bone influences, weights sum to 1.
    // Indices refer to ModelData::bones.
    glm::uvec4 joints{0u};
    glm::vec4 weights{0.0f};
};

struct ModelMaterial {
    std::string name;
    glm::vec3 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    // Resolved path to the diffuse/albedo image on this machine, or empty
    // if there is none or it couldn't be found (see ModelLoadOptions).
    std::string albedoTexture;
    // What the file itself said, e.g. an artist's "U:/Dropbox/.../Tex.png"
    // — kept for error messages when resolution fails.
    std::string albedoTextureOriginal;
};

struct ModelMesh {
    std::string name;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;  // triangle list
    uint32_t material = 0;          // index into ModelData::materials
    // Static meshes: vertices are already in model space (node transforms
    // baked in). Skinned meshes: vertices are in the mesh's geometry
    // space, and ModelBone::inverseBind maps that space to each bone.
    bool skinned = false;
};

struct ModelBone {
    std::string name;
    int parent = -1;                  // index into ModelData::bones, -1 = root
    glm::mat4 localRest{1.0f};        // rest transform relative to the parent
    glm::mat4 inverseBind{1.0f};      // mesh geometry space -> this bone's space
};

// One animation clip, sampled at a fixed rate into per-bone local
// transforms — simple to play back and to blend, and FBX curves are
// usually baked at this rate anyway.
struct ModelAnimation {
    std::string name;
    float duration = 0.0f;
    float sampleRate = 30.0f;
    // frames[f][bone] = that bone's local transform at time f / sampleRate.
    std::vector<std::vector<glm::mat4>> frames;
};

struct ModelData {
    std::string sourcePath;
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelBone> bones;          // parents always come before children
    std::vector<ModelAnimation> animations;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};

    bool isSkinned() const;
    int findBone(const std::string& name) const; // -1 if absent (exact, case-sensitive)
    size_t triangleCount() const;
};

struct ModelLoadOptions {
    // Extra folders searched (by file name) for textures the file refers
    // to with a path that doesn't exist here — the usual case for
    // asset-store packs, whose FBX files embed the artist's own paths.
    // Always also searched: the model's folder, and "Textures"/"textures"
    // next to it and one level up.
    std::vector<std::string> textureSearchPaths;
    // Used when a material has no texture of its own, or its texture
    // can't be found (empty = none). Synty packs use one shared atlas.
    std::string fallbackTexture;
    bool loadAnimations = true;
    float animationSampleRate = 30.0f;
    // Some exported packs mix units: the header says centimeters but a few
    // files hold meters, so they load 100x too small (Synty Town's
    // SM_Bld_Shop_01, SM_Env_Road_01, ...). With this on, an unskinned
    // model in a centimeter file that comes out under `tinyModel` meters
    // on every axis is taken as meters and scaled up 100x.
    bool fixUnitMismatch = true;
    float tinyModel = 0.1f;
};

// Throws std::runtime_error with the path and the reason on failure.
ModelData loadModel(const std::string& path, const ModelLoadOptions& options = {});

// Rest-pose world (model-space) transform of every bone.
std::vector<glm::mat4> computeRestPose(const ModelData& model);

// Given each bone's local transform (e.g. a ModelAnimation frame, or the
// rest pose with a few bones rotated), returns the matrices to skin with:
// world[b] * inverseBind[b]. `locals` must have bones.size() entries.
std::vector<glm::mat4> computeSkinMatrices(const ModelData& model, const std::vector<glm::mat4>& locals);

// Applies skinMatrices to one skinned vertex (position and normal).
void skinVertex(const ModelVertex& in, const std::vector<glm::mat4>& skinMatrices, glm::vec3& outPosition, glm::vec3& outNormal);

} // namespace kke
