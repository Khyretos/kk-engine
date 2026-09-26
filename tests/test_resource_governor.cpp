#include "kke/ResourceGovernor.h"

#include <gtest/gtest.h>

TEST(ResourceGovernor, LeavesRoomByDefault) {
    kke::EngineSettings s;
    kke::ResourceBudget b = kke::computeBudget(s, 8);
    EXPECT_EQ(b.workerThreads, 4);
    EXPECT_FALSE(b.useEverything);
    EXPECT_FLOAT_EQ(b.frameRateLimit, 0.0f);     // vsync paces it
    EXPECT_FLOAT_EQ(b.backgroundFrameRate, 15.0f);
    EXPECT_EQ(kke::computeBudget(s, 1).workerThreads, 1); // the 1-core floor still runs
    EXPECT_EQ(kke::computeBudget(s, 64).workerThreads, 8);
}

TEST(ResourceGovernor, CapsUnsyncedFrames) {
    kke::EngineSettings s;
    s.graphics.vsync = false;
    EXPECT_FLOAT_EQ(kke::computeBudget(s, 4).frameRateLimit, 144.0f);
    s.graphics.frameRateLimit = 60.0f; // the player's own cap wins
    EXPECT_FLOAT_EQ(kke::computeBudget(s, 4).frameRateLimit, 60.0f);
}

TEST(ResourceGovernor, UseEverythingTakesEverything) {
    kke::EngineSettings s;
    s.graphics.vsync = false;
    s.performance.useEverything = true;
    kke::ResourceBudget b = kke::computeBudget(s, 12);
    EXPECT_EQ(b.workerThreads, 12);
    EXPECT_FLOAT_EQ(b.frameRateLimit, 0.0f);
    EXPECT_FLOAT_EQ(b.backgroundFrameRate, 0.0f);
}

TEST(ResourceGovernor, ExplicitThreadCountWins) {
    kke::EngineSettings s;
    s.performance.workerThreads = 3;
    EXPECT_EQ(kke::computeBudget(s, 16).workerThreads, 3);
    s.performance.workerThreads = 500;
    s.sanitize();
    EXPECT_EQ(kke::computeBudget(s, 2).workerThreads, 4); // at most 2x the cores
}

TEST(ResourceGovernor, SettingsRoundTripAndSanitize) {
    kke::EngineSettings s;
    s.performance.useEverything = true;
    s.performance.workerThreads = 6;
    s.performance.renderScale = 0.75f;
    s.performance.backgroundFrameRate = 30.0f;
    EXPECT_EQ(kke::settingsFromJson(kke::settingsToJson(s)), s);
    kke::EngineSettings bad = kke::settingsFromJson(R"({"performance": {"renderScale": 0.1, "backgroundFrameRate": 1}})");
    EXPECT_FLOAT_EQ(bad.performance.renderScale, 0.5f);
    EXPECT_FLOAT_EQ(bad.performance.backgroundFrameRate, 5.0f);
    EXPECT_GE(kke::usableCpuCount(), 1u);
}
