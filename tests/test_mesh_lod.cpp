#include "kke/MeshLod.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

// A skinned grid: one vertex per triangle corner, as FBX files come in.
// The left half rides bone 0, the right half bone 1.
kke::ModelData grid(int n) {
    kke::ModelData m;
    m.bones.push_back({ "a", -1, glm::mat4(1.0f), glm::mat4(1.0f) });
    m.bones.push_back({ "b", 0, glm::mat4(1.0f), glm::mat4(1.0f) });
    kke::ModelMesh mesh;
    mesh.skinned = true;
    auto vertex = [&](int x, int z) {
        kke::ModelVertex v;
        v.position = glm::vec3(static_cast<float>(x) / static_cast<float>(n), 0.0f, static_cast<float>(z) / static_cast<float>(n));
        v.uv = glm::vec2(v.position.x, v.position.z);
        v.joints = glm::uvec4(x * 2 < n ? 0u : 1u, 0, 0, 0);
        v.weights = glm::vec4(1, 0, 0, 0);
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(v);
    };
    for (int x = 0; x < n; ++x)
        for (int z = 0; z < n; ++z) {
            vertex(x, z), vertex(x, z + 1), vertex(x + 1, z + 1);
            vertex(x, z), vertex(x + 1, z + 1), vertex(x + 1, z);
        }
    m.meshes.push_back(std::move(mesh));
    return m;
}

TEST(MeshLod, WeldMergesCornerCopies) {
    kke::ModelData m = grid(8);
    ASSERT_EQ(m.meshes[0].vertices.size(), 8u * 8u * 6u);
    const size_t removed = kke::weldModel(m);
    EXPECT_EQ(m.meshes[0].vertices.size(), 9u * 9u);
    EXPECT_EQ(removed, 8u * 8u * 6u - 81u);
    EXPECT_EQ(m.meshes[0].indices.size(), 8u * 8u * 6u);
}

TEST(MeshLod, SimplifyKeepsSkinWeightsAndCutsTriangles) {
    // A bumpy grid, so there's something to lose.
    kke::ModelData m = grid(32);
    for (auto& v : m.meshes[0].vertices) v.position.y = 0.02f * std::sin(v.position.x * 3.0f) * std::cos(v.position.z * 2.0f);
    kke::SimplifyOptions o;
    o.maxError = 0.05f;
    const kke::ModelData lod = kke::simplifyModel(m, 0.1f, o);
    const size_t before = m.triangleCount(), after = lod.triangleCount();
    EXPECT_LE(after, before / 5);
    EXPECT_GT(after, 0u);
    // Every vertex left is one of the originals, weights and all.
    for (const kke::ModelVertex& v : lod.meshes[0].vertices) {
        EXPECT_EQ(v.joints.x, v.position.x * 2.0f < 1.0f - 1e-4f ? 0u : 1u);
        EXPECT_FLOAT_EQ(v.weights.x, 1.0f);
    }
    for (uint32_t i : lod.meshes[0].indices) EXPECT_LT(i, lod.meshes[0].vertices.size());
}

TEST(MeshLod, SmallPartsAreKept) {
    const kke::ModelData m = grid(4); // 32 triangles
    const kke::ModelData lod = kke::simplifyModel(m, 0.1f);
    EXPECT_EQ(lod.triangleCount(), m.triangleCount());
}

} // namespace
