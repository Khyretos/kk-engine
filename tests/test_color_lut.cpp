#include "kke/ColorLut.h"

#include <gtest/gtest.h>

#include <string>

// ENG-F10 part 2: .cube LUTs and the tetrahedral read.

TEST(ColorLut, TheIdentityTableChangesNothing) {
    const kke::ColorLut lut = kke::ColorLut::identity(17);
    for (const glm::vec3 c : { glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.2f, 0.7f, 0.4f), glm::vec3(0.93f, 0.01f, 0.5f) }) {
        const glm::vec3 out = lut.apply(c);
        EXPECT_NEAR(out.r, c.r, 1e-5f);
        EXPECT_NEAR(out.g, c.g, 1e-5f);
        EXPECT_NEAR(out.b, c.b, 1e-5f);
    }
}

TEST(ColorLut, ReadsACubeFile) {
    // A 2-point table that swaps red and blue.
    const std::string text = "# made by hand\nTITLE \"swap\"\nLUT_3D_SIZE 2\n"
                             "0 0 0\n0 0 1\n0 1 0\n0 1 1\n1 0 0\n1 0 1\n1 1 0\n1 1 1\n";
    kke::ColorLut lut;
    std::string error;
    ASSERT_TRUE(kke::parseCubeLut(text, lut, error)) << error;
    EXPECT_EQ(lut.size, 2);
    EXPECT_EQ(lut.title, "swap");
    const glm::vec3 out = lut.apply(glm::vec3(0.9f, 0.5f, 0.1f));
    EXPECT_NEAR(out.r, 0.1f, 1e-5f);
    EXPECT_NEAR(out.g, 0.5f, 1e-5f);
    EXPECT_NEAR(out.b, 0.9f, 1e-5f);
}

TEST(ColorLut, SaysWhatIsWrongWithABadFile) {
    kke::ColorLut lut;
    std::string error;
    EXPECT_FALSE(kke::parseCubeLut("0 0 0\n", lut, error));
    EXPECT_NE(error.find("LUT_3D_SIZE"), std::string::npos);
    EXPECT_FALSE(kke::parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", lut, error));
    EXPECT_NE(error.find("needs 8 entries"), std::string::npos);
    EXPECT_FALSE(kke::parseCubeLut("LUT_1D_SIZE 16\n", lut, error));
}

TEST(ColorLut, GreysStayGreyThroughATintFreeTable) {
    // A table that raises contrast evenly on all channels: greys in must come out grey, between samples too
    // (a tetrahedral read keeps the diagonal; trilinear would tint it with off-diagonal corners).
    kke::ColorLut lut = kke::ColorLut::identity(5);
    for (glm::vec3& v : lut.table) v = glm::clamp((v - 0.5f) * 1.4f + 0.5f, 0.0f, 1.0f);
    for (float g = 0.0f; g <= 1.0f; g += 0.07f) {
        const glm::vec3 out = lut.apply(glm::vec3(g));
        EXPECT_NEAR(out.r, out.g, 1e-5f);
        EXPECT_NEAR(out.g, out.b, 1e-5f);
    }
}
