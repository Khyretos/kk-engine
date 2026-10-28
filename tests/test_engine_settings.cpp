#include "kke/EngineSettings.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
    s.graphics.shadowResolution = 1024;
    s.graphics.msaa = 2;
    s.graphics.brightness = 1.5f;
    s.graphics.uiScale = 1.25f;
    s.graphics.showDebugOverlay = false;
    s.graphics.autoLod = false;
    s.graphics.lodBias = 1.75f;
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

TEST(EngineSettings, LevelOfDetailDefaultsToOnAndTheBiasIsClamped) {
    EngineSettings s;
    EXPECT_TRUE(s.graphics.autoLod);
    EXPECT_FLOAT_EQ(s.graphics.lodBias, 1.0f);
    EngineSettings wild = kke::settingsFromJson(R"({"graphics": {"lodBias": 99}})");
    wild.sanitize();
    EXPECT_FLOAT_EQ(wild.graphics.lodBias, 4.0f);
    wild.graphics.lodBias = 0.0f;
    wild.sanitize();
    EXPECT_FLOAT_EQ(wild.graphics.lodBias, 0.25f);
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
        "graphics": {"fieldOfView": 5, "uiScale": 50, "brightness": -1, "frameRateLimit": 3, "msaa": 6},
        "audio": {"master": 400, "music": -3},
        "controls": {"mouseSensitivity": 0},
        "gameplay": {"difficulty": "nightmare", "maxPhysicsStepsPerFrame": 99}
    })");
    EXPECT_FLOAT_EQ(s.graphics.fieldOfView, 30.0f);
    EXPECT_FLOAT_EQ(s.graphics.uiScale, 2.5f);
    EXPECT_FLOAT_EQ(s.graphics.brightness, 0.0f);
    EXPECT_FLOAT_EQ(s.graphics.frameRateLimit, 15.0f);
    EXPECT_EQ(s.graphics.msaa, 4); // rounded down to a supported count
    EXPECT_EQ(s.audio.master, 100);
    EXPECT_EQ(s.audio.music, 0);
    EXPECT_FLOAT_EQ(s.controls.mouseSensitivity, 0.1f);
    EXPECT_EQ(s.gameplay.difficulty, "normal");
    EXPECT_EQ(s.gameplay.maxPhysicsStepsPerFrame, 8);
}

TEST(EngineSettings, ShadowResolutionIsOneOfTheSupportedSizes) {
    EXPECT_EQ(EngineSettings{}.graphics.shadowResolution, 2048);
    auto read = [](int v) { return kke::settingsFromJson("{\"graphics\": {\"shadowResolution\": " + std::to_string(v) + "}}").graphics.shadowResolution; };
    EXPECT_EQ(read(1024), 1024);
    EXPECT_EQ(read(2048), 2048);
    EXPECT_EQ(read(100), 1024);   // too small: the smallest size
    EXPECT_EQ(read(1500), 1024);  // between: rounded down
    EXPECT_EQ(read(99999), 2048); // too big: the largest
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

TEST(EngineSettings, LanguageAndAccessibilityRoundTrip) {
    EngineSettings s;
    EXPECT_EQ(s.text.language, "auto");
    EXPECT_TRUE(s.text.subtitles);
    EXPECT_EQ(s.accessibility.colorBlind, "off");
    s.text.language = "pt-BR";
    s.text.subtitles = false;
    s.text.subtitleSize = 1.5f;
    s.text.subtitleBackground = 0.25f;
    s.accessibility.colorBlind = "deuteranopia";
    s.accessibility.colorBlindStrength = 0.5f;
    s.accessibility.highContrast = true;
    s.accessibility.reduceMotion = true;
    s.accessibility.screenReader = true;
    const EngineSettings back = kke::settingsFromJson(kke::settingsToJson(s));
    EXPECT_EQ(back, s);
    EXPECT_EQ(back.text.language, "pt-BR");
    EXPECT_EQ(back.accessibility.colorBlind, "deuteranopia");
    EXPECT_TRUE(back.accessibility.screenReader);
    EngineSettings other = s;
    other.accessibility.reduceMotion = false;
    EXPECT_NE(other, s); // each one counts in the comparison
    other = s;
    other.text.language = "nl";
    EXPECT_NE(other, s);
}

TEST(EngineSettings, LanguageAndAccessibilityAreSanitised) {
    const EngineSettings s = kke::settingsFromJson(R"({"text": {"language": "", "subtitleSize": 9, "subtitleBackground": -1},
        "accessibility": {"colorBlind": "rainbow", "colorBlindStrength": 7}})");
    EXPECT_EQ(s.text.language, "auto");
    EXPECT_FLOAT_EQ(s.text.subtitleSize, 2.5f);
    EXPECT_FLOAT_EQ(s.text.subtitleBackground, 0.0f);
    EXPECT_EQ(s.accessibility.colorBlind, "off");
    EXPECT_FLOAT_EQ(s.accessibility.colorBlindStrength, 1.0f);
    // An old file without them keeps the defaults.
    const EngineSettings old = kke::settingsFromJson(R"({"graphics": {"vsync": false}})");
    EXPECT_EQ(old.text.language, "auto");
    EXPECT_TRUE(old.text.subtitles);
}

TEST(EngineSettings, CrashReportsAreNeverSentUnlessThePlayerSaysSo) {
    EngineSettings s;
    EXPECT_FALSE(s.privacy.sendCrashReports); // off by default
    EXPECT_FALSE(kke::settingsFromJson(R"({"graphics": {"vsync": false}})").privacy.sendCrashReports); // and an old file keeps it off
    s.privacy.sendCrashReports = true;
    const EngineSettings back = kke::settingsFromJson(kke::settingsToJson(s));
    EXPECT_TRUE(back.privacy.sendCrashReports);
    EXPECT_EQ(back, s);
    EngineSettings other = s;
    other.privacy.sendCrashReports = false;
    EXPECT_NE(other, s);
}
