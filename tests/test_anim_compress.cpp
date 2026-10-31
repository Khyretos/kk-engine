// Animation compression (kke/AnimCompress.h, ENG-F63).
#include "kke/AnimCompress.h"

#include "kke/ModelAsset.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

#include <algorithm>
#include <cmath>
#include <random>

using namespace kke;

namespace {

constexpr float kPi = 3.14159265f;

// A 5-bone clip at 30 fps:
//   0 swings sideways in a sine (smooth curve)
//   1 turns about Y at a constant speed (a straight line for slerp)
//   2 never moves
//   3 stands still, then snaps 20 cm up at frame 40
//   4 turns about X and Z as a wobble
std::vector<Pose> makeFrames(size_t frames) {
    std::vector<Pose> out(frames, Pose(5));
    for (size_t f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        out[f][0].t = glm::vec3(0.4f * std::sin(t * 2.0f), 0.1f, 0.0f);
        out[f][1].r = glm::angleAxis(0.5f * t, glm::vec3(0, 1, 0));
        out[f][2].t = glm::vec3(0.3f, 0.2f, 0.1f);
        out[f][2].r = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 1, 0)));
        out[f][3].t = glm::vec3(0.0f, f >= 40 ? 0.2f : 0.0f, 0.0f);
        out[f][4].r = glm::angleAxis(0.3f * std::sin(t * 3.0f), glm::vec3(1, 0, 0)) * glm::angleAxis(0.2f * std::cos(t * 2.0f), glm::vec3(0, 0, 1));
    }
    return out;
}

float rotErr(const glm::quat& a, const glm::quat& b) {
    const glm::quat d = a * glm::conjugate(b);
    return 2.0f * std::atan2(glm::length(glm::vec3(d.x, d.y, d.z)), std::abs(d.w)) * 57.29578f;
}

void expectClose(const Pose& a, const Pose& b, float pos, float rotDeg, float scl) {
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_LE(glm::length(a[i].t - b[i].t), pos) << "bone " << i;
        EXPECT_LE(rotErr(a[i].r, b[i].r), rotDeg) << "bone " << i;
        EXPECT_LE(glm::length(a[i].s - b[i].s), scl) << "bone " << i;
    }
}

} // namespace

TEST(AnimCompress, DropsWhatTheCurveDoesNotNeedAndKeepsWithinTolerance) {
    const std::vector<Pose> frames = makeFrames(120);
    PackedClip clip;
    AnimCompressionStats stats;
    std::string error;
    ASSERT_TRUE(packFrames(frames, "walk", 4.0f, 30.0f, {}, clip, &stats, &error)) << error;
    EXPECT_EQ(clip.frameCount(), 120u);
    EXPECT_EQ(clip.boneCount(), 5u);
    EXPECT_EQ(stats.rawKeys, 120u * 5u * 3u);
    EXPECT_LT(stats.keys, stats.rawKeys / 4);
    EXPECT_GT(stats.ratio(), 8.0f) << "raw " << stats.rawBytes << " packed " << stats.packedBytes;
    EXPECT_LE(stats.maxPositionError, 0.0005f + 0.0001f);
    EXPECT_LE(stats.maxRotationErrorDegrees, 0.05f + 0.01f);
    EXPECT_LE(stats.maxScaleError, 0.0005f + 1e-4f);
    // and every frame really is that close
    Pose got;
    for (size_t f = 0; f < frames.size(); ++f) {
        clip.frame(f, got);
        expectClose(got, frames[f], 0.0006f, 0.06f, 0.0006f);
    }
}

TEST(AnimCompress, ATrackThatNeverMovesIsOneKeyAndAStraightLineIsTwo) {
    const std::vector<Pose> frames = makeFrames(120);
    AnimCompressionOptions o;
    o.quantize = false;
    PackedClip clip;
    ASSERT_TRUE(packFrames(frames, "x", 4.0f, 30.0f, o, clip));
    // bone 2 never moves: 3 channels, one key each
    const std::vector<uint8_t> bytes = clip.serialize();
    PackedClip again;
    ASSERT_TRUE(PackedClip::deserialize(bytes, again));
    // count the keys of bone 2 and bone 1's rotation through a throwaway pack of single bones
    std::vector<Pose> still(120, Pose(1)), turning(120, Pose(1));
    for (size_t f = 0; f < 120; ++f) {
        still[f][0] = frames[f][2];
        turning[f][0] = frames[f][1];
    }
    PackedClip a, b;
    ASSERT_TRUE(packFrames(still, "still", 4.0f, 30.0f, o, a));
    EXPECT_EQ(a.keyCount(), 3u);
    ASSERT_TRUE(packFrames(turning, "turning", 4.0f, 30.0f, o, b));
    EXPECT_EQ(b.keyCount(), 1u + 2u + 1u) << "position and scale are still; the rotation is its two end keys";
}

TEST(AnimCompress, ASnapKeepsItsKeysSoItStaysASnap) {
    const std::vector<Pose> frames = makeFrames(120);
    std::vector<Pose> snap(120, Pose(1));
    for (size_t f = 0; f < 120; ++f) snap[f][0] = frames[f][3];
    AnimCompressionOptions o;
    o.quantize = false;
    PackedClip clip;
    AnimCompressionStats stats;
    ASSERT_TRUE(packFrames(snap, "snap", 4.0f, 30.0f, o, clip, &stats));
    Pose p;
    clip.frame(39, p);
    EXPECT_NEAR(p[0].t.y, 0.0f, 0.0006f);
    clip.frame(40, p);
    EXPECT_NEAR(p[0].t.y, 0.2f, 0.0006f);
    EXPECT_LE(stats.maxPositionError, 0.0005f + 1e-6f);
}

TEST(AnimCompress, WithoutQuantizingAZeroToleranceKeepsEveryFrameExactly) {
    const std::vector<Pose> frames = makeFrames(60);
    AnimCompressionOptions o;
    o.quantize = false;
    o.positionTolerance = o.scaleTolerance = 0.0f;
    o.rotationToleranceDegrees = 0.0f;
    PackedClip clip;
    AnimCompressionStats stats;
    ASSERT_TRUE(packFrames(frames, "exact", 2.0f, 30.0f, o, clip, &stats));
    EXPECT_LE(stats.maxPositionError, 1e-6f);
    EXPECT_LE(stats.maxRotationErrorDegrees, 0.01f);
}

TEST(AnimCompress, QuantizationErrorIsTiny) {
    const std::vector<Pose> frames = makeFrames(120);
    AnimCompressionOptions o;
    o.positionTolerance = o.scaleTolerance = 0.0f; // keep every key: the error is the 16-bit steps alone
    o.rotationToleranceDegrees = 0.0f;
    PackedClip clip;
    AnimCompressionStats stats;
    ASSERT_TRUE(packFrames(frames, "q", 4.0f, 30.0f, o, clip, &stats));
    EXPECT_LE(stats.maxPositionError, 0.0001f) << "a 0.8 m range in 65535 steps is 0.01 mm";
    EXPECT_LE(stats.maxRotationErrorDegrees, 0.01f);
    EXPECT_TRUE(clip.quantized());
}

TEST(AnimCompress, QuaternionsThatFlipSignAreTheSameRotation) {
    std::vector<Pose> frames(60, Pose(1));
    for (size_t f = 0; f < 60; ++f) {
        glm::quat q = glm::angleAxis(0.02f * static_cast<float>(f), glm::vec3(0, 1, 0));
        frames[f][0].r = f % 2 ? -q : q; // q and -q: one rotation, two signs
    }
    PackedClip clip;
    AnimCompressionStats stats;
    ASSERT_TRUE(packFrames(frames, "flip", 2.0f, 30.0f, {}, clip, &stats));
    EXPECT_LE(stats.maxRotationErrorDegrees, 0.06f);
    EXPECT_EQ(clip.keyCount(), 1u + 2u + 1u) << "the sign flips are no motion";
}

TEST(AnimCompress, SamplingPlaysLikeAnimationSetDid) {
    const std::vector<Pose> frames = makeFrames(120);
    PackedClip clip;
    ASSERT_TRUE(packFrames(frames, "play", 4.0f, 30.0f, {}, clip));
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> when(-1.0f, 9.0f);
    Pose got, a, b, want;
    for (int i = 0; i < 200; ++i) {
        const float time = when(rng);
        for (bool loop : { true, false }) {
            clip.sample(time, loop, got);
            // the original: the same two-frame blend AnimationSet::sample does
            float t = time;
            if (loop) {
                t = std::fmod(t, 4.0f);
                if (t < 0) t += 4.0f;
            } else {
                t = std::clamp(t, 0.0f, 4.0f);
            }
            const float f = t * 30.0f;
            const size_t last = frames.size() - 1;
            const size_t f0 = std::min(static_cast<size_t>(f), last);
            size_t f1 = f0 + 1;
            if (f1 > last) f1 = loop ? 0 : last;
            blendPoses(frames[f0], frames[f1], f - static_cast<float>(f0), want);
            expectClose(got, want, 0.0008f, 0.08f, 0.0008f);
        }
    }
}

TEST(AnimCompress, FileRoundTripsExactly) {
    const std::vector<Pose> frames = makeFrames(90);
    for (bool quantize : { true, false }) {
        AnimCompressionOptions o;
        o.quantize = quantize;
        PackedClip clip;
        ASSERT_TRUE(packFrames(frames, "idle_loop", 3.0f, 30.0f, o, clip));
        const std::vector<uint8_t> bytes = clip.serialize();
        EXPECT_EQ(bytes.size(), clip.byteSize()) << "quantize " << quantize;
        PackedClip back;
        std::string error;
        ASSERT_TRUE(PackedClip::deserialize(bytes, back, &error)) << error;
        EXPECT_EQ(back.name, "idle_loop");
        EXPECT_FLOAT_EQ(back.duration, 3.0f);
        EXPECT_EQ(back.quantized(), quantize);
        const std::vector<Pose> a = clip.unpack(), c = back.unpack();
        ASSERT_EQ(a.size(), c.size());
        for (size_t f = 0; f < a.size(); ++f)
            for (size_t b = 0; b < a[f].size(); ++b) {
                EXPECT_EQ(a[f][b].t, c[f][b].t);
                EXPECT_EQ(a[f][b].r, c[f][b].r);
                EXPECT_EQ(a[f][b].s, c[f][b].s);
            }
        EXPECT_EQ(back.serialize(), bytes);
    }
}

TEST(AnimCompress, QuantizedFilesAreAboutAHalfTheSizeOfFloats) {
    const std::vector<Pose> frames = makeFrames(120);
    AnimCompressionOptions q, f;
    f.quantize = false;
    PackedClip a, b;
    ASSERT_TRUE(packFrames(frames, "a", 4.0f, 30.0f, q, a));
    ASSERT_TRUE(packFrames(frames, "a", 4.0f, 30.0f, f, b));
    EXPECT_LT(a.byteSize() * 10, b.byteSize() * 8) << a.byteSize() << " vs " << b.byteSize();
}

TEST(AnimCompress, RefusesBytesThatAreNotAClip) {
    const std::vector<Pose> frames = makeFrames(40);
    PackedClip clip;
    ASSERT_TRUE(packFrames(frames, "c", 1.3f, 30.0f, {}, clip));
    const std::vector<uint8_t> good = clip.serialize();
    PackedClip out;
    std::string error;
    // cut short at every length
    for (size_t n = 0; n < good.size(); ++n) {
        const std::vector<uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_FALSE(PackedClip::deserialize(cut, out, &error)) << "length " << n;
    }
    // extra bytes
    std::vector<uint8_t> longer = good;
    longer.push_back(0);
    EXPECT_FALSE(PackedClip::deserialize(longer, out, &error));
    EXPECT_NE(error.find("extra bytes"), std::string::npos);
    // a wrong header, version, counts
    std::vector<uint8_t> bad = good;
    bad[0] = 'X';
    EXPECT_FALSE(PackedClip::deserialize(bad, out, &error));
    EXPECT_NE(error.find("wrong header"), std::string::npos);
    bad = good;
    bad[4] = 9;
    EXPECT_FALSE(PackedClip::deserialize(bad, out, &error));
    EXPECT_NE(error.find("version"), std::string::npos);
    bad = good;
    bad[20] = 0xFF;
    bad[21] = 0xFF;
    bad[22] = 0xFF; // a huge frame count
    EXPECT_FALSE(PackedClip::deserialize(bad, out, &error));
    // random corruption never crashes (and a result, when it parses, is playable)
    std::mt19937 rng(11);
    for (int i = 0; i < 400; ++i) {
        std::vector<uint8_t> noisy = good;
        for (int k = 0; k < 4; ++k) noisy[rng() % noisy.size()] = static_cast<uint8_t>(rng());
        PackedClip maybe;
        if (PackedClip::deserialize(noisy, maybe, nullptr)) {
            Pose p;
            maybe.sample(0.5f, true, p);
            maybe.frame(maybe.frameCount() - 1, p);
        }
    }
}

TEST(AnimCompress, RefusesAnEmptyOrHugeClip) {
    PackedClip clip;
    std::string error;
    EXPECT_FALSE(packFrames({}, "x", 1.0f, 30.0f, {}, clip, nullptr, &error));
    EXPECT_NE(error.find("no frames"), std::string::npos);
    EXPECT_FALSE(packFrames(std::vector<Pose>(3, Pose{}), "x", 1.0f, 30.0f, {}, clip, nullptr, &error));
    EXPECT_NE(error.find("no bones"), std::string::npos);
    EXPECT_FALSE(packFrames(std::vector<Pose>(PackedClip::kMaxFrames + 1, Pose(1)), "x", 1.0f, 30.0f, {}, clip, nullptr, &error));
    EXPECT_NE(error.find("frames (at most"), std::string::npos);
}

TEST(AnimCompress, ModelClipsPackThroughTheirMatrices) {
    const std::vector<Pose> frames = makeFrames(60);
    ModelAnimation anim;
    anim.name = "from_matrices";
    anim.duration = 2.0f;
    anim.sampleRate = 30.0f;
    for (const Pose& p : frames) {
        std::vector<glm::mat4> locals;
        poseToLocals(p, locals);
        anim.frames.push_back(locals);
    }
    PackedClip clip;
    AnimCompressionStats stats;
    std::string error;
    ASSERT_TRUE(packAnimation(anim, {}, clip, &stats, &error)) << error;
    EXPECT_EQ(clip.name, "from_matrices");
    EXPECT_GT(stats.ratio(), 4.0f);
    Pose got;
    clip.frame(33, got);
    expectClose(got, frames[33], 0.0006f, 0.06f, 0.0006f);
}

namespace {

// A skeleton of 5 bones with one clip walking 1 m/s forward on the root and swinging an arm.
ModelData walkingModel() {
    ModelData m;
    for (int i = 0; i < 5; ++i) {
        ModelBone b;
        b.name = "bone" + std::to_string(i);
        b.parent = i == 0 ? -1 : i - 1;
        m.bones.push_back(b);
    }
    ModelAnimation a;
    a.name = "Walk_Loop";
    a.duration = 2.0f;
    a.sampleRate = 30.0f;
    for (int f = 0; f < 60; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        Pose p(5);
        p[0].t = glm::vec3(0.0f, 1.0f, t);              // the root walks +Z
        p[1].r = glm::angleAxis(0.5f * std::sin(t * kPi), glm::vec3(1, 0, 0));
        a.frames.emplace_back();
        poseToLocals(p, a.frames.back());
    }
    m.animations.push_back(a);
    return m;
}

} // namespace

TEST(AnimationSetPacked, PlaysTheSameAfterCompress) {
    const ModelData model = walkingModel();
    AnimationSet set(model);
    ASSERT_EQ(set.clipCount(), 1u);
    std::vector<Pose> before;
    Pose p;
    for (float t = 0.0f; t < 2.0f; t += 0.037f) {
        set.sample(0, t, true, p);
        before.push_back(p);
    }
    AnimCompressionStats stats;
    EXPECT_EQ(set.compress({}, &stats), 1u);
    EXPECT_TRUE(set.isPacked(0));
    EXPECT_GT(stats.ratio(), 4.0f);
    size_t i = 0;
    for (float t = 0.0f; t < 2.0f; t += 0.037f) {
        set.sample(0, t, true, p);
        expectClose(p, before[i++], 0.001f, 0.1f, 0.001f);
    }
    EXPECT_EQ(set.compress({}), 0u) << "already packed";
}

TEST(AnimationSetPacked, RootMotionStillWorksAfterCompress) {
    ModelData model = walkingModel();
    AnimationSet set(model);
    set.compress({});
    ASSERT_TRUE(set.isPacked(0));
    set.extractRootMotion(model, 0);
    EXPECT_FALSE(set.isPacked(0)) << "editing the frames unpacked the clip";
    EXPECT_TRUE(set.hasRootMotion());
    EXPECT_NEAR(set.rootTravel(0, 0.0f, 1.0f, true).z, 1.0f, 0.01f);
    Pose p;
    set.sample(0, 1.0f, true, p);
    EXPECT_NEAR(p[0].t.z, 0.0f, 0.01f) << "the clip plays in place";
    // and it can be packed again
    EXPECT_EQ(set.compress({}), 1u);
    EXPECT_NEAR(set.rootTravel(0, 0.0f, 1.0f, true).z, 1.0f, 0.01f);
}

TEST(AnimationSetPacked, RemoveLiftUnpacksOnlyTheClipsItEdits) {
    ModelData model = walkingModel();
    ModelAnimation other = model.animations[0];
    other.name = "Idle_Loop";
    model.animations.push_back(other);
    AnimationSet set(model);
    EXPECT_EQ(set.compress({}), 2u);
    EXPECT_EQ(set.removeLift(model, 0, "Walk"), 1);
    EXPECT_FALSE(set.isPacked(0));
    EXPECT_TRUE(set.isPacked(1));
}

// The shipped animation library (assets/animations, CC0): real motion, not
// synthetic curves.
TEST(AnimCompress, RealClipsShrinkAndStayWithinTolerance) {
    namespace fs = std::filesystem;
    const fs::path file = fs::path(KKE_SOURCE_DIR) / "assets/animations/UAL1_Standard.fbx";
    if (!fs::exists(file)) GTEST_SKIP() << file << " missing";
    const ModelData model = loadModel(file.string());
    ASSERT_GT(model.animations.size(), 10u);
    AnimCompressionStats total;
    size_t packed = 0;
    for (const ModelAnimation& a : model.animations) {
        if (a.frames.empty() || a.frames.size() > PackedClip::kMaxFrames) continue;
        PackedClip clip;
        AnimCompressionStats s;
        std::string error;
        ASSERT_TRUE(packAnimation(a, {}, clip, &s, &error)) << a.name << ": " << error;
        EXPECT_LE(s.maxPositionError, 0.0006f) << a.name;
        EXPECT_LE(s.maxRotationErrorDegrees, 0.07f) << a.name;
        EXPECT_LE(s.maxScaleError, 0.0006f) << a.name;
        // the file reads back to the same clip
        PackedClip back;
        ASSERT_TRUE(PackedClip::deserialize(clip.serialize(), back, &error)) << error;
        total += s;
        ++packed;
    }
    ASSERT_GT(packed, 10u);
    std::printf("[ packed ] %zu clips: %.1f MB as matrices -> %.2f MB (%.1fx), worst error %.3f mm / %.3f deg\n", packed,
                static_cast<double>(total.rawBytes) / 1e6, static_cast<double>(total.packedBytes) / 1e6, static_cast<double>(total.ratio()),
                static_cast<double>(total.maxPositionError) * 1000.0, static_cast<double>(total.maxRotationErrorDegrees));
    EXPECT_GT(total.ratio(), 5.0f);
}
