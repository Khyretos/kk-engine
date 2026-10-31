#include "kke/ColourBlind.h"

#include <gtest/gtest.h>

#include <glm/gtc/epsilon.hpp>

#include <cmath>

using kke::ColourBlindMode;

namespace {
bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::all(glm::epsilonEqual(a, b, eps)); }
const glm::vec3 kWhite(1.0f), kGrey(0.4f);
} // namespace

TEST(ColourBlind, NamesRoundTripAndUnknownIsOff) {
    for (ColourBlindMode m : { ColourBlindMode::Off, ColourBlindMode::Protanopia, ColourBlindMode::Deuteranopia, ColourBlindMode::Tritanopia, ColourBlindMode::Achromatopsia })
        EXPECT_EQ(kke::colourBlindModeFromName(kke::colourBlindModeName(m)), m);
    EXPECT_EQ(kke::colourBlindModeFromName("rainbow"), ColourBlindMode::Off);
    EXPECT_EQ(kke::colourBlindModeFromName(""), ColourBlindMode::Off);
}

TEST(ColourBlind, OffAndZeroStrengthChangeNothing) {
    const glm::vec3 c(0.7f, 0.2f, 0.4f);
    EXPECT_TRUE(near(kke::colourBlindCorrection(ColourBlindMode::Off) * c, c));
    for (ColourBlindMode m : { ColourBlindMode::Protanopia, ColourBlindMode::Deuteranopia, ColourBlindMode::Tritanopia, ColourBlindMode::Achromatopsia }) {
        EXPECT_TRUE(near(kke::colourBlindCorrection(m, 0.0f) * c, c)) << kke::colourBlindModeName(m);
        EXPECT_TRUE(near(kke::colourBlindSimulation(m, 0.0f) * c, c)) << kke::colourBlindModeName(m);
    }
}

TEST(ColourBlind, GreysStayGrey) {
    // White and every grey must come out unchanged: the matrices keep neutral colours neutral.
    for (ColourBlindMode m : { ColourBlindMode::Protanopia, ColourBlindMode::Deuteranopia, ColourBlindMode::Tritanopia, ColourBlindMode::Achromatopsia }) {
        for (float s : { 0.5f, 1.0f }) {
            EXPECT_TRUE(near(kke::colourBlindCorrection(m, s) * kWhite, kWhite, 2e-3f)) << kke::colourBlindModeName(m);
            EXPECT_TRUE(near(kke::colourBlindCorrection(m, s) * kGrey, kGrey, 2e-3f)) << kke::colourBlindModeName(m);
            EXPECT_TRUE(near(kke::colourBlindSimulation(m, s) * kGrey, kGrey, 2e-3f)) << kke::colourBlindModeName(m);
        }
    }
}

TEST(ColourBlind, AchromatopsiaRemovesColourButKeepsLightness) {
    const glm::mat3 m = kke::colourBlindCorrection(ColourBlindMode::Achromatopsia, 1.0f);
    const glm::vec3 out = m * glm::vec3(0.8f, 0.1f, 0.3f);
    EXPECT_NEAR(out.r, out.g, 1e-5f);
    EXPECT_NEAR(out.g, out.b, 1e-5f);
    EXPECT_NEAR(out.r, 0.2126f * 0.8f + 0.7152f * 0.1f + 0.0722f * 0.3f, 1e-5f);
    // Half strength is halfway between the colour and its grey.
    const glm::vec3 half = kke::colourBlindCorrection(ColourBlindMode::Achromatopsia, 0.5f) * glm::vec3(0.8f, 0.1f, 0.3f);
    EXPECT_NEAR(half.r, 0.5f * (0.8f + out.r), 1e-5f);
}

TEST(ColourBlind, SimulationLosesTheAxisTheConditionLoses) {
    // A red and a green of the same blue and nearly the same lightness look alike to someone without
    // red or green cones, and the simulation brings them together.
    const glm::vec3 red(0.8f, 0.3f, 0.1f), green(0.3f, 0.55f, 0.1f);
    const float before = glm::distance(red, green);
    for (ColourBlindMode m : { ColourBlindMode::Protanopia, ColourBlindMode::Deuteranopia }) {
        const glm::mat3 sim = kke::colourBlindSimulation(m);
        EXPECT_LT(glm::distance(sim * red, sim * green), before * 0.6f) << kke::colourBlindModeName(m);
    }
    // And blue/yellow for tritanopia.
    const glm::vec3 blue(0.2f, 0.35f, 0.7f), teal(0.2f, 0.55f, 0.55f);
    const glm::mat3 trit = kke::colourBlindSimulation(ColourBlindMode::Tritanopia);
    EXPECT_LT(glm::distance(trit * blue, trit * teal), glm::distance(blue, teal) * 0.8f);
}

TEST(ColourBlind, CorrectionPullsApartColoursTheyCouldNotTell) {
    // The point of the correction is not that the picture looks the same to them, but that two colours the
    // condition makes identical stop being identical. Find the direction of colour the condition sees least,
    // take two colours along it, and check the correction makes them differ for the simulated viewer.
    for (ColourBlindMode mode : { ColourBlindMode::Protanopia, ColourBlindMode::Deuteranopia, ColourBlindMode::Tritanopia }) {
        const glm::mat3 sim = kke::colourBlindSimulation(mode);
        const glm::mat3 fix = kke::colourBlindCorrection(mode);
        glm::vec3 least(1, 0, 0);
        float smallest = 1e9f;
        for (int i = 0; i < 90; ++i) // a grid of directions over the sphere
            for (int j = 0; j < 180; ++j) {
                const float theta = 3.14159265f * (static_cast<float>(i) + 0.5f) / 90.0f, phi = 6.2831853f * static_cast<float>(j) / 180.0f;
                const glm::vec3 d(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
                const float seen = glm::length(sim * d);
                if (seen < smallest) {
                    smallest = seen;
                    least = d;
                }
            }
        const glm::vec3 a(0.5f, 0.4f, 0.3f);
        glm::vec3 b = a + 0.25f * least;
        if (glm::any(glm::lessThan(b, glm::vec3(0.0f))) || glm::any(glm::greaterThan(b, glm::vec3(1.0f)))) b = a - 0.25f * least;
        const float before = glm::distance(sim * a, sim * b);
        const float after = glm::distance(sim * (fix * a), sim * (fix * b));
        EXPECT_LT(before, 0.06f) << kke::colourBlindModeName(mode) << ": the pair really is one they cannot tell apart";
        EXPECT_GT(after, 0.15f) << kke::colourBlindModeName(mode);
        EXPECT_GT(after, before * 3.0f) << kke::colourBlindModeName(mode);
    }
}

TEST(ColourBlind, StrengthBlendsLinearly) {
    const glm::vec3 c(0.6f, 0.3f, 0.5f);
    const glm::vec3 none = c, full = kke::colourBlindCorrection(ColourBlindMode::Deuteranopia, 1.0f) * c;
    EXPECT_TRUE(near(kke::colourBlindCorrection(ColourBlindMode::Deuteranopia, 0.25f) * c, none + 0.25f * (full - none)));
    EXPECT_TRUE(near(kke::colourBlindCorrection(ColourBlindMode::Deuteranopia, 7.0f) * c, full)) << "strength is clamped to 1";
}
