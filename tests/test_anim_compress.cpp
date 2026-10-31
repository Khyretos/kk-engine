#include "kke/AnimCompress.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

using kke::AnimCompressOptions;
using kke::ModelAnimation;
using kke::PackedAnimation;

namespace {

glm::mat4 trs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    glm::mat4 m = glm::mat4_cast(r);
    m[0] *= s.x;
    m[1] *= s.y;
    m[2] *= s.z;
    m[3] = glm::vec4(t, 1.0f);
    return m;
}

// How far two local matrices are apart: translation in metres, rotation in radians, scale as a distance.
struct Gap {
    float translation = 0, rotation = 0, scale = 0;
};
Gap gapBetween(const glm::mat4& a, const glm::mat4& b) {
    Gap g;
    g.translation = glm::distance(glm::vec3(a[3]), glm::vec3(b[3]));
    const glm::vec3 sa(glm::length(glm::vec3(a[0])), glm::length(glm::vec3(a[1])), glm::length(glm::vec3(a[2])));
    const glm::vec3 sb(glm::length(glm::vec3(b[0])), glm::length(glm::vec3(b[1])), glm::length(glm::vec3(b[2])));
    g.scale = glm::distance(sa, sb);
    const glm::quat qa = glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(a[0]) / sa.x, glm::vec3(a[1]) / sa.y, glm::vec3(a[2]) / sa.z)));
    const glm::quat qb = glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(b[0]) / sb.x, glm::vec3(b[1]) / sb.y, glm::vec3(b[2]) / sb.z)));
    const glm::quat r = glm::conjugate(qa) * qb; // acos(dot) cannot resolve angles under ~0.0007 in float: use the vector part
    g.rotation = 2.0f * std::atan2(glm::length(glm::vec3(r.x, r.y, r.z)), std::fabs(r.w));
    return g;
}

// Five bones over three seconds at 30 fps: a walking root, a swinging limb, a bone that never moves, a scaled
// one, and one that spins through several full turns (its quaternion changes sign along the way).
std::vector<std::vector<glm::mat4>> clip() {
    std::vector<std::vector<glm::mat4>> frames;
    for (int f = 0; f <= 90; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        std::vector<glm::mat4> bones;
        bones.push_back(trs({ 0.0f, 0.9f + 0.03f * std::sin(t * 12.0f), 0.9f * t }, glm::angleAxis(0.1f * std::sin(t * 6.0f), glm::vec3(0, 1, 0)), glm::vec3(1.0f)));
        bones.push_back(trs({ 0.0f, 0.4f, 0.0f }, glm::angleAxis(0.6f * std::sin(t * 9.4f), glm::vec3(1, 0, 0)), glm::vec3(1.0f)));
        bones.push_back(trs({ 0.1f, 0.2f, -0.3f }, glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 2, 3))), glm::vec3(1.0f)));
        bones.push_back(trs({ 0.0f, 0.1f, 0.0f }, glm::angleAxis(1.5707963f, glm::vec3(0, 0, 1)), glm::vec3(1.0f, 2.0f, 1.0f)));
        bones.push_back(trs({ 0.0f, 0.0f, 0.0f }, glm::angleAxis(4.0f * 3.14159265f * t / 3.0f, glm::vec3(0, 1, 0)), glm::vec3(1.0f)));
        frames.push_back(bones);
    }
    return frames;
}

} // namespace

TEST(AnimCompress, RoundTripStaysWithinTheTolerance) {
    const auto frames = clip();
    const AnimCompressOptions o;
    const PackedAnimation packed = kke::packFrames(frames, o);
    ASSERT_EQ(packed.frameCount, frames.size());
    ASSERT_EQ(packed.tracks.size(), 5u);
    const auto back = kke::unpackFrames(packed);
    ASSERT_EQ(back.size(), frames.size());
    Gap worst;
    for (size_t f = 0; f < frames.size(); ++f)
        for (size_t b = 0; b < 5; ++b) {
            const Gap g = gapBetween(frames[f][b], back[f][b]);
            worst.translation = std::max(worst.translation, g.translation);
            worst.rotation = std::max(worst.rotation, g.rotation);
            worst.scale = std::max(worst.scale, g.scale);
        }
    EXPECT_LE(worst.translation, o.translationError) << "metres";
    EXPECT_LE(worst.rotation, o.rotationError * 1.5f) << "radians, including the 16-bit quantising";
    EXPECT_LE(worst.scale, o.scaleError);
}

TEST(AnimCompress, ItIsSmallerAndAConstantTrackIsOneKey) {
    const auto frames = clip();
    const PackedAnimation packed = kke::packFrames(frames);
    const size_t dense = frames.size() * 5 * sizeof(glm::mat4);
    EXPECT_LT(packed.bytes() * 3, dense) << "at least three times smaller than a matrix per bone per frame";
    // Bone 2 never moves; bone 3 only has a scale; every channel that is constant has exactly one key.
    EXPECT_EQ(packed.tracks[2].rot.size(), 1u);
    EXPECT_EQ(packed.tracks[2].pos.size(), 1u);
    EXPECT_EQ(packed.tracks[2].scl.size(), 1u);
    EXPECT_EQ(packed.tracks[3].scl.size(), 1u);
    EXPECT_EQ(packed.tracks[1].pos.size(), 1u) << "a limb that only rotates has one translation key";
    EXPECT_GT(packed.tracks[1].rot.size(), 4u) << "while the swing needs keys";
    EXPECT_LT(packed.tracks[1].rot.size(), 91u) << "but not one a frame";
}

TEST(AnimCompress, ASpinningBoneSurvivesTheQuaternionSignFlips) {
    const auto frames = clip();
    const auto back = kke::unpackFrames(kke::packFrames(frames));
    float worst = 0.0f;
    for (size_t f = 0; f < frames.size(); ++f) worst = std::max(worst, gapBetween(frames[f][4], back[f][4]).rotation);
    EXPECT_LT(worst, 0.001f) << "no spikes where q and -q swap";
}

TEST(AnimCompress, QuantisedRotationsAreAccurate) {
    // Smallest-three in 16 bits: error well under the tolerance for any rotation.
    float worst = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const float a = static_cast<float>(i) * 0.7f, b = static_cast<float>(i) * 1.3f;
        const glm::quat q = glm::normalize(glm::quat(std::cos(a), std::sin(a) * std::cos(b), std::sin(a) * std::sin(b), std::cos(b) * 0.3f));
        std::vector<std::vector<glm::mat4>> one{ { glm::mat4_cast(q) } };
        const auto back = kke::unpackFrames(kke::packFrames(one));
        worst = std::max(worst, gapBetween(glm::mat4_cast(q), back[0][0]).rotation);
    }
    EXPECT_LT(worst, 0.0001f);
}

TEST(AnimCompress, ASingleFrameAndNoFramesAreFine) {
    EXPECT_EQ(kke::packFrames({}).frameCount, 0u);
    std::vector<std::vector<glm::mat4>> one{ { glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) } };
    const auto back = kke::unpackFrames(kke::packFrames(one));
    ASSERT_EQ(back.size(), 1u);
    EXPECT_NEAR(back[0][0][3].y, 2.0f, 1e-3f);
}

TEST(AnimCompress, AWideTranslationKeepsFullPrecision) {
    // A root that travels 200 m: 16 bits would be 3 mm a step, so the track keeps full floats.
    std::vector<std::vector<glm::mat4>> frames;
    for (int f = 0; f <= 60; ++f) frames.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 200.0f * static_cast<float>(f) / 60.0f + 0.01f * std::sin(f * 1.0f))) });
    const AnimCompressOptions o;
    const auto back = kke::unpackFrames(kke::packFrames(frames, o));
    float worst = 0.0f;
    for (size_t f = 0; f < frames.size(); ++f) worst = std::max(worst, gapBetween(frames[f][0], back[f][0]).translation);
    EXPECT_LE(worst, o.translationError);
}

TEST(AnimCompress, ModelAnimationSamplesTheSameEitherWay) {
    ModelAnimation dense;
    dense.name = "walk";
    dense.sampleRate = 30.0f;
    dense.duration = 3.0f;
    dense.frames = clip();
    kke::ModelData model;
    model.animations.push_back(dense);
    const auto stats = kke::compressAnimations(model);
    ASSERT_EQ(stats.clips, 1u);
    const ModelAnimation& packed = model.animations[0];
    EXPECT_TRUE(packed.compressed());
    EXPECT_TRUE(packed.frames.empty()) << "the dense frames are gone";
    EXPECT_EQ(packed.frameCount(), 91u);
    EXPECT_GT(stats.ratio(), 3.0f);
    float worst = 0.0f;
    std::vector<glm::mat4> a, b;
    for (float f = 0.0f; f < 92.0f; f += 0.37f) { // fractional frames, and past the end
        dense.sampleLocals(f, a);
        packed.sampleLocals(f, b);
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            const Gap g = gapBetween(a[i], b[i]);
            worst = std::max({ worst, g.translation, g.rotation, g.scale });
        }
    }
    EXPECT_LT(worst, 0.002f);
    packed.localsAt(10, b);
    dense.localsAt(10, a);
    EXPECT_LT(gapBetween(a[0], b[0]).translation, 0.001f);
    // Compressing again changes nothing.
    EXPECT_EQ(kke::compressAnimations(model).clips, 0u);
}

TEST(AnimCompress, TheShippedMannequinLibraryCompressesWithinTheTolerance) {
    namespace fs = std::filesystem;
    const fs::path file = fs::path(KKE_SOURCE_DIR) / "assets/animations/UAL1_Standard.fbx";
    if (!fs::exists(file)) GTEST_SKIP() << "assets/animations/UAL1_Standard.fbx missing";
    kke::ModelData model = kke::loadModel(file.string());
    ASSERT_GT(model.animations.size(), 20u);
    const std::vector<ModelAnimation> original = model.animations;
    const AnimCompressOptions o;
    const auto stats = kke::compressAnimations(model, o);
    std::cout << "[ compress ] " << stats.clips << " clips: " << stats.denseBytes / 1024 << " KB -> " << stats.packedBytes / 1024 << " KB, "
              << stats.ratio() << "x\n";
    EXPECT_EQ(stats.clips, original.size());
    EXPECT_GT(stats.ratio(), 3.0f);
    Gap worst;
    std::vector<glm::mat4> a, b;
    for (size_t c = 0; c < original.size(); ++c)
        for (size_t f = 0; f < original[c].frames.size(); ++f) {
            original[c].localsAt(f, a);
            model.animations[c].localsAt(f, b);
            ASSERT_EQ(a.size(), b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                const Gap g = gapBetween(a[i], b[i]);
                worst.translation = std::max(worst.translation, g.translation);
                worst.rotation = std::max(worst.rotation, g.rotation);
                worst.scale = std::max(worst.scale, g.scale);
            }
        }
    std::cout << "[ compress ] worst error over every frame of every clip: " << worst.translation * 1000.0f << " mm, " << worst.rotation * 57.2958f
              << " degrees, scale " << worst.scale << "\n";
    EXPECT_LE(worst.translation, o.translationError * 1.2f);
    EXPECT_LE(worst.rotation, o.rotationError * 1.5f);
    EXPECT_LE(worst.scale, o.scaleError * 1.2f);
}
