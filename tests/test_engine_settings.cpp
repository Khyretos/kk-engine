#include "kke/EngineSettings.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

using kke::EngineSettings;

TEST(EngineSettings, DefaultsRoundTripThroughJson) {
    EngineSettings defaults;
    EXPECT_EQ(kke::settingsFromJson(kke::settingsToJson(defaults)), defaults);
}

TEST(EngineSettings, ModifiedValuesRoundTrip) {
    EngineSettings s;
    s.graphics.fullscreen = true;
    s.graphics.vsync = false;
    s.graphics.frameRateLimit = 144.0f;
    s.graphics.fieldOfView = 90.0f;
    s.graphics.shadows = false;
    s.graphics.brightness = 1.5f;
    s.graphics.uiScale = 1.25f;
    s.graphics.showDebugOverlay = false;
    s.audio.master = 10;
    s.audio.music = 0;
    s.audio.effects = 100;
    s.audio.muteWhenUnfocused = false;
    s.controls.mouseSensitivity = 2.5f;
    s.controls.invertY = true;
    s.controls.keyBindings["jump"] = "J";
    s.gameplay.difficulty = "hard";
    s.gameplay.maxPhysicsStepsPerFrame = 4;
    s.gameplay.showDamageNumbers = false;
    s.custom["favoriteColor"] = "teal";
    EXPECT_EQ(kke::settingsFromJson(kke::settingsToJson(s)), s);
}

TEST(EngineSettings, MissingKeysKeepDefaults) {
    EngineSettings s = kke::settingsFromJson(R"({"graphics": {"fieldOfView": 75}})");
    EXPECT_FLOAT_EQ(s.graphics.fieldOfView, 75.0f);
    EXPECT_TRUE(s.graphics.vsync);
    EXPECT_EQ(s.audio.master, 80);
    EXPECT_EQ(s.controls.keyBindings.at("jump"), "Space");
}

TEST(EngineSettings, WrongTypesAreIgnoredNotFatal) {
    EngineSettings s = kke::settingsFromJson(R"({"graphics": {"vsync": "yes", "uiScale": [1]}, "audio": "loud"})");
    EXPECT_TRUE(s.graphics.vsync);
    EXPECT_FLOAT_EQ(s.graphics.uiScale, 1.0f);
    EXPECT_EQ(s.audio.master, 80);
}

TEST(EngineSettings, OutOfRangeValuesAreClamped) {
    EngineSettings s = kke::settingsFromJson(R"({
        "graphics": {"fieldOfView": 5, "uiScale": 50, "brightness": -1, "frameRateLimit": 3},
        "audio": {"master": 400, "music": -3},
        "controls": {"mouseSensitivity": 0},
        "gameplay": {"difficulty": "nightmare", "maxPhysicsStepsPerFrame": 99}
    })");
    EXPECT_FLOAT_EQ(s.graphics.fieldOfView, 30.0f);
    EXPECT_FLOAT_EQ(s.graphics.uiScale, 2.5f);
    EXPECT_FLOAT_EQ(s.graphics.brightness, 0.0f);
    EXPECT_FLOAT_EQ(s.graphics.frameRateLimit, 15.0f);
    EXPECT_EQ(s.audio.master, 100);
    EXPECT_EQ(s.audio.music, 0);
    EXPECT_FLOAT_EQ(s.controls.mouseSensitivity, 0.1f);
    EXPECT_EQ(s.gameplay.difficulty, "normal");
    EXPECT_EQ(s.gameplay.maxPhysicsStepsPerFrame, 8);
}

TEST(EngineSettings, ZeroFrameRateLimitMeansUnlimited) {
    EngineSettings s = kke::settingsFromJson(R"({"graphics": {"frameRateLimit": 0}})");
    EXPECT_FLOAT_EQ(s.graphics.frameRateLimit, 0.0f);
}

TEST(EngineSettings, KeyBindingsMergeWithDefaults) {
    EngineSettings s = kke::settingsFromJson(R"({"controls": {"keyBindings": {"jump": "J", "sprint": "Left Shift"}}})");
    EXPECT_EQ(s.controls.keyBindings.at("jump"), "J");
    EXPECT_EQ(s.controls.keyBindings.at("sprint"), "Left Shift");
    EXPECT_EQ(s.controls.keyBindings.at("move_forward"), "W"); // default kept
}

TEST(EngineSettings, InvalidJsonThrows) {
    EXPECT_THROW(kke::settingsFromJson("{not json"), std::runtime_error);
    EXPECT_THROW(kke::settingsFromJson("[1, 2, 3]"), std::runtime_error);
}

TEST(EngineSettings, InequalityOperator) {
    EngineSettings a, b;
    EXPECT_FALSE(a != b);
    b.audio.music = 1;
    EXPECT_TRUE(a != b);
}

TEST(EngineSettings, FileRoundTripAndMissingFile) {
    auto path = (std::filesystem::temp_directory_path() / "kke_settings_test.json").string();
    EngineSettings s;
    s.graphics.fieldOfView = 100.0f;
    ASSERT_TRUE(kke::saveSettingsFile(s, path));
    std::string error;
    EXPECT_EQ(kke::loadSettingsFile(path, &error), s);
    EXPECT_TRUE(error.empty());
    std::remove(path.c_str());

    EngineSettings missing = kke::loadSettingsFile(path, &error);
    EXPECT_EQ(missing, EngineSettings{});
    EXPECT_FALSE(error.empty());
}

TEST(EngineSettings, CorruptFileFallsBackToDefaults) {
    auto path = (std::filesystem::temp_directory_path() / "kke_settings_corrupt.json").string();
    { std::ofstream(path) << "{{{"; }
    std::string error;
    EXPECT_EQ(kke::loadSettingsFile(path, &error), EngineSettings{});
    EXPECT_NE(error.find("invalid JSON"), std::string::npos);
    std::remove(path.c_str());
}

TEST(EngineSettings, SaveToUnwritablePathFails) {
    EXPECT_FALSE(kke::saveSettingsFile(EngineSettings{}, "/nonexistent-dir/definitely/settings.json"));
}
