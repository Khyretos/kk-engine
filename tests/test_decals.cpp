// Decals (ENG-F12): the box each one projects, how much of it shows where,
// its fade, the set of them, and the textures made in code.

#include "kke/Decals.h"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace kke;

namespace {

glm::vec3 toLocal(const Decal& d, const glm::vec3& world) { return glm::vec3(decalWorldToLocal(d) * glm::vec4(world, 1.0f)); }

} // namespace

TEST(Decals, BoxMapsTheSpotAndItsEdges) {
    Decal d;
    d.position = glm::vec3(3.0f, 1.0f, -2.0f);
    d.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    d.size = 2.0f;
    d.depth = 0.5f;
    EXPECT_NEAR(glm::length(toLocal(d, d.position)), 0.0f, 1e-5f);
    EXPECT_NEAR(toLocal(d, d.position + glm::vec3(0.0f, 0.5f, 0.0f)).z, 1.0f, 1e-5f); // its depth along the normal
    const glm::vec3 side = toLocal(d, d.position + glm::vec3(1.0f, 0.0f, 0.0f));         // half its size across
    EXPECT_NEAR(std::max(std::abs(side.x), std::abs(side.y)), 1.0f, 1e-5f);
    EXPECT_NEAR(side.z, 0.0f, 1e-5f);
}

TEST(Decals, DepthDefaultsToAQuarterOfTheSize) {
    Decal d;
    d.normal = glm::vec3(1.0f, 0.0f, 0.0f); // on a wall
    d.size = 4.0f;
    EXPECT_NEAR(toLocal(d, glm::vec3(1.0f, 0.0f, 0.0f)).z, 1.0f, 1e-5f);
}

TEST(Decals, AngleTurnsItRoundItsNormal) {
    Decal d;
    d.size = 2.0f;
    const glm::vec3 a = toLocal(d, glm::vec3(1.0f, 0.0f, 0.0f));
    d.angle = glm::half_pi<float>();
    const glm::vec3 b = toLocal(d, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(glm::dot(glm::vec2(a), glm::vec2(b)), 0.0f, 1e-5f); // a quarter turn
    EXPECT_NEAR(glm::length(glm::vec2(b)), 1.0f, 1e-5f);
}

TEST(Decals, WeightInsideFacingAndFadingAtItsEnds) {
    EXPECT_FLOAT_EQ(decalWeight(glm::vec3(0.0f), 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(decalWeight(glm::vec3(1.2f, 0.0f, 0.0f), 1.0f), 0.0f); // outside
    EXPECT_FLOAT_EQ(decalWeight(glm::vec3(0.0f, 0.0f, 1.01f), 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(decalWeight(glm::vec3(0.0f), 0.0f), 0.0f);             // a wall under a floor decal: nothing
    EXPECT_GT(decalWeight(glm::vec3(0.0f, 0.0f, 0.7f), 1.0f), 0.0f);
    EXPECT_LT(decalWeight(glm::vec3(0.0f, 0.0f, 0.7f), 1.0f), 1.0f);       // fading toward the end of its depth
}

TEST(Decals, FadeAfterItsLife) {
    Decal d;
    EXPECT_FLOAT_EQ(decalFade(d, 1000.0f), 1.0f); // life 0: stays
    d.life = 5.0f;
    d.fade = 2.0f;
    EXPECT_FLOAT_EQ(decalFade(d, 4.0f), 1.0f);
    EXPECT_NEAR(decalFade(d, 6.0f), 0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(decalFade(d, 8.0f), 0.0f);
}

TEST(Decals, SetPlacesRemovesAndDropsTheOldestWhenFull) {
    DecalSet set;
    Decal d;
    const DecalId first = set.place(d, 0.0f, 0);
    const DecalId second = set.place(d, 0.0f, 1);
    EXPECT_NE(first, second);
    EXPECT_EQ(set.count(), 2u);
    EXPECT_TRUE(set.remove(first));
    EXPECT_FALSE(set.remove(first));
    for (size_t i = 0; i < DecalSet::kMax + 10; ++i) set.place(d, 0.0f, 0);
    EXPECT_EQ(set.count(), DecalSet::kMax);
    EXPECT_FALSE(set.remove(second)); // pushed out
    set.clear();
    EXPECT_EQ(set.count(), 0u);
}

TEST(Decals, SetFadesAndForgetsExpiredOnes) {
    DecalSet set;
    Decal d;
    d.life = 1.0f;
    d.fade = 1.0f;
    d.opacity = 0.8f;
    set.place(d, 10.0f, 3);
    std::vector<GpuDecal> out;
    std::vector<glm::vec4> spheres;
    set.build(11.5f, out, spheres);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0].colorOpacity.a, 0.4f, 1e-6f);   // half way through its fade
    EXPECT_FLOAT_EQ(out[0].params.x, 3.0f);            // its texture layer
    EXPECT_GT(spheres[0].w, 0.5f * std::sqrt(2.0f));   // reaches its corners
    set.update(12.5f);
    EXPECT_EQ(set.count(), 0u);
}

TEST(Decals, BuiltInTexturesAreMadeTheSameEveryTime) {
    for (const std::string& name : decalBuiltIns()) {
        const std::vector<uint8_t> a = decalTexture(name, 64), b = decalTexture(name, 64);
        ASSERT_EQ(a.size(), 64u * 64u * 4u) << name;
        EXPECT_EQ(a, b) << name;
        int covered = 0;
        for (size_t i = 3; i < a.size(); i += 4) covered += a[i] > 128;
        EXPECT_GT(covered, 20) << name;             // something to see
        EXPECT_LT(covered, 64 * 64 * 9 / 10) << name; // not a square
    }
    EXPECT_TRUE(decalTexture("no such decal", 64).empty());
}

TEST(Decals, ScorchIsDarkInTheMiddleAndClearAtTheCorners) {
    const std::vector<uint8_t> t = decalTexture("scorch", 64);
    const uint8_t* middle = &t[(32 * 64 + 32) * 4];
    EXPECT_GT(middle[3], 200);
    EXPECT_LT(middle[0], 120);
    EXPECT_EQ(t[3], 0); // a corner
}
