#include <map>
#include "kke/MeltVolume.h"
#include "kke/ParticleFluid.h"

#include <gtest/gtest.h>

namespace {
kke::MeltVolume makeCube() {
    kke::MeltVolume v(glm::ivec3(10), glm::vec3(-0.5f, 0.0f, -0.5f), 0.1f);
    v.fillBox(glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 0.6f, 0.3f), -10.0f);
    v.material().meltingPoint = 0.0f;
    return v;
}
} // namespace

TEST(MeltVolume, SurfaceIsClosedAroundTheSolid) {
    kke::MeltVolume v = makeCube();
    ASSERT_TRUE(v.rebuildMesh());
    EXPECT_FALSE(v.rebuildMesh()); // nothing changed: no rebuild
    ASSERT_GT(v.meshIndices().size(), 0u);
    const glm::vec3 c(0.0f, 0.3f, 0.0f);
    size_t outward = 0;
    for (size_t i = 0; i < v.meshPositions().size(); ++i) {
        const glm::vec3& p = v.meshPositions()[i];
        EXPECT_LT(glm::length(p - c), 0.7f);
        outward += glm::dot(p - c, v.meshNormals()[i]) > 0.0f;
    }
    EXPECT_GT(outward, v.meshPositions().size() * 9 / 10);
    // Every edge used by exactly two triangles = watertight.
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    const auto& ix = v.meshIndices();
    for (size_t t = 0; t < ix.size(); t += 3)
        for (int e = 0; e < 3; ++e) {
            uint32_t a = ix[t + e], b = ix[t + (e + 1) % 3];
            ++edges[{ std::min(a, b), std::max(a, b) }];
        }
    for (const auto& [e, n] : edges) EXPECT_EQ(n, 2);
}

TEST(MeltVolume, SignedDistanceSign) {
    kke::MeltVolume v = makeCube();
    glm::vec3 n;
    EXPECT_LT(v.signedDistance(glm::vec3(0.0f, 0.3f, 0.0f), n), 0.0f);
    EXPECT_GT(v.signedDistance(glm::vec3(0.0f, 0.9f, 0.0f), n), 0.0f);
    EXPECT_GT(v.signedDistance(glm::vec3(0.0f, 0.75f, 0.0f), n), 0.0f);
    EXPECT_GT(n.y, 0.5f); // above the top: normal points up
}

TEST(MeltVolume, HotLiquidMeltsItAndMeltBecomesLiquid) {
    kke::MeltVolume v = makeCube();
    kke::ParticleFluid::Params fp;
    fp.radius = 0.03f;
    kke::ParticleFluid fluid(fp, 2000);
    fluid.addCollider([&](const glm::vec3& p, glm::vec3& n) { return v.signedDistance(p, n); });
    // A blanket of 1200 C "lava" on top.
    for (int x = -4; x <= 4; ++x)
        for (int z = -4; z <= 4; ++z) fluid.add(glm::vec3(x * 0.06f, 0.65f, z * 0.06f), glm::vec3(0.0f), 1200.0f, 0);
    const size_t lava = fluid.size();
    size_t emitted = 0;
    for (int i = 0; i < 120; ++i) {
        fluid.step(1.0f / 60.0f);
        emitted += v.step(1.0f / 60.0f, fluid);
    }
    EXPECT_LT(v.solidFraction(), 1.0f);
    EXPECT_GT(emitted, 0u);
    EXPECT_EQ(fluid.size(), lava + emitted);
    EXPECT_LT(fluid.temperatures()[0], 1200.0f); // the lava lost heat to the ice
}

TEST(MeltVolume, NoHeatNoMelt) {
    kke::MeltVolume v = makeCube();
    kke::ParticleFluid fluid(kke::ParticleFluid::Params{}, 10);
    for (int i = 0; i < 60; ++i) v.step(1.0f / 60.0f, fluid);
    EXPECT_FLOAT_EQ(v.solidFraction(), 1.0f);
}
