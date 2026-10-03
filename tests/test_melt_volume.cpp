#include <map>
#include <algorithm>
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

// Regression (user report: melt "fills an invisible cube" before it flows):
// the grid's own boundary must not act as a wall. Just outside the grid the
// distance field has to keep growing, not collapse to one cell.
TEST(MeltVolume, GridBoundaryIsNotAWall) {
    kke::MeltVolume v = makeCube(); // grid x in [-0.5, 0.5], block x in [-0.3, 0.3]
    glm::vec3 n;
    for (float x : { 0.45f, 0.5f, 0.55f, 0.58f }) {
        float d = v.signedDistance(glm::vec3(x, 0.05f, 0.0f), n);
        EXPECT_GT(d, 0.12f) << "x=" << x;      // ~x - 0.3 away from the block
        EXPECT_GT(n.x, 0.5f) << "x=" << x;     // and pointing away from it
    }
    // A droplet sliding off the block's side keeps going outward.
    kke::ParticleFluid::Params fp;
    fp.radius = 0.03f;
    kke::ParticleFluid fluid(fp, 10);
    fluid.addCollider([&](const glm::vec3& p, glm::vec3& nn) { return v.signedDistance(p, nn); });
    fluid.add(glm::vec3(0.4f, 0.03f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 20.0f, 0);
    for (int i = 0; i < 60; ++i) fluid.step(1.0f / 60.0f);
    EXPECT_GT(fluid.positions()[0].x, 0.6f);
}

// Kees's report: "some pieces keep floating". A piece with nothing under
// it falls and lands on what's below; what stands on the ground stays.
TEST(MeltVolume, LoosePiecesFallAndLand) {
    kke::MeltVolume v(glm::ivec3(10), glm::vec3(-0.5f, 0.0f, -0.5f), 0.1f);
    v.fillBox(glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 0.2f, 0.3f), -10.0f); // a slab on the ground (2 cells)
    v.fillBox(glm::vec3(-0.1f, 0.6f, -0.1f), glm::vec3(0.1f, 0.8f, 0.1f), -10.0f); // a lump in the air above it
    v.material().meltingPoint = 0.0f;
    EXPECT_GT(v.density(glm::vec3(0.0f, 0.7f, 0.0f)), 0.9f);
    float t = 0.0f;
    for (int i = 0; i < 120 && (i < 2 || v.fallingVoxels() > 0); ++i, t += 1.0f / 60.0f) v.collapse(1.0f / 60.0f);
    EXPECT_EQ(v.fallingVoxels(), 0u) << "it landed";
    EXPECT_LT(t, 1.0f) << "a 40 cm drop takes about 0.3 s";
    EXPECT_LT(v.density(glm::vec3(0.0f, 0.7f, 0.0f)), 0.1f) << "nothing left in the air";
    EXPECT_GT(v.density(glm::vec3(0.0f, 0.25f, 0.0f)), 0.9f) << "resting on the slab";
    EXPECT_GT(v.density(glm::vec3(0.25f, 0.05f, 0.25f)), 0.9f) << "the slab didn't move";
    EXPECT_NEAR(v.solidFraction(), 1.0f, 1e-4f) << "nothing lost on the way down";
    // At rest, collapse() leaves it alone.
    v.rebuildMesh();
    v.collapse(1.0f / 60.0f);
    EXPECT_FALSE(v.rebuildMesh());
}

TEST(MeltVolume, SoftMaterialSagsIntoAHole) {
    kke::MeltVolume v(glm::ivec3(6, 8, 6), glm::vec3(-0.3f, 0.0f, -0.3f), 0.1f);
    // A bridge: two legs and a deck, warm enough to be soft but not melting.
    v.fillBox(glm::vec3(-0.3f, 0.0f, -0.1f), glm::vec3(-0.1f, 0.6f, 0.1f), 55.0f);
    v.fillBox(glm::vec3(0.1f, 0.0f, -0.1f), glm::vec3(0.3f, 0.6f, 0.1f), 55.0f);
    v.fillBox(glm::vec3(-0.3f, 0.4f, -0.1f), glm::vec3(0.3f, 0.6f, 0.1f), 55.0f);
    v.material().meltingPoint = 60.0f;
    v.material().softening = 15.0f;
    v.material().sagSpeed = 0.5f;
    EXPECT_LT(v.density(glm::vec3(0.0f, 0.05f, 0.0f)), 0.1f);
    for (int i = 0; i < 120; ++i) v.collapse(1.0f / 60.0f);
    EXPECT_GT(v.density(glm::vec3(0.0f, 0.05f, 0.0f)), 0.9f) << "the middle of the deck sagged all the way down";
    EXPECT_LT(v.density(glm::vec3(0.0f, 0.55f, 0.0f)), 0.1f) << "and left a dip where it was";
    EXPECT_GT(v.density(glm::vec3(-0.25f, 0.55f, 0.0f)), 0.9f) << "the deck over the legs stays";
    EXPECT_EQ(v.fallingVoxels(), 0u);
}
