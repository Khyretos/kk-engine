#include "kke/ShadowRays.h"

#include <gtest/gtest.h>

// ENG-F07: which pixels get a shadow ray, and when last frame's ray is used.

TEST(ShadowRays, KkeTracesOnlyPastTheShadowMap) {
    const kke::ShadowRaySettings s;
    EXPECT_FALSE(kke::shadowRayTraced(kke::ShadowRays::Kke, 10.0f, s));
    EXPECT_FALSE(kke::shadowRayTraced(kke::ShadowRays::Kke, 74.0f, s));
    EXPECT_TRUE(kke::shadowRayTraced(kke::ShadowRays::Kke, 120.0f, s));
    EXPECT_FALSE(kke::shadowRayTraced(kke::ShadowRays::Kke, 900.0f, s)); // past the farthest ray
}

TEST(ShadowRays, ClassicTracesEverythingInReach) {
    const kke::ShadowRaySettings s;
    EXPECT_TRUE(kke::shadowRayTraced(kke::ShadowRays::Classic, 1.0f, s));
    EXPECT_TRUE(kke::shadowRayTraced(kke::ShadowRays::Classic, 500.0f, s));
    EXPECT_FALSE(kke::shadowRayTraced(kke::ShadowRays::Off, 500.0f, s));
}

TEST(ShadowRays, LastFramesRayIsUsedOnlyForTheSameSurface) {
    const kke::ShadowRaySettings s;
    EXPECT_FLOAT_EQ(kke::shadowRayLookup(0.0f, 100.0f, 101.0f, s), 0.0f);  // the same hillside: in shadow
    EXPECT_FLOAT_EQ(kke::shadowRayLookup(1.0f, 100.0f, 140.0f, s), -1.0f); // something else was there: the map answers
    EXPECT_FLOAT_EQ(kke::shadowRayLookup(1.0f, 0.0f, 100.0f, s), -1.0f);   // not traced
}

TEST(ShadowRays, Names) {
    EXPECT_EQ(kke::shadowRaysFromName("kke", kke::ShadowRays::Off), kke::ShadowRays::Kke);
    EXPECT_EQ(kke::shadowRaysFromName("classic", kke::ShadowRays::Off), kke::ShadowRays::Classic);
    EXPECT_EQ(kke::shadowRaysFromName("rtx", kke::ShadowRays::Off), kke::ShadowRays::Off);
}
