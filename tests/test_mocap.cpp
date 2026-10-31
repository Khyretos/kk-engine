// Motion capture: BVH import and clean-up (kke/Mocap.h, ENG-F23).
#include "kke/AnimRig.h"
#include "kke/Mocap.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>

using namespace kke;

namespace {

// A small CMU-style skeleton in centimetres: hips with a spine and head, a leg, 3 frames.
const char* kBvh = R"(HIERARCHY
ROOT Hips
{
  OFFSET 0.0 100.0 0.0
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  JOINT Spine
  {
    OFFSET 0.0 20.0 0.0
    CHANNELS 3 Zrotation Xrotation Yrotation
    JOINT Head
    {
      OFFSET 0.0 40.0 0.0
      CHANNELS 3 Zrotation Xrotation Yrotation
      End Site
      {
        OFFSET 0.0 15.0 0.0
      }
    }
  }
  JOINT LeftUpLeg
  {
    OFFSET 10.0 -5.0 0.0
    CHANNELS 3 Xrotation Yrotation Zrotation
    End Site
    {
      OFFSET 0.0 -40.0 0.0
    }
  }
}
MOTION
Frames: 3
Frame Time: 0.0333333
0 0 0   0 0 0   0 0 0   0 0 0   0 0 0
10 5 -20   90 0 0   0 0 0   0 0 0   0 0 0
0 0 0   0 0 0   0 0 0   0 0 0   90 0 0
)";

glm::mat3 rotationOf(const glm::mat4& m) { return glm::mat3(m); }

void expectMat3(const glm::mat3& a, const glm::mat3& b, float eps = 1e-5f) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) EXPECT_NEAR(a[c][r], b[c][r], eps) << "[" << c << "][" << r << "]";
}

std::string writeTemp(const std::string& name, const std::string& text) {
    const std::string path = ::testing::TempDir() + name;
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST(Bvh, ParsesTheSkeletonAndTheMotion) {
    BvhMotion m;
    std::string error;
    ASSERT_TRUE(parseBvh(kBvh, m, &error)) << error;
    ASSERT_EQ(m.joints.size(), 4u);
    EXPECT_EQ(m.joints[0].name, "Hips");
    EXPECT_EQ(m.joints[0].parent, -1);
    EXPECT_EQ(m.joints[1].name, "Spine");
    EXPECT_EQ(m.joints[1].parent, 0);
    EXPECT_EQ(m.joints[2].name, "Head");
    EXPECT_EQ(m.joints[2].parent, 1);
    EXPECT_EQ(m.joints[3].name, "LeftUpLeg");
    EXPECT_EQ(m.joints[3].parent, 0);
    EXPECT_EQ(m.joints[0].offset, glm::vec3(0, 100, 0));
    EXPECT_EQ(m.joints[0].channels.size(), 6u);
    EXPECT_EQ(m.joints[0].channels[3], BvhChannel::Zrotation);
    EXPECT_TRUE(m.joints[2].hasEndSite);
    EXPECT_EQ(m.joints[2].endSite, glm::vec3(0, 15, 0));
    EXPECT_FALSE(m.joints[1].hasEndSite);
    EXPECT_EQ(m.channelCount, 15u);
    EXPECT_EQ(m.frameCount, 3u);
    EXPECT_NEAR(m.frameTime, 0.0333333f, 1e-6f);
    EXPECT_NEAR(m.rate(), 30.0f, 0.01f);
    EXPECT_EQ(m.joints[3].firstChannel, 12u);
    EXPECT_FLOAT_EQ(m.at(1, 0), 10.0f);
    EXPECT_FLOAT_EQ(m.at(1, 3), 90.0f);
    EXPECT_FLOAT_EQ(m.at(2, 12), 90.0f);
}

TEST(Bvh, ToleratesWindowsLineEndingsAndMixamoNames) {
    std::string text = kBvh;
    std::string crlf;
    for (char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    crlf.replace(crlf.find("Hips"), 4, "mixamorig:Hips"); // the root's name only
    BvhMotion m;
    std::string error;
    ASSERT_TRUE(parseBvh(crlf, m, &error)) << error;
    EXPECT_EQ(m.joints[0].name, "mixamorig:Hips") << "a colon inside a name stays";
    EXPECT_EQ(m.frameCount, 3u);
}

TEST(Bvh, RotationsApplyInTheOrderTheChannelsListThem) {
    BvhMotion m;
    ASSERT_TRUE(parseBvh(kBvh, m));
    const ModelData model = bvhToModel(m);
    ASSERT_EQ(model.animations.size(), 1u);
    const ModelAnimation& clip = model.animations[0];
    // Hips, frame 1: Zrotation 90 (the root's first listed rotation)
    expectMat3(rotationOf(clip.frames[1][0]), glm::mat3_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1))));
    // LeftUpLeg, frame 2: Xrotation 90 listed first
    expectMat3(rotationOf(clip.frames[2][3]), glm::mat3_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0))));
    // and a rotation list in the other order gives the other product
    BvhMotion both;
    std::string text = kBvh;
    const size_t at = text.rfind("0 0 0   0 0 0   0 0 0   0 0 0   90 0 0");
    ASSERT_NE(at, std::string::npos);
    text.replace(at, std::string("0 0 0   0 0 0   0 0 0   0 0 0   90 0 0").size(), "0 0 0   30 40 0   0 0 0   0 0 0   90 45 0");
    ASSERT_TRUE(parseBvh(text, both));
    const glm::mat4 hips = bvhToModel(both).animations[0].frames[2][0];
    // Zrotation 30, Xrotation 40, Yrotation 0: R = Rz * Rx
    const glm::mat3 want = glm::mat3_cast(glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::radians(40.0f), glm::vec3(1, 0, 0)));
    expectMat3(rotationOf(hips), want);
    const glm::mat4 leg = bvhToModel(both).animations[0].frames[2][3];
    // Xrotation 90, Yrotation 45: R = Rx * Ry
    expectMat3(rotationOf(leg), glm::mat3_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0)) * glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0))));
}

TEST(Bvh, CentimetresBecomeMetresAndTheRootMoves) {
    BvhMotion m;
    ASSERT_TRUE(parseBvh(kBvh, m));
    const ModelData model = bvhToModel(m);
    ASSERT_EQ(model.bones.size(), 4u);
    // a 1.2 m skeleton written in cm (the hips at 100) is guessed as cm
    EXPECT_NEAR(model.bones[0].localRest[3][1], 1.0f, 1e-5f);
    EXPECT_NEAR(model.bones[1].localRest[3][1], 0.2f, 1e-5f);
    const ModelAnimation& clip = model.animations[0];
    EXPECT_NEAR(clip.sampleRate, 30.0f, 0.01f);
    EXPECT_NEAR(clip.duration, 2.0f / 30.0f, 1e-3f) << "three frames span two frame times";
    // the root's position channels add to its offset
    EXPECT_NEAR(clip.frames[1][0][3][0], 0.10f, 1e-5f);
    EXPECT_NEAR(clip.frames[1][0][3][1], 1.05f, 1e-5f);
    EXPECT_NEAR(clip.frames[1][0][3][2], -0.20f, 1e-5f);
    // metres stay metres
    BvhImportOptions o;
    o.scale = 1.0f;
    EXPECT_NEAR(bvhToModel(m, o).bones[0].localRest[3][1], 100.0f, 1e-3f);
    BvhMotion small = m;
    for (BvhJoint& j : small.joints) j.offset *= 0.01f;
    EXPECT_NEAR(bvhToModel(small).bones[0].localRest[3][1], 1.0f, 1e-5f) << "a skeleton under 3 units tall is in metres";
}

TEST(Bvh, TheSkeletonIsAModelTheRetargeterTakes) {
    BvhMotion m;
    ASSERT_TRUE(parseBvh(kBvh, m));
    const ModelData model = bvhToModel(m);
    // parents first; bind and rest agree
    glm::mat4 world[4];
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_LT(model.bones[i].parent, static_cast<int>(i));
        world[i] = (model.bones[i].parent >= 0 ? world[model.bones[i].parent] : glm::mat4(1.0f)) * model.bones[i].localRest;
        const glm::mat4 id = world[i] * model.bones[i].inverseBind;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) EXPECT_NEAR(id[c][r], c == r ? 1.0f : 0.0f, 1e-4f);
    }
    EXPECT_EQ(model.findBone("Head"), 2);
    const BoneMatch match = matchBones(model, model);
    EXPECT_EQ(match.matched, 4);
    const std::vector<ModelAnimation> clips = retargetAnimations(model, model, match);
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_EQ(clips[0].frames.size(), 3u);
}

TEST(Bvh, ZUpFilesTurnToYUp) {
    BvhMotion m;
    ASSERT_TRUE(parseBvh(kBvh, m));
    // a Z-up skeleton: the spine goes up the Z axis
    for (BvhJoint& j : m.joints) j.offset = glm::vec3(j.offset.x, 0.0f, j.offset.y);
    BvhImportOptions o;
    o.zUp = true;
    o.scale = 0.01f;
    const ModelData model = bvhToModel(m, o);
    EXPECT_NEAR(model.bones[1].localRest[3][1], 0.2f, 1e-5f) << "up is Y now";
    EXPECT_NEAR(model.bones[1].localRest[3][2], 0.0f, 1e-5f);
    // a turn about the file's up axis (Z) is a turn about Y
    const glm::mat3 hips = rotationOf(model.animations[0].frames[1][0]);
    expectMat3(hips, glm::mat3_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))));
}

TEST(Bvh, SaysWhatIsWrongWithALine) {
    BvhMotion m;
    std::string error;
    auto bad = [&](const std::string& text, const std::string& expect) {
        EXPECT_FALSE(parseBvh(text, m, &error)) << expect;
        EXPECT_NE(error.find(expect), std::string::npos) << "'" << error << "' should say '" << expect << "'";
    };
    bad("", "HIERARCHY");
    bad("HIERARCHY\nMOTION\n", "no ROOT");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 3 Xrotation Yrotation Wrotation\n}\n", "line 5: unknown channel 'Wrotation'");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 x\n}\n", "line 4: 'x' is not a number");
    bad("HIERARCHY\nROOT A\n{\nCHANNELS 3 Xrotation Yrotation Zrotation\n}\n", "has no OFFSET");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\n}\n", "has no CHANNELS");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 9 Xrotation\n}\n", "0 to 6 channels");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n", "the file ends inside joint 'A'");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\nBOGUS\n}\n", "unexpected 'BOGUS'");
    const std::string head = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\nMOTION\n";
    bad(head + "Frames: 2\nFrame Time: 0.03\n1\n", "the file ends in frame 1");
    bad(head + "Frames: 1\nFrame Time: 0.03\n1\n2\n", "more numbers than 1 frames");
    bad(head + "Frames: 1\nFrame Time: 0\n1\n", "frame time must be above 0");
    bad(head + "Frames: 1.5\nFrame Time: 0.03\n1\n", "0 to 200000 frames");
    bad(head + "Frames: 1\nFrame Time: 0.03\nabc\n", "'abc' is not a number (frame 0)");
    bad(head + "Frames: 1\nFrame Time: 0.03\nnan\n", "not a number");
    bad("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\n", "MOTION");
    // too many joints
    std::string many = "HIERARCHY\nROOT J0\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n";
    for (size_t i = 1; i <= kBvhMaxJoints + 1; ++i) many += "JOINT J" + std::to_string(i) + "\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\n";
    many += "}\nMOTION\n";
    bad(many, "more than 512 joints");
    // no crash on chopped files at every length
    const std::string good = kBvh;
    for (size_t n = 0; n < good.size(); n += 7) EXPECT_NO_FATAL_FAILURE(parseBvh(good.substr(0, n), m, &error)) << n;
}

TEST(Bvh, LoadsThroughLoadModelAndReportsAMissingFile) {
    const std::string path = writeTemp("kke_walk.bvh", kBvh);
    const ModelData viaBvh = loadBvh(path);
    EXPECT_EQ(viaBvh.animations[0].name, "kke_walk") << "the clip is named for the file";
    EXPECT_EQ(viaBvh.bones.size(), 4u);
    EXPECT_EQ(viaBvh.sourcePath, path);
    const ModelData viaLoad = loadModel(path);
    EXPECT_EQ(viaLoad.bones.size(), 4u);
    EXPECT_EQ(viaLoad.animations.size(), 1u);
    EXPECT_TRUE(viaLoad.meshes.empty());
    EXPECT_THROW(loadBvh(::testing::TempDir() + "nope.bvh"), std::runtime_error);
    const std::string broken = writeTemp("kke_broken.bvh", "HIERARCHY\nnonsense");
    try {
        loadBvh(broken);
        FAIL() << "no error";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("kke_broken.bvh"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("line"), std::string::npos) << e.what();
    }
    std::filesystem::remove(path);
    std::filesystem::remove(broken);
}

// ---- clean-up ----------------------------------------------------------------------

namespace {

std::vector<Pose> bone(size_t frames, const std::function<void(size_t, BoneTRS&)>& fn) {
    std::vector<Pose> out(frames, Pose(2));
    for (size_t f = 0; f < frames; ++f) fn(f, out[f][0]);
    return out;
}

} // namespace

TEST(MocapClean, FlipsAreFixedAndNothingElseChanges) {
    auto frames = bone(20, [](size_t f, BoneTRS& b) {
        const glm::quat q = glm::angleAxis(0.05f * static_cast<float>(f), glm::vec3(0, 1, 0));
        b.r = f % 3 == 1 ? -q : q;
    });
    MotionCleanOptions o;
    const std::vector<Pose> out = cleanMotion(frames, 30.0f, o);
    ASSERT_EQ(out.size(), 20u);
    for (size_t f = 1; f < out.size(); ++f) EXPECT_GT(glm::dot(out[f - 1][0].r, out[f][0].r), 0.99f) << f;
    // a rotation is the same rotation
    for (size_t f = 0; f < out.size(); ++f) EXPECT_NEAR(std::abs(glm::dot(out[f][0].r, frames[f][0].r)), 1.0f, 1e-5f);
}

TEST(MocapClean, ASpikeGoesAndTheMotionStays) {
    auto frames = bone(30, [](size_t f, BoneTRS& b) {
        b.t = glm::vec3(0.01f * static_cast<float>(f), 1.0f, 0.0f);
        b.r = glm::angleAxis(0.02f * static_cast<float>(f), glm::vec3(1, 0, 0));
    });
    frames[10][0].r = glm::angleAxis(2.5f, glm::vec3(0, 0, 1)) * frames[10][0].r; // a glitch
    frames[20][0].t += glm::vec3(0, 0.8f, 0);                                     // and a jump of 80 cm
    MotionCleanOptions o;
    o.spikeDegrees = 30.0f;
    o.spikeMetres = 0.2f;
    MotionCleanReport report;
    const std::vector<Pose> out = cleanMotion(frames, 30.0f, o, &report);
    EXPECT_EQ(report.spikesFixed, 2u);
    const glm::quat want = glm::angleAxis(0.02f * 10.0f, glm::vec3(1, 0, 0));
    EXPECT_LT(2.0f * std::acos(std::min(1.0f, std::abs(glm::dot(out[10][0].r, want)))) * 57.3f, 0.5f);
    EXPECT_NEAR(out[20][0].t.y, 1.0f, 1e-4f);
    EXPECT_NEAR(out[5][0].t.x, 0.05f, 1e-6f) << "frames that were fine are untouched";
}

TEST(MocapClean, SmoothingTakesTheJitterAndKeepsTheMotion) {
    std::mt19937 rng(3);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    auto frames = bone(150, [&](size_t f, BoneTRS& b) {
        const float t = static_cast<float>(f) / 30.0f;
        b.t = glm::vec3(std::sin(t), 1.0f + noise(rng), noise(rng));
        b.r = glm::angleAxis(0.4f * std::sin(t) + noise(rng), glm::vec3(0, 1, 0));
    });
    MotionCleanOptions o;
    o.smoothSeconds = 0.1f;
    const std::vector<Pose> out = cleanMotion(frames, 30.0f, o);
    ASSERT_EQ(out.size(), frames.size());
    double rawJitter = 0.0, cleanJitter = 0.0;
    for (size_t f = 10; f < 140; ++f) {
        rawJitter += std::abs(frames[f][0].t.y - 1.0f);
        cleanJitter += std::abs(out[f][0].t.y - 1.0f);
    }
    EXPECT_LT(cleanJitter, rawJitter * 0.5) << "the noise is much smaller";
    for (size_t f = 10; f < 140; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        EXPECT_NEAR(out[f][0].t.x, std::sin(t), 0.03f) << "the swing is kept";
    }
    // rotations stay unit
    for (const Pose& p : out) EXPECT_NEAR(glm::length(p[0].r), 1.0f, 1e-4f);
}

TEST(MocapClean, TrimAndResample) {
    auto frames = bone(61, [](size_t f, BoneTRS& b) { b.t = glm::vec3(static_cast<float>(f), 0, 0); }); // 2 s at 30 fps
    MotionCleanOptions trim;
    trim.trimStart = 0.5f;
    trim.trimEnd = 0.5f;
    const std::vector<Pose> cut = cleanMotion(frames, 30.0f, trim);
    ASSERT_EQ(cut.size(), 31u);
    EXPECT_FLOAT_EQ(cut.front()[0].t.x, 15.0f);
    EXPECT_FLOAT_EQ(cut.back()[0].t.x, 45.0f);
    MotionCleanOptions down;
    down.resampleRate = 15.0f;
    MotionCleanReport report;
    const std::vector<Pose> half = cleanMotion(frames, 30.0f, down, &report);
    ASSERT_EQ(half.size(), 31u);
    EXPECT_FLOAT_EQ(report.sampleRate, 15.0f);
    EXPECT_NEAR(half[3][0].t.x, 6.0f, 1e-4f) << "the same moment (0.2 s)";
    MotionCleanOptions up;
    up.resampleRate = 60.0f;
    const std::vector<Pose> twice = cleanMotion(frames, 30.0f, up);
    ASSERT_EQ(twice.size(), 121u);
    EXPECT_NEAR(twice[1][0].t.x, 0.5f, 1e-4f) << "halfway between two frames";
    // trimming everything away is refused (nothing is cut)
    MotionCleanOptions all;
    all.trimStart = 5.0f;
    EXPECT_EQ(cleanMotion(frames, 30.0f, all).size(), 61u);
}

TEST(MocapClean, InPlaceTakesTheHorizontalTravelOut) {
    auto frames = bone(30, [](size_t f, BoneTRS& b) { b.t = glm::vec3(0.1f + 0.03f * static_cast<float>(f), 1.0f + 0.01f * std::sin(static_cast<float>(f)), 0.2f + 0.02f * static_cast<float>(f)); });
    MotionCleanOptions o;
    o.inPlaceBone = 0;
    MotionCleanReport report;
    const std::vector<Pose> out = cleanMotion(frames, 30.0f, o, &report);
    EXPECT_NEAR(report.travel.x, 0.03f * 29.0f, 1e-5f);
    EXPECT_NEAR(report.travel.z, 0.02f * 29.0f, 1e-5f);
    EXPECT_EQ(report.travel.y, 0.0f);
    for (size_t f = 0; f < out.size(); ++f) {
        EXPECT_NEAR(out[f][0].t.x, 0.1f, 1e-6f);
        EXPECT_NEAR(out[f][0].t.z, 0.2f, 1e-6f);
        EXPECT_NEAR(out[f][0].t.y, frames[f][0].t.y, 1e-6f) << "the bounce stays";
    }
    MotionCleanOptions none;
    none.inPlaceBone = 5; // no such bone
    EXPECT_NO_FATAL_FAILURE(cleanMotion(frames, 30.0f, none));
}

TEST(MocapClean, LoopBlendMakesTheEndFlowIntoTheStart) {
    // a swing that does not come back: the end is far from the start
    auto frames = bone(61, [](size_t f, BoneTRS& b) { b.r = glm::angleAxis(0.02f * static_cast<float>(f), glm::vec3(0, 0, 1)); });
    MotionCleanOptions o;
    o.loopBlendSeconds = 0.5f; // 15 frames
    MotionCleanReport report;
    const std::vector<Pose> loop = cleanMotion(frames, 30.0f, o, &report);
    ASSERT_EQ(loop.size(), 61u - 15u);
    EXPECT_EQ(report.framesAfter, 46u);
    auto angle = [](const glm::quat& a, const glm::quat& b) {
        const glm::quat d = a * glm::conjugate(b);
        return 2.0f * std::atan2(glm::length(glm::vec3(d.x, d.y, d.z)), std::abs(d.w)) * 57.3f;
    };
    // consecutive frames differ by about the same small step everywhere, including from the last to the first
    const float step = angle(loop[10][0].r, loop[11][0].r);
    EXPECT_LT(angle(loop.back()[0].r, loop.front()[0].r), step * 3.0f) << "no jump at the join";
    // without it the join is a big jump
    const float jump = angle(frames.back()[0].r, frames.front()[0].r);
    EXPECT_GT(jump, 60.0f);
    // too short to blend: left alone
    MotionCleanOptions big;
    big.loopBlendSeconds = 5.0f;
    EXPECT_EQ(cleanMotion(frames, 30.0f, big).size(), 61u);
}

TEST(MocapClean, ACleanedModelClipKeepsItsShape) {
    BvhMotion m;
    ASSERT_TRUE(parseBvh(kBvh, m));
    const ModelData model = bvhToModel(m);
    MotionCleanOptions o;
    o.resampleRate = 60.0f;
    MotionCleanReport report;
    const ModelAnimation out = cleanAnimation(model.animations[0], o, &report);
    EXPECT_EQ(out.name, model.animations[0].name);
    EXPECT_FLOAT_EQ(out.sampleRate, 60.0f);
    EXPECT_EQ(out.frames.size(), 5u) << "three frames at 30 fps over 2/30 s: five at 60 fps";
    EXPECT_NEAR(out.duration, 4.0f / 60.0f, 1e-4f);
    ASSERT_EQ(out.frames[0].size(), 4u);
    // the first frame is the first frame
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(out.frames[0][0][c][r], model.animations[0].frames[0][0][c][r], 1e-5f);
    // a looped one: duration spans every frame
    MotionCleanOptions loopy;
    std::vector<Pose> many(40, Pose(1));
    ModelAnimation long_;
    long_.sampleRate = 30.0f;
    for (const Pose& p : many) {
        long_.frames.emplace_back();
        poseToLocals(p, long_.frames.back());
    }
    loopy.loopBlendSeconds = 0.2f;
    const ModelAnimation looped = cleanAnimation(long_, loopy);
    EXPECT_EQ(looped.frames.size(), 34u);
    EXPECT_NEAR(looped.duration, 34.0f / 30.0f, 1e-4f) << "a loop plays its last frame into its first";
}
