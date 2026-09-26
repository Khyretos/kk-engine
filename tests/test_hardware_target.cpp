#include "kke/HardwareTarget.h"
#include "kke/Platform.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

using kke::HardwareTarget;
using kke::platform::DeviceHints;

namespace {

DeviceHints desktopPc() {
    DeviceHints h;
    h.os = "Linux";
    h.vendor = "Micro-Star International Co., Ltd.";
    h.productName = "MS-7C56";
    h.logicalCores = 16;
    h.systemRamMb = 32768;
    return h;
}

std::string chosen(const DeviceHints& h, const std::string& requested = {}, const std::string& buildDefault = {}) {
    return kke::chooseHardwareTarget(h, requested, buildDefault).target->name;
}

} // namespace

TEST(HardwareTarget, BuiltInTargetsExistAndTheirSettingsParse) {
    for (const char* name : { "desktop", "desktop-low", "steam-deck", "handheld-pc", "android", "ios" }) {
        const HardwareTarget* t = kke::findHardwareTarget(name);
        ASSERT_NE(t, nullptr) << name;
        // A typo in a built-in override would silently fall back to
        // desktop defaults, so parse each one strictly here.
        EXPECT_NO_THROW((void)kke::settingsFromJson(t->settings)) << name;
        EXPECT_GT(t->targetFps, 0.0f) << name;
        EXPECT_FALSE(t->displayName.empty()) << name;
    }
    EXPECT_EQ(kke::findHardwareTarget("no-such-target"), nullptr);
}

TEST(HardwareTarget, DesktopIsTheEngineDefaults) {
    EXPECT_EQ(kke::settingsForTarget(*kke::findHardwareTarget("desktop")), kke::EngineSettings{});
}

TEST(HardwareTarget, PresetsChangeWhatTheyClaim) {
    const kke::EngineSettings deck = kke::settingsForTarget(*kke::findHardwareTarget("steam-deck"));
    EXPECT_TRUE(deck.graphics.fullscreen);
    EXPECT_EQ(deck.graphics.msaa, 2);
    EXPECT_FLOAT_EQ(deck.graphics.frameRateLimit, 60.0f);
    EXPECT_FLOAT_EQ(deck.graphics.uiScale, 1.25f);

    const kke::EngineSettings phone = kke::settingsForTarget(*kke::findHardwareTarget("android"));
    EXPECT_FALSE(phone.graphics.shadows);
    EXPECT_FLOAT_EQ(phone.graphics.frameRateLimit, 30.0f);
    EXPECT_LT(phone.performance.renderScale, 1.0f);

    const kke::EngineSettings low = kke::settingsForTarget(*kke::findHardwareTarget("desktop-low"));
    EXPECT_EQ(low.graphics.msaa, 1);
    EXPECT_FALSE(low.graphics.shadows);
    // Keys a preset leaves out keep the engine defaults.
    EXPECT_EQ(low.controls.keyBindings, kke::EngineSettings{}.controls.keyBindings);
}

TEST(HardwareTarget, RecognisesDevices) {
    EXPECT_EQ(chosen(desktopPc()), "desktop");

    DeviceHints deck = desktopPc();
    deck.vendor = "Valve";
    deck.productName = "Jupiter";
    deck.logicalCores = 8;
    deck.systemRamMb = 15000;
    EXPECT_EQ(chosen(deck), "steam-deck");
    deck.productName = "Galileo"; // OLED
    EXPECT_EQ(chosen(deck), "steam-deck");

    DeviceHints gameMode = desktopPc();
    gameMode.steamDeckMode = true;
    EXPECT_EQ(chosen(gameMode), "steam-deck");

    DeviceHints ally = desktopPc();
    ally.vendor = "ASUSTeK COMPUTER INC.";
    ally.productName = "ROG Ally RC71L_RC71L";
    EXPECT_EQ(chosen(ally), "handheld-pc");

    DeviceHints phone;
    phone.os = "Android";
    phone.logicalCores = 8;
    phone.systemRamMb = 8000;
    EXPECT_EQ(chosen(phone), "android");
    phone.os = "iOS";
    EXPECT_EQ(chosen(phone), "ios");

    DeviceHints oldLaptop = desktopPc();
    oldLaptop.logicalCores = 2;
    EXPECT_EQ(chosen(oldLaptop), "desktop-low");
    oldLaptop.logicalCores = 4;
    oldLaptop.systemRamMb = 4096;
    EXPECT_EQ(chosen(oldLaptop), "desktop-low");

    // Unknown numbers (a backend that can't tell) are not "low-end".
    DeviceHints unknown;
    EXPECT_EQ(chosen(unknown), "desktop");
}

TEST(HardwareTarget, OverridesComeFirst) {
    DeviceHints deck = desktopPc();
    deck.steamDeckMode = true;
    EXPECT_EQ(chosen(deck, "desktop"), "desktop");            // KKE_TARGET wins
    EXPECT_EQ(chosen(desktopPc(), {}, "steam-deck"), "steam-deck"); // then the build preset
    EXPECT_EQ(chosen(desktopPc(), "android", "steam-deck"), "android");
    // Unknown names are ignored rather than trusted.
    EXPECT_EQ(chosen(deck, "nintendo-64"), "steam-deck");
    EXPECT_EQ(chosen(desktopPc(), {}, "typo"), "desktop");
    EXPECT_FALSE(kke::chooseHardwareTarget(deck, "desktop").reason.empty());
}

TEST(HardwareTarget, GamesAddAndTuneTargetsFromDataFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "kke_test_hardware_target";
    std::filesystem::create_directories(dir);
    const auto file = dir / "targets.yml";
    {
        std::ofstream out(file);
        out << "targets:\n"
               "  - name: test-arcade-cabinet\n"
               "    displayName: Arcade cabinet\n"
               "    targetFps: 120\n"
               "    settings:\n"
               "      graphics: { fullscreen: true, msaa: 8 }\n"
               "  - name: steam-deck\n"
               "    targetFps: 40\n";
    }
    std::string error;
    EXPECT_EQ(kke::loadHardwareTargetsFile((dir / "targets.json").string(), &error), 2) << error; // .yml spelling found
    EXPECT_TRUE(error.empty()) << error;

    const HardwareTarget* cab = kke::findHardwareTarget("test-arcade-cabinet");
    ASSERT_NE(cab, nullptr);
    EXPECT_EQ(cab->displayName, "Arcade cabinet");
    EXPECT_FLOAT_EQ(cab->targetFps, 120.0f);
    EXPECT_EQ(kke::settingsForTarget(*cab).graphics.msaa, 8);

    // Tuning a built-in keeps what the file doesn't mention.
    const HardwareTarget* deck = kke::findHardwareTarget("steam-deck");
    ASSERT_NE(deck, nullptr);
    EXPECT_FLOAT_EQ(deck->targetFps, 40.0f);
    EXPECT_EQ(kke::settingsForTarget(*deck).graphics.msaa, 2);
    EXPECT_EQ(deck->displayName, "Steam Deck");

    // Put the built-in back for any later test in this process.
    HardwareTarget restored = *deck;
    restored.targetFps = 60.0f;
    kke::registerHardwareTarget(restored);

    EXPECT_EQ(kke::loadHardwareTargetsFile((dir / "missing.json").string(), &error), 0);
    std::filesystem::remove_all(dir);
}

TEST(HardwareTarget, SavedSettingsBeatTheTargetDefaults) {
    const kke::EngineSettings deck = kke::settingsForTarget(*kke::findHardwareTarget("steam-deck"));
    const kke::EngineSettings s = kke::settingsFromJson(R"({ "graphics": { "msaa": 8 } })", deck);
    EXPECT_EQ(s.graphics.msaa, 8);                // the player's choice
    EXPECT_FLOAT_EQ(s.graphics.uiScale, 1.25f);   // the device's default
    std::string error;
    EXPECT_EQ(kke::loadSettingsFile("no/such/settings.json", &error, deck), deck);
}

TEST(Platform, DesktopBackendAnswers) {
    EXPECT_STREQ(kke::platform::backendName(), "desktop");
    EXPECT_GE(kke::platform::usableCpuCount(), 1u);
    EXPECT_FALSE(kke::platform::hostName().empty());

    std::vector<uint8_t> a(32), b(32);
    ASSERT_TRUE(kke::platform::secureRandom(a.data(), a.size()));
    ASSERT_TRUE(kke::platform::secureRandom(b.data(), b.size()));
    EXPECT_NE(a, b);

    void* p = kke::platform::alignedAlloc(100, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0u);
    kke::platform::alignedFree(p);

    const DeviceHints h = kke::platform::deviceHints();
    EXPECT_FALSE(h.os.empty());
}
