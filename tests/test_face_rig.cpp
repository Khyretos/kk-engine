#include "kke/FaceRig.h"

#include <gtest/gtest.h>

#include <algorithm>

using kke::FaceRig;

namespace {

// Apple ARKit's blend shape names (the ones a face rig needs here).
const std::vector<std::string> kArkit = { "eyeBlinkLeft", "eyeBlinkRight", "jawOpen", "mouthSmileLeft", "mouthSmileRight",
                                          "cheekSquintLeft", "cheekSquintRight", "browInnerUp", "mouthFrownLeft", "mouthFrownRight",
                                          "mouthPucker", "mouthFunnel", "mouthClose" };
const std::vector<std::string> kVrm = { "happy", "angry", "sad", "blink", "aa", "ih", "ou", "ee", "oh" };
const std::vector<std::string> kVroid = { "Fcl_ALL_Joy", "Fcl_EYE_Close", "Fcl_MTH_A", "Fcl_MTH_I" };

void settle(FaceRig& rig, float seconds = 1.5f) {
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) rig.update(1.0f / 60.0f);
}

float weight(const FaceRig& rig, const std::string& morph) {
    auto it = rig.weights().find(morph);
    return it == rig.weights().end() ? 0.0f : it->second;
}

} // namespace

TEST(FaceRig, ResolvesExpressionsAgainstWhateverNamesTheModelUses) {
    FaceRig arkit, vrm, vroid;
    arkit.bind(kArkit);
    vrm.bind(kVrm);
    vroid.bind(kVroid);
    EXPECT_TRUE(arkit.canDo("happy")) << "ARKit has no 'happy', but it has both smiles";
    EXPECT_TRUE(vrm.canDo("happy"));
    EXPECT_TRUE(vroid.canDo("happy"));
    EXPECT_TRUE(arkit.canDo("blink"));
    EXPECT_TRUE(vroid.canDo("blink"));
    EXPECT_TRUE(arkit.canDo("aa")) << "the open mouth is jawOpen";
    EXPECT_FALSE(arkit.canDo("angry")) << "no brow-down shapes: no recipe is complete";
    EXPECT_FALSE(vroid.canDo("sad"));
    EXPECT_FALSE(vroid.canDo("nonsense"));
    arkit.setExpression("happy", 1.0f);
    settle(arkit);
    EXPECT_NEAR(weight(arkit, "mouthSmileLeft"), 1.0f, 1e-3f);
    EXPECT_NEAR(weight(arkit, "mouthSmileRight"), 1.0f, 1e-3f);
    EXPECT_NEAR(weight(arkit, "cheekSquintLeft"), 0.5f, 1e-3f);
    vroid.setExpression("happy", 0.5f);
    settle(vroid);
    EXPECT_NEAR(weight(vroid, "Fcl_ALL_Joy"), 0.5f, 1e-3f);
    EXPECT_FALSE(arkit.setExpression("angry", 1.0f)) << "an expression the model cannot do says so";
}

TEST(FaceRig, NamesMatchIgnoringCaseAndSeparators) {
    FaceRig rig;
    rig.bind({ "Viseme_AA", "eye_blink_left", "EYE-BLINK-RIGHT" });
    EXPECT_TRUE(rig.canDo("aa"));
    EXPECT_TRUE(rig.canDo("blink"));
    EXPECT_TRUE(rig.setMorph("VISEME-aa", 0.5f));
    settle(rig);
    EXPECT_NEAR(weight(rig, "Viseme_AA"), 0.5f, 1e-3f) << "the model's own spelling comes out";
}

TEST(FaceRig, ExpressionsEaseInAndOutAndNeverPassOne) {
    FaceRig rig;
    rig.bind(kVrm);
    rig.setAutoBlink(false);
    rig.setExpression("happy", 1.0f);
    rig.update(1.0f / 60.0f);
    const float first = weight(rig, "happy");
    EXPECT_GT(first, 0.0f);
    EXPECT_LT(first, 0.5f) << "it eases in, it does not snap";
    settle(rig);
    EXPECT_NEAR(weight(rig, "happy"), 1.0f, 1e-3f);
    rig.setExpression("happy", 2.5f);
    settle(rig);
    EXPECT_LE(weight(rig, "happy"), 1.0f);
    rig.setExpression("neutral", 0.0f);
    settle(rig);
    EXPECT_TRUE(rig.weights().empty()) << "back to a still face: only non-zero weights are listed";
}

TEST(FaceRig, TwoExpressionsOnOneMorphAddButStayUnderOne) {
    FaceRig rig;
    rig.bind(kArkit);
    rig.setAutoBlink(false);
    rig.setExpression("aa", 0.8f);   // jawOpen 0.8
    rig.setExpression("ou", 1.0f);   // pucker 1, jawOpen 0.2
    settle(rig);
    EXPECT_NEAR(weight(rig, "jawOpen"), 1.0f, 1e-3f);
}

TEST(FaceRig, BlinksByItselfAndOpensAgain) {
    FaceRig rig;
    rig.bind(kVrm);
    float most = 0.0f;
    int closedFrames = 0, frames = 0;
    for (float t = 0.0f; t < 12.0f; t += 1.0f / 60.0f) {
        rig.update(1.0f / 60.0f);
        const float w = weight(rig, "blink");
        most = std::max(most, w);
        if (w > 0.5f) ++closedFrames;
        ++frames;
    }
    EXPECT_GT(most, 0.7f) << "at least one blink in twelve seconds, at intervals of two to six";
    EXPECT_LT(closedFrames, frames / 4) << "and the eyes are open most of the time";
    settle(rig, 1.0f);
    // Switched off, it never blinks.
    FaceRig still;
    still.bind(kVrm);
    still.setAutoBlink(false);
    for (float t = 0.0f; t < 12.0f; t += 1.0f / 60.0f) {
        still.update(1.0f / 60.0f);
        ASSERT_EQ(weight(still, "blink"), 0.0f);
    }
}

TEST(FaceRig, ASpokenLineMovesTheMouthThroughItsVowels) {
    const auto keys = FaceRig::lipSyncFromText("Hello", 1.0f);
    ASSERT_GE(keys.size(), 3u);
    std::vector<std::string> shapes;
    for (const auto& k : keys)
        if (k.weight > 0.0f) shapes.push_back(k.viseme);
    EXPECT_EQ(shapes, (std::vector<std::string>{ "ee", "oh" })) << "h-E-ll-O";
    EXPECT_EQ(keys.back().viseme, "sil");
    EXPECT_NEAR(keys.back().time, 1.0f, 1e-5f);
    for (size_t i = 1; i < keys.size(); ++i) EXPECT_GE(keys[i].time, keys[i - 1].time);
    EXPECT_TRUE(FaceRig::lipSyncFromText("", 1.0f).empty());
    EXPECT_TRUE(FaceRig::lipSyncFromText("hi", 0.0f).empty());

    FaceRig rig;
    rig.bind(kVrm);
    rig.setAutoBlink(false);
    rig.say({ { 0.0f, "aa", 1.0f }, { 0.5f, "oh", 1.0f }, { 1.0f, "sil", 0.0f } });
    EXPECT_TRUE(rig.speaking());
    for (float t = 0.0f; t < 0.4f; t += 1.0f / 60.0f) rig.update(1.0f / 60.0f);
    EXPECT_GT(weight(rig, "aa"), 0.8f);
    EXPECT_EQ(weight(rig, "oh"), 0.0f);
    for (float t = 0.0f; t < 0.4f; t += 1.0f / 60.0f) rig.update(1.0f / 60.0f);
    EXPECT_GT(weight(rig, "oh"), 0.8f);
    EXPECT_LT(weight(rig, "aa"), 0.05f) << "the shape before it let go";
    settle(rig, 1.0f);
    EXPECT_FALSE(rig.speaking());
    EXPECT_TRUE(rig.weights().empty()) << "and the mouth is closed when the line is over";
}

TEST(FaceRig, AVoiceLevelOpensTheJawAndClosesItWhenSilent) {
    FaceRig rig;
    rig.bind(kArkit);
    rig.setAutoBlink(false);
    rig.setSpeechLevel(0.9f);
    settle(rig, 0.6f);
    EXPECT_TRUE(rig.speaking());
    float jaw = 0.0f;
    for (float t = 0.0f; t < 1.0f; t += 1.0f / 60.0f) {
        rig.update(1.0f / 60.0f);
        jaw = std::max(jaw, weight(rig, "jawOpen"));
    }
    EXPECT_GT(jaw, 0.5f);
    rig.setSpeechLevel(0.0f);
    settle(rig, 1.5f);
    EXPECT_FALSE(rig.speaking());
    EXPECT_NEAR(weight(rig, "jawOpen"), 0.0f, 1e-3f);
}

TEST(FaceRig, VersionChangesOnlyWhenTheFaceDoes) {
    FaceRig rig;
    rig.bind(kVrm);
    rig.setAutoBlink(false);
    settle(rig, 0.2f);
    const unsigned still = rig.version();
    settle(rig, 0.2f);
    EXPECT_EQ(rig.version(), still) << "a face at rest costs the caller nothing";
    rig.setExpression("sad", 1.0f);
    rig.update(1.0f / 60.0f);
    EXPECT_NE(rig.version(), still);
    rig.clear();
    settle(rig);
    EXPECT_TRUE(rig.weights().empty());
    EXPECT_EQ(rig.expressions().size(), 9u) << "happy angry sad blink aa ih ou ee oh";
}
