// Haptics (ENG-F48, kke/Haptics.h): the mixer that feeds each player's pad.

#include "kke/Haptics.h"

#include <gtest/gtest.h>

using namespace kke;

TEST(Haptics, APulseFadesAndEnds) {
    HapticMixer m;
    HapticPulse p;
    p.low = 1.0f;
    p.seconds = 1.0f;
    p.decay = 0.5f;
    m.play(p);
    EXPECT_FLOAT_EQ(m.update(0.25f).low, 1.0f) << "full strength before the decay";
    EXPECT_FLOAT_EQ(m.update(0.5f).low, 1.0f) << "t = 0.25";
    EXPECT_NEAR(m.update(0.25f).low, 0.5f, 1e-5f) << "t = 0.75: halfway down the fade";
    m.update(0.25f); // t = 1.0 now
    EXPECT_EQ(m.active(), 0u);
    EXPECT_FLOAT_EQ(m.update(0.1f).low, 0.0f);
}

TEST(Haptics, SourcesAddUpAndClip) {
    HapticMixer m;
    HapticPulse a, b;
    a.low = 0.7f;
    a.seconds = 1.0f;
    a.decay = 0.0f;
    b = a;
    b.high = 0.3f;
    m.play(a);
    m.play(b);
    const HapticMix mix = m.update(0.01f);
    EXPECT_FLOAT_EQ(mix.low, 1.0f) << "0.7 + 0.7 clips to 1";
    EXPECT_FLOAT_EQ(mix.high, 0.3f);
}

TEST(Haptics, LevelsStayUntilZero) {
    HapticMixer m;
    HapticPulse engine;
    ASSERT_TRUE(hapticPreset("engine", 1.0f, engine));
    m.setLevel("engine", 0.5f, engine);
    for (int i = 0; i < 100; ++i) m.update(0.1f);
    EXPECT_NEAR(m.update(0.1f).low, engine.low * 0.5f, 1e-6f) << "still on after 10 s";
    m.setLevel("engine", 1.0f, engine);
    EXPECT_NEAR(m.update(0.1f).low, engine.low, 1e-6f) << "updated, not added twice";
    m.setLevel("engine", 0.0f, engine);
    EXPECT_EQ(m.active(), 0u);
}

TEST(Haptics, VibrationSettingScalesAndSilences) {
    HapticMixer m;
    HapticPulse p;
    ASSERT_TRUE(hapticPreset("impact", 1.0f, p));
    m.setScale(0.5f);
    m.play(p);
    EXPECT_NEAR(m.update(0.0f).low, p.low * 0.5f, 1e-6f);
    m.setScale(0.0f);
    m.play(p);
    const HapticMix off = m.update(0.0f);
    EXPECT_EQ(off.low, 0.0f);
    EXPECT_EQ(off.high, 0.0f);
}

TEST(Haptics, PresetsAndBeats) {
    HapticPulse p;
    EXPECT_FALSE(hapticPreset("wobble", 1.0f, p));
    EXPECT_GE(hapticPresetNames().size(), 8u);
    ASSERT_TRUE(hapticPreset("heartbeat", 1.0f, p));
    EXPECT_GT(hapticEnvelope(p, 0.05f), 0.0f) << "a beat";
    EXPECT_EQ(hapticEnvelope(p, 0.5f), 0.0f) << "the pause between beats";
    ASSERT_TRUE(hapticPreset("explosion", 0.5f, p));
    EXPECT_FLOAT_EQ(p.low, 0.5f) << "strength scales the motors";
    EXPECT_LT(hapticEnvelope(p, 0.0f), 1.0f) << "an attack ramps in";
    EXPECT_EQ(hapticEnvelope(p, p.seconds), 0.0f);
}
