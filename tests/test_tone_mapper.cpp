#include "kke/Application.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

// The enum's values are what the shaders read (LightingUBO.toneParams.x),
// so they must match the branches in shaders/tonemap.glsl.
TEST(ToneMapper, DefaultsToAgXAtUnitExposure) {
    const kke::Lighting lighting;
    EXPECT_EQ(lighting.toneMapper, kke::ToneMapper::AgX);
    EXPECT_FLOAT_EQ(lighting.exposure, 1.0f);
}

TEST(ToneMapper, ValuesMatchTheShader) {
    EXPECT_EQ(static_cast<int>(kke::ToneMapper::AgX), 0);
    EXPECT_EQ(static_cast<int>(kke::ToneMapper::ACES), 1);
    EXPECT_EQ(static_cast<int>(kke::ToneMapper::Reinhard), 2);

    std::ifstream file(std::string(KKE_SOURCE_DIR) + "/shaders/tonemap.glsl");
    ASSERT_TRUE(file.good());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string src = ss.str();
    EXPECT_NE(src.find("if (op == 1) return toneMapAces(c);"), std::string::npos);
    EXPECT_NE(src.find("if (op == 2) return c / (c + vec3(1.0));"), std::string::npos);
    EXPECT_NE(src.find("return toneMapAgX(c);"), std::string::npos);
}

TEST(ToneMapper, ParsesNamesAnyCase) {
    EXPECT_EQ(kke::parseToneMapper("agx"), kke::ToneMapper::AgX);
    EXPECT_EQ(kke::parseToneMapper("ACES"), kke::ToneMapper::ACES);
    EXPECT_EQ(kke::parseToneMapper("Reinhard"), kke::ToneMapper::Reinhard);
    EXPECT_FALSE(kke::parseToneMapper("filmic").has_value());
    EXPECT_FALSE(kke::parseToneMapper("").has_value());
}
