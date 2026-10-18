#include "kke/Bloom.h"

#include <gtest/gtest.h>

#include <cmath>

// ENG-F10: bloom's halvings and its build back up.

namespace {

kke::BloomImage flat(int w, int h, const glm::vec3& c) {
    kke::BloomImage img;
    img.width = w;
    img.height = h;
    img.light.assign(static_cast<size_t>(w * h), c);
    return img;
}

} // namespace

TEST(Bloom, LevelsStopBeforeTheImageVanishes) {
    EXPECT_EQ(kke::bloomLevels(1920, 1080, 6), 6u);
    EXPECT_EQ(kke::bloomLevels(16, 16, 6), 3u); // 8, 4, 2
    EXPECT_EQ(kke::bloomLevels(2, 2, 6), 0u);
}

TEST(Bloom, AFlatImageBloomsToItself) {
    // No threshold, normalised: an even light stays exactly that bright.
    const kke::BloomImage out = kke::bloom(flat(64, 48, glm::vec3(0.5f, 1.0f, 2.0f)), {});
    for (const glm::vec3& c : out.light) {
        EXPECT_NEAR(c.x, 0.5f, 1e-4f);
        EXPECT_NEAR(c.z, 2.0f, 1e-4f);
    }
}

TEST(Bloom, ABrightSpotSpreadsEvenlyAndFadesWithDistance) {
    kke::BloomImage img = flat(64, 64, glm::vec3(0.0f));
    for (int y = 30; y < 34; ++y)
        for (int x = 30; x < 34; ++x) img.light[static_cast<size_t>(y * 64 + x)] = glm::vec3(10.0f);
    const kke::BloomImage out = kke::bloom(img, {});
    ASSERT_EQ(out.width, 32);
    auto at = [&](int x, int y) { return out.light[static_cast<size_t>(y * 32 + x)].x; };
    EXPECT_GT(at(16, 16), at(20, 16));
    EXPECT_GT(at(20, 16), at(26, 16));
    EXPECT_GT(at(26, 16), 0.0f);                // reaches far
    EXPECT_NEAR(at(20, 16), at(16, 20), 0.02f * at(16, 16) + 1e-4f); // round, not a cross
}

TEST(Bloom, OneHotPixelCannotFlareIntoABlob) {
    // A single pixel a thousand times brighter than the rest: the first halving weighs it down (Karis).
    kke::BloomImage img = flat(32, 32, glm::vec3(0.1f));
    img.light[static_cast<size_t>(16 * 32 + 16)] = glm::vec3(1000.0f);
    const kke::BloomImage half = kke::bloomDownsample(img, true), plain = kke::bloomDownsample(img, false);
    // Where it is only one of the boxes a pixel averages, its weight sinks: the light it spreads is far less.
    float spread = 0.0f, spreadPlain = 0.0f;
    for (const glm::vec3& c : half.light) spread += c.x;
    for (const glm::vec3& c : plain.light) spreadPlain += c.x;
    EXPECT_LT(spread, spreadPlain * 0.6f);
}
