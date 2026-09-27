#include "kke/PlayBlocks.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

// Advances a swing to its end, returning the first hit on the box (if any).
kke::BatSwing::Hit swingAt(kke::BatSwing& swing, const glm::vec3& mn, const glm::vec3& mx, float dt = 1.0f / 60.0f) {
    kke::BatSwing::Hit first;
    for (int i = 0; i < 200 && swing.update(dt); ++i) {
        kke::BatSwing::Hit h = swing.sweep(mn, mx);
        if (h.hit && !first.hit) first = h;
    }
    return first;
}

} // namespace

TEST(PlayBlocks, DefaultPaletteHasAPersonAndABat) {
    const auto blocks = kke::defaultPlayBlocks();
    auto person = std::find_if(blocks.begin(), blocks.end(), [](const kke::PlayBlock& b) { return b.id == "person"; });
    auto bat = std::find_if(blocks.begin(), blocks.end(), [](const kke::PlayBlock& b) { return b.id == "bat"; });
    ASSERT_NE(person, blocks.end());
    ASSERT_NE(bat, blocks.end());
    EXPECT_EQ(person->kind, kke::PlayBlockKind::Character);
    EXPECT_TRUE(person->randomAsset);
    EXPECT_EQ(bat->kind, kke::PlayBlockKind::Tool);
    std::set<std::string> ids;
    for (const kke::PlayBlock& b : blocks) {
        EXPECT_TRUE(ids.insert(b.id).second) << "duplicate id " << b.id;
        EXPECT_FALSE(b.label.empty());
        EXPECT_FALSE(b.assets.empty());
    }
}

TEST(PlayBlocks, AvailableAssetsKeepOrderAndSkipMissing) {
    kke::PlayBlock b{ "x", "X", kke::PlayBlockKind::Prop, { "a", "b", "c" }, false, 1.0f, {} };
    auto have = kke::availableAssets(b, [](const std::string& n) { return n != "b"; });
    EXPECT_EQ(have, (std::vector<std::string>{ "a", "c" }));
    EXPECT_TRUE(kke::availableAssets(b, {}).empty());
    EXPECT_EQ(kke::chooseAsset(b, have, 7), "a");       // not random: always the first
    EXPECT_EQ(kke::chooseAsset(b, {}, 7), "");
    b.randomAsset = true;
    EXPECT_EQ(kke::chooseAsset(b, have, 0), "a");
    EXPECT_EQ(kke::chooseAsset(b, have, 1), "c");
    EXPECT_EQ(kke::chooseAsset(b, have, 2), "a");
}

TEST(PlayBlocks, YawToFaceTurnsPlusZTowardTheViewer) {
    for (glm::vec3 viewer : { glm::vec3(0, 3, 5), glm::vec3(5, 0, 0), glm::vec3(-3, 1, -4) }) {
        const float yaw = glm::radians(kke::yawToFace(glm::vec3(0.0f), viewer));
        const glm::vec3 facing(std::sin(yaw), 0.0f, std::cos(yaw));
        EXPECT_NEAR(glm::dot(facing, glm::normalize(glm::vec3(viewer.x, 0.0f, viewer.z))), 1.0f, 1e-5f);
    }
    EXPECT_EQ(kke::yawToFace(glm::vec3(1, 0, 1), glm::vec3(1, 5, 1)), 0.0f);
}

TEST(PlayBlocks, LongAxisPointsFromTheThinEnd) {
    // A "bat" along -X: thin handle at x = +0.4, thick barrel at x = -0.4.
    std::vector<glm::vec3> pts;
    for (int i = 0; i <= 20; ++i) {
        const float x = 0.4f - 0.04f * static_cast<float>(i);
        const float r = 0.015f + 0.03f * static_cast<float>(i) / 20.0f;
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * 0.785398f;
            pts.push_back({ x, r * std::cos(a), r * std::sin(a) });
        }
    }
    const kke::LongAxis la = kke::findLongAxis(pts);
    EXPECT_NEAR(la.length, 0.8f, 1e-4f);
    EXPECT_NEAR(la.axis.x, -1.0f, 1e-6f);
    EXPECT_NEAR(la.handle.x, 0.4f, 1e-5f);
    EXPECT_EQ(kke::findLongAxis({}).length, 0.0f);
}

TEST(PlayBlocks, SwingSweepsLeftToRightAndFinishes) {
    kke::BatSwing swing;
    EXPECT_FALSE(swing.active());
    EXPECT_FALSE(swing.update(0.1f));
    ASSERT_TRUE(swing.start(glm::vec3(0, 1, 0), glm::vec3(0, 0, -1)));
    EXPECT_FALSE(swing.start(glm::vec3(0, 1, 0), glm::vec3(0, 0, -1))) << "one swing at a time";
    // Starts on the left (-X when facing -Z).
    EXPECT_LT(swing.direction().x, -0.9f);
    float last = swing.angleDegrees();
    int updates = 0;
    while (swing.hitting() && updates < 100) {
        swing.update(1.0f / 60.0f);
        EXPECT_GE(swing.angleDegrees(), last);
        last = swing.angleDegrees();
        ++updates;
    }
    EXPECT_NEAR(swing.angleDegrees(), swing.settings().endDegrees, 1e-3f);
    EXPECT_GT(swing.direction().x, 0.9f); // ends on the right
    EXPECT_TRUE(swing.active());           // follow-through
    for (int i = 0; i < 100 && swing.update(1.0f / 60.0f); ++i) {}
    EXPECT_FALSE(swing.active());
    EXPECT_TRUE(swing.start(glm::vec3(0), glm::vec3(1, 0, 0))) << "can swing again";
}

TEST(PlayBlocks, SwingAimedAtACharacterHitsItAndPushesItAway) {
    // A person-sized box 4 m in front of the camera; the bat is placed so
    // its sweet spot passes through the box's middle.
    const glm::vec3 mn(-0.25f, 0.0f, -4.25f), mx(0.25f, 1.8f, -3.75f);
    const glm::vec3 forward(0, 0, -1);
    kke::BatSwing swing;
    const glm::vec3 pivot = swing.pivotFor(glm::vec3(0, 0, -4), forward);
    EXPECT_NEAR(pivot.y, swing.settings().pivotHeight, 1e-6f);
    ASSERT_TRUE(swing.start(pivot, forward));
    const kke::BatSwing::Hit hit = swingAt(swing, mn, mx);
    ASSERT_TRUE(hit.hit);
    EXPECT_GT(hit.push.x, 1.0f);    // along the swing: left to right
    EXPECT_LT(hit.push.z, 0.0f);    // away from the swinger
    EXPECT_GT(hit.push.y, 0.0f);    // off the ground
    EXPECT_LE(glm::length(glm::vec2(hit.push.x, hit.push.z)), swing.settings().maxPushSpeed + 1e-3f);
}

TEST(PlayBlocks, SwingMissesWhatIsOutOfReach) {
    kke::BatSwing swing;
    ASSERT_TRUE(swing.start(glm::vec3(0, 1.15f, 0), glm::vec3(0, 0, -1)));
    // Far in front, behind the swinger, and above the bat.
    EXPECT_FALSE(swingAt(swing, glm::vec3(-0.3f, 0, -3.3f), glm::vec3(0.3f, 1.8f, -2.7f)).hit);
    ASSERT_TRUE(swing.start(glm::vec3(0, 1.15f, 0), glm::vec3(0, 0, -1)));
    EXPECT_FALSE(swingAt(swing, glm::vec3(-0.3f, 0, 0.7f), glm::vec3(0.3f, 1.8f, 1.3f)).hit);
    ASSERT_TRUE(swing.start(glm::vec3(0, 1.15f, 0), glm::vec3(0, 0, -1)));
    EXPECT_FALSE(swingAt(swing, glm::vec3(-0.3f, 1.5f, -1.3f), glm::vec3(0.3f, 2.0f, -0.7f)).hit);
}

TEST(PlayBlocks, BigFrameStepsStillHitThinThings) {
    // One 0.2 s frame covers most of the arc; the sweep samples in between.
    const glm::vec3 mn(-0.02f, 0.0f, -1.2f), mx(0.02f, 2.0f, -0.6f);
    kke::SwingSettings st;
    st.maxStepSeconds = 1.0f; // let the 0.2 s frame through unclamped
    kke::BatSwing swing(st);
    ASSERT_TRUE(swing.start(glm::vec3(0, 1.15f, 0), glm::vec3(0, 0, -1)));
    EXPECT_TRUE(swingAt(swing, mn, mx, 0.2f).hit);
}

TEST(PlayBlocks, FollowThroughDoesNotHit) {
    kke::BatSwing swing;
    ASSERT_TRUE(swing.start(glm::vec3(0, 1.15f, 0), glm::vec3(0, 0, -1)));
    while (swing.hitting()) swing.update(1.0f / 60.0f);
    // The bat rests pointing right; a box placed on it now isn't hit.
    const glm::vec3 tip = swing.pivot() + swing.direction() * 0.8f;
    swing.update(1.0f / 60.0f);
    EXPECT_FALSE(swing.sweep(tip - glm::vec3(0.2f), tip + glm::vec3(0.2f)).hit);
}

TEST(PlayBlocks, AHitchNeitherSkipsTheSwingNorWeakensTheHit) {
    const glm::vec3 mn(-0.25f, 0.0f, -4.25f), mx(0.25f, 1.8f, -3.75f);
    const glm::vec3 forward(0, 0, -1);
    kke::BatSwing smooth;
    ASSERT_TRUE(smooth.start(smooth.pivotFor(glm::vec3(0, 0, -4), forward), forward));
    const kke::BatSwing::Hit a = swingAt(smooth, mn, mx);
    ASSERT_TRUE(a.hit);

    // A one-second frame (the bat model loading) only advances the swing
    // by maxStepSeconds: it is still going afterwards.
    kke::BatSwing hitched;
    ASSERT_TRUE(hitched.start(hitched.pivotFor(glm::vec3(0, 0, -4), forward), forward));
    hitched.update(1.0f);
    EXPECT_TRUE(hitched.hitting());

    // Even a whole swing in one frame pushes as hard as a smooth one:
    // the push is the swing's speed where it hit, not angle / frame time.
    kke::SwingSettings st;
    st.maxStepSeconds = 1.0f;
    kke::BatSwing oneFrame(st);
    ASSERT_TRUE(oneFrame.start(oneFrame.pivotFor(glm::vec3(0, 0, -4), forward), forward));
    const kke::BatSwing::Hit b = swingAt(oneFrame, mn, mx, 1.0f);
    ASSERT_TRUE(b.hit);
    EXPECT_NEAR(glm::length(b.push), glm::length(a.push), 0.15f * glm::length(a.push));
}

TEST(PlayBlocks, EveryHitKnocksOver) {
    // Something the bat only grazes at the very start of the swing, where
    // it barely moves, still gets at least minPushSpeed.
    kke::BatSwing swing;
    const glm::vec3 pivot(0, 1.15f, 0);
    ASSERT_TRUE(swing.start(pivot, glm::vec3(0, 0, -1)));
    const glm::vec3 tip = pivot + swing.direction(swing.settings().startDegrees) * 1.0f;
    const kke::BatSwing::Hit hit = swingAt(swing, tip - glm::vec3(0.05f), tip + glm::vec3(0.05f));
    ASSERT_TRUE(hit.hit);
    EXPECT_LT(hit.degrees, swing.settings().startDegrees + 5.0f);
    EXPECT_GE(glm::length(glm::vec2(hit.push.x, hit.push.z)), swing.settings().minPushSpeed - 1e-3f);
}

TEST(PlayBlocks, PadPointerDeadzoneCurveAndScreenScale) {
    EXPECT_EQ(kke::padPointerStep({ 0.15f, 0.0f }, 0.016f, 720.0f), glm::vec2(0.0f)) << "drift inside the deadzone";
    EXPECT_EQ(kke::padPointerStep({ 1.0f, 0.0f }, 0.0f, 720.0f), glm::vec2(0.0f));
    const glm::vec2 full = kke::padPointerStep({ 1.0f, 0.0f }, 1.0f, 720.0f);
    EXPECT_NEAR(full.x, 900.0f, 1e-2f);
    EXPECT_NEAR(full.y, 0.0f, 1e-5f);
    const glm::vec2 half = kke::padPointerStep({ 0.0f, 0.6f }, 1.0f, 720.0f);
    EXPECT_GT(half.y, 0.0f);
    EXPECT_LT(half.y, 900.0f * 0.3f) << "small tilts move slowly (curve)";
    // A 1440-pixel screen is crossed in the same time: twice the pixels.
    EXPECT_NEAR(kke::padPointerStep({ 1.0f, 0.0f }, 1.0f, 1440.0f).x, 1800.0f, 1e-1f);
    // Past full tilt (diagonal of a square gate) doesn't go faster.
    EXPECT_NEAR(glm::length(kke::padPointerStep({ 1.0f, 1.0f }, 1.0f, 720.0f)), 900.0f, 1e-1f);
}

TEST(PlayBlocks, ShoulderButtonsStepThroughThePalette) {
    const std::vector<glm::vec2> cells{ { 100, 650 }, { 184, 650 }, { 268, 650 } };
    EXPECT_EQ(kke::stepPaletteCell({}, { 0, 0 }, 1), -1);
    EXPECT_EQ(kke::stepPaletteCell(cells, { 640, 200 }, 1), 0) << "from the world: the first cell";
    EXPECT_EQ(kke::stepPaletteCell(cells, { 640, 200 }, -1), 2) << "or the last";
    EXPECT_EQ(kke::stepPaletteCell(cells, { 105, 640 }, 1), 1);
    EXPECT_EQ(kke::stepPaletteCell(cells, { 184, 650 }, -1), 0);
    EXPECT_EQ(kke::stepPaletteCell(cells, { 268, 650 }, 1), 0) << "wraps";
    EXPECT_EQ(kke::stepPaletteCell(cells, { 100, 650 }, -1), 2);
}
