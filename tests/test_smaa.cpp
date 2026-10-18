#include "kke/Smaa.h"

#include <gtest/gtest.h>

#include <cmath>

// ENG-F05: anti-aliasing from the shape of the edges, one frame, no history.

namespace {

// A dark half-plane below the line y = slope * x + offset, on a lit background.
kke::SmaaImage slanted(float slope, float offset) {
    kke::SmaaImage img;
    img.width = 32;
    img.height = 16;
    img.light = [slope, offset](int x, int y) {
        const float line = slope * (static_cast<float>(x) + 0.5f) + offset;
        return static_cast<float>(y) + 0.5f > line ? glm::vec3(0.02f) : glm::vec3(1.0f);
    };
    return img;
}

} // namespace

TEST(Smaa, AStepOfASlantedLineSplitsByTheLine) {
    // A run of four pixels from an upward step to a downward one: the line falls from +0.5 to -0.5.
    // The second pixel (before 1, after 2) has the line from +0.25 to 0 over it: a quarter-wide triangle.
    const glm::vec2 a = kke::smaaLineArea(1, 2, 0.5f, -0.5f);
    EXPECT_NEAR(a.x, 0.125f, 1e-5f);
    EXPECT_NEAR(a.y, 0.0f, 1e-6f);
    const glm::vec2 b = kke::smaaLineArea(2, 1, 0.5f, -0.5f); // mirrored: below the edge
    EXPECT_NEAR(b.x, 0.0f, 1e-6f);
    EXPECT_NEAR(b.y, 0.125f, 1e-5f);
}

TEST(Smaa, AStraightEdgeStaysSharp) {
    kke::SmaaImage img;
    img.width = 16;
    img.height = 8;
    img.light = [](int, int y) { return y < 4 ? glm::vec3(1.0f) : glm::vec3(0.0f); };
    const kke::SmaaSettings s;
    const std::vector<glm::vec4> w = kke::smaaWeights(img.width, img.height, kke::smaaEdges(img, s), s);
    for (const glm::vec4& v : w) EXPECT_FLOAT_EQ(v.x + v.y + v.z + v.w, 0.0f);
}

TEST(Smaa, AFaintDifferenceIsNoEdge) {
    kke::SmaaImage img;
    img.width = 8;
    img.height = 8;
    img.light = [](int x, int) { return glm::vec3(0.50f + 0.002f * static_cast<float>(x)); };
    const kke::SmaaSettings s;
    for (const glm::bvec2& e : kke::smaaEdges(img, s)) EXPECT_FALSE(e.x || e.y);
}

TEST(Smaa, ASlantedEdgeGetsInBetweenShades) {
    // A gentle slope: steps every four pixels. Pixels along the line are mixed; far from it they stay.
    const kke::SmaaImage img = slanted(0.25f, 4.0f);
    const kke::SmaaSettings s;
    const std::vector<glm::vec4> w = kke::smaaWeights(img.width, img.height, kke::smaaEdges(img, s), s);
    const std::vector<glm::vec3> out = kke::smaaBlend(img, w);
    int mixed = 0;
    for (const glm::vec3& c : out) mixed += c.x > 0.05f && c.x < 0.97f;
    EXPECT_GT(mixed, 10);
    EXPECT_FLOAT_EQ(out[0].x, 1.0f);                               // far above the line
    EXPECT_FLOAT_EQ(out[static_cast<size_t>(15 * 32)].x, 0.02f);   // far below it
}

TEST(Smaa, BlendingKeepsTheImageAsBrightOverall) {
    // What one pixel gives up its neighbour takes in: the mean barely moves.
    const kke::SmaaImage img = slanted(0.3f, 3.0f);
    const kke::SmaaSettings s;
    const std::vector<glm::vec3> out = kke::smaaBlend(img, kke::smaaWeights(img.width, img.height, kke::smaaEdges(img, s), s));
    double before = 0.0, after = 0.0;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            before += img.light(x, y).x;
            after += out[static_cast<size_t>(y * img.width + x)].x;
        }
    EXPECT_NEAR(after / before, 1.0, 0.02);
}

TEST(Smaa, CloserToTheTrueCoverageThanAliased) {
    // The true picture: each pixel's share of the dark half-plane, by 16 x 16 samples. The blended
    // image must be nearer to it than the aliased one, for gentle and steep lines alike.
    for (float slope : { 0.2f, 0.45f, 2.5f }) {
        const float offset = 8.0f - slope * 16.0f;
        const kke::SmaaImage img = slanted(slope, offset);
        const kke::SmaaSettings s;
        const std::vector<glm::vec3> out = kke::smaaBlend(img, kke::smaaWeights(img.width, img.height, kke::smaaEdges(img, s), s));
        double aliased = 0.0, blended = 0.0;
        for (int y = 2; y < img.height - 2; ++y)
            for (int x = 2; x < img.width - 2; ++x) {
                int dark = 0;
                for (int j = 0; j < 16; ++j)
                    for (int i = 0; i < 16; ++i) {
                        const float px = static_cast<float>(x) + (static_cast<float>(i) + 0.5f) / 16.0f;
                        const float py = static_cast<float>(y) + (static_cast<float>(j) + 0.5f) / 16.0f;
                        dark += py > slope * px + offset;
                    }
                const double truth = 1.0 - 0.98 * dark / 256.0;
                aliased += std::pow(img.light(x, y).x - truth, 2.0);
                blended += std::pow(out[static_cast<size_t>(y * img.width + x)].x - truth, 2.0);
            }
        EXPECT_LT(blended, aliased * 0.6) << "slope " << slope;
    }
}
