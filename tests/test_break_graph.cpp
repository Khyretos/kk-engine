#include "kke/BreakGraph.h"
#include "kke/VoxelTets.h"

#include <gtest/gtest.h>

namespace {
// 3 x 1 x 1 cells (18 tets); piece = cell x index: pieces 0 | 1 | 2.
kke::TetMeshData bar(std::vector<uint32_t>& chunk) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < 8; ++i) p.push_back(glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) * glm::vec3(3, 1, 1));
    std::vector<uint32_t> idx = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    kke::TetMeshData m = kke::voxelizeToTets(p, idx, 1.0f, 100).mesh;
    chunk.clear();
    for (const auto& t : m.tets) {
        glm::vec3 c = (m.vertices[t[0]] + m.vertices[t[1]] + m.vertices[t[2]] + m.vertices[t[3]]) * 0.25f;
        chunk.push_back(static_cast<uint32_t>(c.x));
    }
    return m;
}
auto all = [](uint32_t) { return true; };
} // namespace

TEST(BreakGraph, FindsBordersAndOutside) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    kke::BreakGraph g(m, chunk, {});
    size_t border = 0, outside = 0;
    for (uint32_t t = 0; t < g.tetCount(); ++t) {
        border += g.isBorderTet(t);
        outside += g.originalExterior(t) != 0;
    }
    EXPECT_GT(border, 0u);
    EXPECT_LT(border, g.tetCount());
    EXPECT_GT(outside, 0u);
}

TEST(BreakGraph, OnlyOverloadedBordersBreak) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    kke::BreakGraph g(m, chunk, {});
    g.arm(100.0f);
    std::vector<uint32_t> tets(g.tetCount());
    for (uint32_t t = 0; t < tets.size(); ++t) tets[t] = t;
    // Below the threshold: nothing.
    for (uint32_t t : tets) EXPECT_FALSE(g.report(t, 99.0f, all));
    EXPECT_EQ(g.groups(tets, all).size(), 1u);
    // Overload one tet on the 0|1 border only.
    uint32_t hit = UINT32_MAX;
    for (uint32_t t : tets) {
        if (chunk[t] != 0 || !g.isBorderTet(t)) continue;
        hit = t;
        break;
    }
    ASSERT_NE(hit, UINT32_MAX);
    EXPECT_TRUE(g.report(hit, 150.0f, all));
    EXPECT_FALSE(g.report(hit, 150.0f, all)); // already broken: nothing new
    EXPECT_TRUE(g.isBroken(0, 1));
    EXPECT_FALSE(g.isBroken(1, 2));
    auto groups = g.groups(tets, all);
    ASSERT_EQ(groups.size(), 2u); // piece 0 | pieces 1+2 still together
    EXPECT_EQ(chunk[groups[0][0]], 0u);
    EXPECT_EQ(groups[0].size(), 6u);
    EXPECT_EQ(groups[1].size(), 12u);
}

TEST(BreakGraph, StrengthAndRestStressRaiseThresholds) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    std::vector<float> strength(m.tets.size(), 3.0f);
    kke::BreakGraph g(m, chunk, strength);
    std::vector<float> rest(m.tets.size(), 40.0f);
    g.arm(100.0f, rest, 1.25f);
    EXPECT_FLOAT_EQ(g.threshold(0), 350.0f);
}

TEST(BreakGraph, BordersToOtherBodiesAreIgnored) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    kke::BreakGraph g(m, chunk, {});
    g.arm(1.0f);
    // Body = piece 0 alone (already split off): its border tets see no
    // neighbour in the same body, so nothing new breaks.
    auto onlyPiece0 = [&](uint32_t t) { return chunk[t] == 0; };
    for (uint32_t t = 0; t < g.tetCount(); ++t)
        if (chunk[t] == 0) {
            EXPECT_FALSE(g.report(t, 1e9f, onlyPiece0));
        }
    EXPECT_EQ(g.brokenBorderCount(), 0u);
}

// Multiplayer (docs/NETWORKING.md "Breakables"): a host's broken borders,
// applied to a second copy that never saw a stress, give the same pieces.
TEST(BreakGraph, AFollowerGivenTheHostsBordersBreaksTheSame) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    kke::BreakGraph host(m, chunk, {}), client(m, chunk, {});
    host.arm(100.0f);
    std::vector<uint32_t> tets(host.tetCount());
    for (uint32_t t = 0; t < tets.size(); ++t) tets[t] = t;
    for (uint32_t t : tets)
        if (chunk[t] == 2 && host.isBorderTet(t)) host.report(t, 500.0f, all); // the 1|2 border
    const auto borders = host.brokenBorders();
    ASSERT_EQ(borders.size(), 1u);
    EXPECT_EQ(borders[0], std::make_pair(1u, 2u));

    for (const auto& [a, b] : borders) EXPECT_TRUE(client.breakBorder(b, a)); // either order
    EXPECT_FALSE(client.breakBorder(1, 2)) << "already broken";
    EXPECT_EQ(client.brokenBorders(), borders);
    EXPECT_EQ(client.groups(tets, all), host.groups(tets, all));
}

TEST(BreakGraph, BordersThatDontExistCantBeBroken) {
    std::vector<uint32_t> chunk;
    kke::TetMeshData m = bar(chunk);
    kke::BreakGraph g(m, chunk, {});
    EXPECT_FALSE(g.breakBorder(0, 2)) << "pieces 0 and 2 don't touch";
    EXPECT_FALSE(g.breakBorder(1, 1));
    EXPECT_FALSE(g.breakBorder(7, 9000)) << "no such pieces (a message from a stranger)";
    EXPECT_EQ(g.brokenBorderCount(), 0u);
    EXPECT_TRUE(g.breakBorder(0, 1));
    EXPECT_EQ(g.brokenBorderCount(), 1u);
}
