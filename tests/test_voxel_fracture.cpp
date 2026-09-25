#include "kke/FracturePattern.h"
#include "kke/VoxelTets.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

// Closed unit cube as a triangle mesh (12 triangles).
void unitCube(std::vector<glm::vec3>& p, std::vector<uint32_t>& idx) {
    p.clear();
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1));
    idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
}

float minVolume(const kke::TetMeshData& m) {
    float v = 1e30f;
    for (const auto& t : m.tets) v = std::min(v, kke::tetVolume(m.vertices[t[0]], m.vertices[t[1]], m.vertices[t[2]], m.vertices[t[3]]));
    return v;
}

} // namespace

TEST(VoxelTets, ClosedCubeIsFilledSolid) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.25f, 1000);
    // 4x4x4 cells, interior included (flood fill can't reach it).
    EXPECT_EQ(v.dims, glm::ivec3(4));
    EXPECT_EQ(v.solidCells, 64u);
    EXPECT_EQ(v.mesh.tets.size(), 64u * 6u);
    EXPECT_EQ(v.mesh.vertices.size(), 125u); // shared corners: 5^3
    EXPECT_GT(minVolume(v.mesh), 0.0f);      // every tet valid, same winding
}

TEST(VoxelTets, OpenPlaneBecomesOneLayer) {
    std::vector<glm::vec3> p = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 0, 2 }, { 2, 0, 2 } };
    std::vector<uint32_t> idx = { 0, 2, 1, 1, 2, 3 };
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.5f, 1000);
    EXPECT_EQ(v.dims.y, 1);
    EXPECT_EQ(v.solidCells, 16u);
}

TEST(VoxelTets, BudgetEnlargesCells) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.05f, 40);
    EXPECT_LE(v.solidCells, 40u);
    EXPECT_GT(v.solidCells, 0u);
    EXPECT_GT(v.cellSize, 0.05f);
}

TEST(VoxelTets, EmbeddedPointsReconstructExactly) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::VoxelTetMesh v = kke::voxelizeToTets(p, idx, 0.5f, 1000);
    std::vector<glm::vec3> pts = { { 0.1f, 0.2f, 0.3f }, { 0.9f, 0.9f, 0.9f }, { 0.5f, 0.5f, 0.5f }, { 0, 0, 0 }, { 1, 1, 1 } };
    kke::TetEmbedding e = kke::embedPoints(v.mesh, pts);
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& t = v.mesh.tets[e.tet[i]];
        glm::vec4 w = e.weights[i];
        EXPECT_GE(std::min({ w.x, w.y, w.z, w.w }), -1e-4f) << i;
        glm::vec3 r = v.mesh.vertices[t[0]] * w.x + v.mesh.vertices[t[1]] * w.y + v.mesh.vertices[t[2]] * w.z + v.mesh.vertices[t[3]] * w.w;
        EXPECT_NEAR(glm::length(r - pts[i]), 0.0f, 1e-4f) << i;
    }
}

TEST(FracturePattern, ChunkCountsPerPattern) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::TetMeshData m = kke::voxelizeToTets(p, idx, 0.2f, 1000).mesh;
    EXPECT_EQ(kke::chunkCount(kke::fractureChunks(m, kke::FracturePattern::Shards, 0.3f, 1)), m.tets.size());
    EXPECT_EQ(kke::chunkCount(kke::fractureChunks(m, kke::FracturePattern::Solid, 0.3f, 1)), 1u);
    size_t voronoi = kke::chunkCount(kke::fractureChunks(m, kke::FracturePattern::Voronoi, 0.4f, 1));
    EXPECT_GE(voronoi, 5u);
    EXPECT_LE(voronoi, 64u);
    // Repeatable: same seed, same chunks.
    EXPECT_EQ(kke::fractureChunks(m, kke::FracturePattern::Voronoi, 0.4f, 7), kke::fractureChunks(m, kke::FracturePattern::Voronoi, 0.4f, 7));
}

TEST(FracturePattern, FlagsDisableOnlyFacesInsideChunks) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::TetMeshData m = kke::voxelizeToTets(p, idx, 0.5f, 1000).mesh; // 8 cells, 48 tets
    auto bits = [](const std::vector<uint16_t>& f) {
        size_t n = 0;
        for (uint16_t x : f) for (int i = 0; i < 4; ++i) n += (x >> i) & 1;
        return n;
    };
    // Shards: nothing disabled.
    EXPECT_EQ(bits(kke::fractureFlagsFromChunks(m, kke::fractureChunks(m, kke::FracturePattern::Shards, 0.5f, 1))), 0u);
    // Solid: every interior face disabled on both sides. 48 tets x 4 faces
    // = 192 face slots; exterior faces = 6 sides x 4 cells x 2 triangles = 48.
    EXPECT_EQ(bits(kke::fractureFlagsFromChunks(m, kke::fractureChunks(m, kke::FracturePattern::Solid, 0.5f, 1))), 192u - 48u);
}

TEST(FracturePattern, JitterKeepsSurfaceAndValidTets) {
    std::vector<glm::vec3> p;
    std::vector<uint32_t> idx;
    unitCube(p, idx);
    kke::TetMeshData m = kke::voxelizeToTets(p, idx, 0.25f, 1000).mesh;
    kke::TetMeshData j = m;
    kke::jitterInteriorVertices(j, 0.08f, 3);
    size_t moved = 0;
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        bool onSurface = glm::any(glm::lessThan(m.vertices[v], glm::vec3(1e-4f))) || glm::any(glm::greaterThan(m.vertices[v], glm::vec3(1 - 1e-4f)));
        if (onSurface) EXPECT_EQ(m.vertices[v], j.vertices[v]);
        else moved += m.vertices[v] != j.vertices[v];
    }
    EXPECT_GT(moved, 0u);
    EXPECT_GT(minVolume(j), 0.0f);
}
