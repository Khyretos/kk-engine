#include "kke/VolumetricFog.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// ENG-F03: volumetric fog's slices and its integration along a column.

TEST(VolumetricFog, SlicesSpanNearToFar) {
    const kke::VolumetricFogSettings s;
    EXPECT_NEAR(kke::volumetricSliceDistance(s, 0.0f), s.nearDistance, 1e-5f);
    EXPECT_NEAR(kke::volumetricSliceDistance(s, static_cast<float>(s.slices)), s.farDistance, 1e-3f);
    // Thin near the camera, thick far away.
    const float first = kke::volumetricSliceDistance(s, 1.0f) - kke::volumetricSliceDistance(s, 0.0f);
    const float last = kke::volumetricSliceDistance(s, static_cast<float>(s.slices)) -
                       kke::volumetricSliceDistance(s, static_cast<float>(s.slices) - 1.0f);
    EXPECT_LT(first * 10.0f, last);
}

TEST(VolumetricFog, SliceOfUndoesSliceDistance) {
    const kke::VolumetricFogSettings s;
    for (float x : { 0.0f, 3.25f, 17.5f, 40.0f, 48.0f })
        EXPECT_NEAR(kke::volumetricSliceOf(s, kke::volumetricSliceDistance(s, x)), x, 1e-3f);
}

TEST(VolumetricFog, EvenFogMatchesBeerLambert) {
    // The same fog in every cell: the transmittance at the far edge is exp(-sigma * depth), and what is
    // scattered is the fog's colour times what did not get through, whatever the slice sizes.
    const kke::VolumetricFogSettings s;
    const float sigma = 0.03f;
    const std::vector<kke::FogCell> column(s.slices, kke::FogCell{ glm::vec3(0.5f, 0.6f, 0.7f), sigma });
    const std::vector<glm::vec4> out = kke::integrateFogColumn(s, column);
    ASSERT_EQ(out.size(), s.slices);
    const float depth = s.farDistance - s.nearDistance;
    EXPECT_NEAR(out.back().a, std::exp(-sigma * depth), 1e-4f);
    EXPECT_NEAR(out.back().g, 0.6f * (1.0f - std::exp(-sigma * depth)), 1e-4f);
    for (size_t i = 1; i < out.size(); ++i) EXPECT_LE(out[i].a, out[i - 1].a); // never clears up further out
}

TEST(VolumetricFog, NoFogLetsEverythingThrough) {
    const kke::VolumetricFogSettings s;
    const std::vector<glm::vec4> out = kke::integrateFogColumn(s, std::vector<kke::FogCell>(s.slices));
    EXPECT_FLOAT_EQ(out.back().a, 1.0f);
    EXPECT_FLOAT_EQ(out.back().r, 0.0f);
}

TEST(VolumetricFog, AShadowedStretchScattersLess) {
    // A shaft: the middle third of the column is in the sun's shadow (darker fog there).
    const kke::VolumetricFogSettings s;
    std::vector<kke::FogCell> lit(s.slices, kke::FogCell{ glm::vec3(1.0f), 0.02f }), shaded = lit;
    for (uint32_t i = s.slices / 3; i < 2 * s.slices / 3; ++i) shaded[i].radiance = glm::vec3(0.3f);
    const float a = kke::integrateFogColumn(s, lit).back().r, b = kke::integrateFogColumn(s, shaded).back().r;
    EXPECT_LT(b, a);
    EXPECT_NEAR(kke::integrateFogColumn(s, lit).back().a, kke::integrateFogColumn(s, shaded).back().a, 1e-6f); // same fog, same transmittance
}
