#include "kke/ToneMap.h"
#include "kke/Sky.h"

#include <gtest/gtest.h>

#include "tonemap_glsl.h"
#include "tonemap_post_frag.h"

#include <cmath>
#include <string>

// ENG-G17: the CPU mirror of shaders/tonemap.glsl (clear colours,
// thumbnails) and the tone-map pass's push constants. Expected values come
// from an independent NumPy version of the same formulas.

namespace {

void expectNear(const glm::vec3& got, const glm::vec3& want, float tol) {
    EXPECT_NEAR(got.r, want.r, tol);
    EXPECT_NEAR(got.g, want.g, tol);
    EXPECT_NEAR(got.b, want.b, tol);
}

kke::PostParams params(kke::ToneMapper op, float exposure = 1.0f) { return kke::makePostParams(op, exposure, kke::ColorGrade{}); }

} // namespace

TEST(ToneMapCpu, AgXMatchesTheReference) {
    const kke::PostParams p = params(kke::ToneMapper::AgX);
    expectNear(kke::toneMapCpu(glm::vec3(0.18f), p), glm::vec3(0.214467f, 0.214533f, 0.214537f), 2e-5f);
    expectNear(kke::toneMapCpu(glm::vec3(1.0f, 0.5f, 0.1f), p), glm::vec3(0.62415f, 0.439084f, 0.174697f), 2e-5f);
    expectNear(kke::toneMapCpu(glm::vec3(4.0f), p), glm::vec3(0.860622f, 0.861003f, 0.861027f), 2e-5f);
}

TEST(ToneMapCpu, ExposureMultipliesSceneLightFirst) {
    expectNear(kke::toneMapCpu(glm::vec3(0.18f), params(kke::ToneMapper::AgX, 2.0f)), glm::vec3(0.350581f, 0.350701f, 0.350708f), 2e-5f);
}

TEST(ToneMapCpu, AcesAndReinhard) {
    expectNear(kke::toneMapCpu(glm::vec3(1.0f, 0.5f, 0.1f), params(kke::ToneMapper::ACES)), glm::vec3(0.67329f, 0.438492f, 0.058844f), 2e-5f);
    expectNear(kke::toneMapCpu(glm::vec3(1.0f, 3.0f, 0.0f), params(kke::ToneMapper::Reinhard)), glm::vec3(0.5f, 0.75f, 0.0f), 1e-6f);
}

TEST(ToneMapCpu, NonePassesDisplayColoursThrough) {
    const kke::PostParams p = params(kke::ToneMapper::None);
    expectNear(kke::toneMapCpu(glm::vec3(0.25f, 0.5f, 0.75f), p), glm::vec3(0.25f, 0.5f, 0.75f), 1e-6f);
    expectNear(kke::toneMapCpu(glm::vec3(2.0f, -1.0f, 1.0f), p), glm::vec3(1.0f, 0.0f, 1.0f), 1e-6f);
}

TEST(ToneMapCpu, LookChangesTheResultOnlyWhenSet) {
    kke::ColorGrade punchy;
    ASSERT_TRUE(kke::ColorGrade::preset("punchy", punchy));
    const kke::PostParams plain = params(kke::ToneMapper::AgX);
    const kke::PostParams look = kke::makePostParams(kke::ToneMapper::AgX, 1.0f, punchy);
    EXPECT_FLOAT_EQ(plain.lookOffset.a, 0.0f);
    EXPECT_FLOAT_EQ(look.lookOffset.a, 1.0f);
    const glm::vec3 c(0.5f, 0.3f, 0.2f);
    EXPECT_GT(glm::length(kke::toneMapCpu(c, look) - kke::toneMapCpu(c, plain)), 1e-3f);
}

TEST(ToneMapCpu, PacksThePushConstants) {
    kke::ColorGrade g;
    g.slope = glm::vec3(1.1f, 1.2f, 1.3f);
    g.offset = glm::vec3(0.01f, 0.02f, 0.03f);
    g.power = glm::vec3(0.9f, 0.8f, 0.7f);
    g.saturation = 1.5f;
    const kke::PostParams p = kke::makePostParams(kke::ToneMapper::ACES, 1.25f, g);
    EXPECT_EQ(p.tone, glm::vec4(1.0f, 1.25f, 0.0f, 0.0f));
    EXPECT_EQ(p.lookSlope, glm::vec4(1.1f, 1.2f, 1.3f, 1.5f));
    EXPECT_EQ(p.lookOffset, glm::vec4(0.01f, 0.02f, 0.03f, 1.0f));
    EXPECT_EQ(p.lookPower, glm::vec4(0.9f, 0.8f, 0.7f, 0.0f));
}

TEST(ToneMapCpu, InverseRoundTripsEveryCurve) {
    for (kke::ToneMapper op : { kke::ToneMapper::AgX, kke::ToneMapper::ACES, kke::ToneMapper::Reinhard, kke::ToneMapper::None }) {
        const kke::PostParams p = params(op, 1.3f);
        for (const glm::vec3 want : { glm::vec3(0.002f, 0.002f, 0.004f), glm::vec3(0.02f, 0.02f, 0.05f), glm::vec3(0.5f, 0.25f, 0.1f),
                                      glm::vec3(0.9f, 0.9f, 0.9f) }) {
            const glm::vec3 scene = kke::inverseToneMapCpu(want, p);
            expectNear(kke::toneMapCpu(scene, p), want, 2e-4f);
        }
    }
}

TEST(ToneMapCpu, InverseOfWhiteGetsAsCloseAsTheCurveAllows) {
    const kke::PostParams p = params(kke::ToneMapper::AgX);
    const glm::vec3 got = kke::toneMapCpu(kke::inverseToneMapCpu(glm::vec3(1.0f), p), p);
    expectNear(got, glm::vec3(1.0f), 0.01f);
}

TEST(ToneMapCpu, ClearColourShowsAsAuthored) {
    // The Renderer's default background, sRGB-authored.
    const glm::vec3 srgb(0.152f, 0.152f, 0.248f);
    auto lin = [](float v) { return std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const kke::PostParams p = params(kke::ToneMapper::AgX);
    expectNear(kke::toneMapCpu(kke::sceneClearColor(srgb, p), p), glm::vec3(lin(0.152f), lin(0.152f), lin(0.248f)), 2e-4f);
}

// AgX cannot show every saturated colour (it desaturates on purpose): a
// clear colour outside its range gets the nearest one it can show.
TEST(ToneMapCpu, SaturatedClearColourGetsTheNearest) {
    auto lin = [](float v) { return std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const kke::PostParams p = params(kke::ToneMapper::AgX);
    const glm::vec3 shown = kke::toneMapCpu(kke::sceneClearColor(glm::vec3(0.35f, 0.55f, 0.85f), p), p);
    expectNear(shown, glm::vec3(lin(0.35f), lin(0.55f), lin(0.85f)), 0.01f);
}

// The None branch the C++ enum promises exists in the shader.
TEST(ToneMapCpu, ShaderHasTheNoneBranch) {
    EXPECT_EQ(static_cast<int>(kke::ToneMapper::None), 3);
    const std::string src = kTonemapGlsl;
    EXPECT_NE(src.find("if (op == 3) return clamp(c, 0.0, 1.0);"), std::string::npos);
}

// On a 10-bit (UNORM) swapchain the tone-map pass encodes sRGB itself;
// Renderer sets PostParams.tone.z for it.
TEST(ToneMapCpu, PostPassEncodesForUnormTargets) {
    const std::string src = kTonemapPostFrag;
    EXPECT_NE(src.find("if (post.tone.z > 0.5) c = linearToSrgb(c);"), std::string::npos);
}
