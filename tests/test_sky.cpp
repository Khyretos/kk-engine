#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/Mood.h"
#include "kke/Sky.h"

#include <gtest/gtest.h>

#include "moods_data.h"

#include <cmath>
#include <set>
#include <string>

namespace {

void expectNear(const glm::vec3& a, const glm::vec3& b, float tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

// A synthetic sky: blue above, grey below, one very bright sun texel.
kke::SkyImage makeSky(uint32_t w, uint32_t h, const glm::vec3& sunDir) {
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    const glm::vec2 sunUv = kke::equirectUv(sunDir);
    const uint32_t full = w / 2;
    const int sx = static_cast<int>(sunUv.x * static_cast<float>(w));
    const int sy = static_cast<int>(sunUv.y * static_cast<float>(full));
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(full);
            glm::vec3 c = v < 0.5f ? glm::vec3(0.2f, 0.4f, 1.0f) : glm::vec3(0.3f);
            if (static_cast<int>(x) == sx && static_cast<int>(y) == sy) c = glm::vec3(5000.0f, 4500.0f, 4000.0f);
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            rgb[i] = c.r;
            rgb[i + 1] = c.g;
            rgb[i + 2] = c.b;
        }
    return kke::SkyImage(w, h, std::move(rgb));
}

} // namespace

TEST(Sky, ConstantAmbientIsExactlyTheOldFlatAmbient) {
    const glm::vec3 flat(0.22f, 0.2f, 0.18f);
    const kke::SkySH sh = kke::SkySH::constant(flat);
    for (const glm::vec3& n : { glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 1, -1)) })
        expectNear(sh.irradiance(n), flat, 1e-6f);
}

TEST(Sky, HemisphereLightsFacesByWhereTheyLook) {
    const kke::SkySH sh = kke::SkySH::hemisphere(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 up = sh.irradiance({ 0, 1, 0 }), down = sh.irradiance({ 0, -1, 0 }), side = sh.irradiance({ 1, 0, 0 });
    EXPECT_GT(up.r, 0.85f);   // mostly the sky above (exact: 1.0; order 2 rings a little)
    EXPECT_LT(up.b, 0.15f);
    EXPECT_GT(down.b, 0.85f);
    EXPECT_NEAR(side.r, 0.5f, 0.05f); // half of each
    EXPECT_NEAR(side.b, 0.5f, 0.05f);
}

TEST(Sky, EquirectMappingRoundTrips) {
    for (const glm::vec3& d : { glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(0.3f, 0.8f, 0.5f)), glm::normalize(glm::vec3(-0.7f, -0.2f, 0.1f)) })
        expectNear(kke::equirectDir(kke::equirectUv(d)), d, 1e-5f);
    // Straight ahead (-z) is the middle of the picture, up is the top row.
    EXPECT_NEAR(kke::equirectUv({ 0, 0, -1 }).x, 0.5f, 1e-6f);
    EXPECT_NEAR(kke::equirectUv({ 0, 1, 0 }).y, 0.0f, 1e-6f);
}

TEST(Sky, YawTurnsAboutTheVerticalAndBack) {
    const glm::vec3 d = glm::normalize(glm::vec3(0.3f, 0.5f, -0.8f));
    expectNear(kke::rotateYaw(kke::rotateYaw(d, 70.0f), -70.0f), d, 1e-5f);
    EXPECT_NEAR(kke::rotateYaw(d, 70.0f).y, d.y, 1e-6f);
    EXPECT_NEAR(kke::azimuthOf(kke::rotateYaw(kke::sunDirectionFrom(10.0f, 30.0f), -90.0f)), 100.0f, 1e-3f);
}

TEST(Sky, ImageAnalysisFindsTheSunAndNormalises) {
    const glm::vec3 sunDir = kke::sunDirectionFrom(120.0f, 30.0f);
    const kke::SkyImage img = makeSky(256, 128, sunDir);
    const kke::SkyImage::Analysis& a = img.analysis();
    ASSERT_TRUE(a.hasSun);
    EXPECT_NEAR(a.sunElevationDegrees, 30.0f, 2.0f);
    EXPECT_NEAR(kke::azimuthOf(a.sunDirection), 120.0f, 2.0f);
    // The sun is left out of the averages: the zenith is the plain blue, scaled.
    EXPECT_GT(a.zenith.b, a.zenith.r * 3.0f);
    const float lum = 0.2126f * 0.2f + 0.7152f * 0.4f + 0.0722f * 1.0f;
    EXPECT_NEAR(a.normalise, 0.7f / lum, 0.05f / lum);
}

TEST(Sky, CroppedImageContinuesItsLastRowDown) {
    // 256 wide, 80 tall: the top 80 of a full 128-row sphere.
    const kke::SkyImage img = makeSky(256, 80, kke::sunDirectionFrom(0.0f, 60.0f));
    EXPECT_EQ(img.fullHeight(), 128u);
    expectNear(img.sample({ 0, -1, 0 }), img.sample(kke::equirectDir({ 0.5f, 79.5f / 128.0f })), 1e-5f);
    EXPECT_EQ(img.toHalfRgba(1.0f).size(), 256u * 128u * 4u);
}

TEST(Sky, ResolverFallsBackToTheGradientWhenTheImageIsMissing) {
    kke::Sky sky;
    sky.kind = kke::Sky::Kind::Image;
    sky.image = "no/such/sky.hdr";
    kke::SkyResolver resolver;
    const kke::SkyEnvironment& env = resolver.resolve(sky, kke::Fog{}, glm::vec3(0.2f));
    EXPECT_EQ(env.kind, kke::Sky::Kind::Gradient);
    // Off: the flat ambient, untouched.
    sky.kind = kke::Sky::Kind::None;
    expectNear(resolver.resolve(sky, kke::Fog{}, glm::vec3(0.2f)).ambient.irradiance({ 0, 1, 0 }), glm::vec3(0.2f), 1e-6f);
}

TEST(Sky, GradientAmbientIsBluerAboveThanBelow) {
    kke::Sky sky;
    sky.kind = kke::Sky::Kind::Gradient;
    sky.ambientSaturation = 1.0f;
    kke::SkyResolver resolver;
    const kke::SkyEnvironment& env = resolver.resolve(sky, kke::Fog{}, glm::vec3(0.2f));
    const glm::vec3 up = env.ambient.irradiance({ 0, 1, 0 }), down = env.ambient.irradiance({ 0, -1, 0 });
    EXPECT_GT(up.b / up.r, down.b / down.r);
    EXPECT_GT(up.b, down.b);
}

TEST(ColorGrade, PresetsAndIdentity) {
    kke::ColorGrade g;
    EXPECT_TRUE(g.isIdentity());
    for (const char* name : { "punchy", "golden", "cool", "faded" }) {
        ASSERT_TRUE(kke::ColorGrade::preset(name, g)) << name;
        EXPECT_FALSE(g.isIdentity()) << name;
    }
    ASSERT_TRUE(kke::ColorGrade::preset("none", g));
    EXPECT_TRUE(g.isIdentity());
    EXPECT_FALSE(kke::ColorGrade::preset("sepia", g));
}

TEST(Mood, ParsesAGradientMoodAndAppliesIt) {
    nlohmann::json data;
    ASSERT_TRUE(kke::datafile::parse(R"(
sky:
  zenith: "#3366cc"
  horizon: [0.8, 0.9, 1.0]
sun: { azimuth: 90, elevation: 30, intensity: 2.5, color: "#ffffff" }
fill: { intensity: 0.4 }
fog: { density: 0.01, color: "#808080" }
look: golden
exposure: 1.2
)", kke::datafile::Format::Yaml, data));
    kke::Mood mood;
    std::string error;
    ASSERT_TRUE(kke::parseMood(data, ".", mood, &error)) << error;
    EXPECT_EQ(mood.sky.kind, kke::Sky::Kind::Gradient);
    EXPECT_NEAR(mood.sky.zenith.b, 0.6038f, 1e-3f); // #cc, sRGB to linear
    EXPECT_FALSE(mood.fog.colorFromSky);
    EXPECT_FALSE(mood.grade.isIdentity());

    kke::Lighting lighting;
    mood.applyTo(lighting);
    expectNear(-lighting.lights[0].direction, kke::sunDirectionFrom(90.0f, 30.0f), 1e-5f);
    EXPECT_FLOAT_EQ(lighting.lights[0].intensity, 2.5f);
    EXPECT_TRUE(lighting.lights[1].enabled);
    EXPECT_TRUE(lighting.fog.enabled);
    EXPECT_FLOAT_EQ(lighting.exposure, 1.2f);
    // The sun is in the east (azimuth 90 = +x), so its light travels west.
    EXPECT_LT(lighting.lights[0].direction.x, 0.0f);
}

TEST(Mood, RejectsMistakesByName) {
    kke::Mood mood;
    std::string error;
    EXPECT_FALSE(kke::parseMood(nlohmann::json{ { "skye", nlohmann::json::object() } }, ".", mood, &error));
    EXPECT_NE(error.find("skye"), std::string::npos);
    EXPECT_FALSE(kke::parseMood(nlohmann::json{ { "sky", { { "zenith", "blue" } } } }, ".", mood, &error));
    EXPECT_NE(error.find("sky.zenith"), std::string::npos);
    EXPECT_FALSE(kke::parseMood(nlohmann::json{ { "look", "sepia" } }, ".", mood, &error));
    EXPECT_NE(error.find("sepia"), std::string::npos);
    EXPECT_FALSE(kke::parseMood(nlohmann::json{ { "fog", { { "maxOpacity", 3 } } } }, ".", mood, &error));
    EXPECT_NE(error.find("fog.maxOpacity"), std::string::npos);
}

TEST(Mood, EveryShippedMoodParsesAndUsesAFetchedSky) {
    const std::set<std::string> skies(std::begin(kShippedSkies), std::end(kShippedSkies));
    ASSERT_GT(std::size(kShippedMoods), 5u);
    for (const ShippedMood& m : kShippedMoods) {
        nlohmann::json data;
        std::string error;
        ASSERT_TRUE(kke::datafile::parse(m.text, kke::datafile::Format::Yaml, data, &error)) << m.file << ": " << error;
        kke::Mood mood;
        ASSERT_TRUE(kke::parseMood(data, "assets/moods", mood, &error)) << m.file << ": " << error;
        EXPECT_FALSE(data.value("title", std::string()).empty()) << m.file << " has no title";
        if (data["sky"].contains("image")) {
            const std::string sky = data["sky"]["image"].get<std::string>();
            EXPECT_TRUE(skies.count(sky)) << m.file << " uses sky '" << sky << "', which cmake/skies.cmake doesn't fetch";
        }
    }
}
