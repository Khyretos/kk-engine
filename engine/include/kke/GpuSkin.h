#pragma once

// GPU skinning (ENG-G08); kke::ModelModule packs these buffers and shaders/skin.comp reads them, writing kke::Vertex into each skinned part's vertex buffer; tests/test_gpu_skin.cpp checks the CPU copy of the shader against skinVertex.
// kSkinBindVec4s: vec4s per bind vertex: (position, uv.x), (normal, uv.y), weights, joints as floats.
// kSkinVec4sPerBone: vec4s per skin matrix (its four columns).
// kSkinVec4sPerZone: vec4s per jiggle zone: (centre, radius), (offset, 0).
// kSkinFloatsPerVertex: floats the shader writes per vertex, kke::Vertex's layout.
// kSkinGroupSize: the shader's local_size_x.
// SkinPush: the shader's push constants.
// packSkinBind: a mesh's bind vertices for the shader, kSkinBindVec4s per vertex.
// packSkinData: the skin matrices, then the jiggle zones, into out (replacing what it held).
// skinPackedVertex: the shader's maths on the CPU for vertex v: writes kSkinFloatsPerVertex floats to out.

#include "kke/JigglePhysics.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// kSkinBindVec4s: vec4s per bind vertex (position/uv.x, normal/uv.y, weights, joints).
constexpr uint32_t kSkinBindVec4s = 4;
constexpr uint32_t kSkinVec4sPerBone = 4;
constexpr uint32_t kSkinVec4sPerZone = 2;
// kSkinFloatsPerVertex: floats the shader writes per vertex (kke::Vertex layout).
constexpr uint32_t kSkinFloatsPerVertex = 11;
constexpr uint32_t kSkinGroupSize = 64;

// SkinPush: the shader's push constants.
struct SkinPush {
    uint32_t vertexCount = 0;
    uint32_t boneCount = 0;
    uint32_t zoneCount = 0;
    uint32_t pad = 0;
};

// packSkinBind: a mesh's bind vertices for the shader (kSkinBindVec4s per vertex).
std::vector<glm::vec4> packSkinBind(const std::vector<ModelVertex>& vertices);
// packSkinData: the skin matrices, then the jiggle zones, into out (replacing what it held).
void packSkinData(const std::vector<glm::mat4>& skin, const std::vector<SkinJiggleOffset>& zones, std::vector<glm::vec4>& out);
// skinPackedVertex: the shader's maths on the CPU for vertex v (writes kSkinFloatsPerVertex floats to out).
void skinPackedVertex(const glm::vec4* bind, const glm::vec4* data, const SkinPush& pc, uint32_t v, float* out);

} // namespace kke
