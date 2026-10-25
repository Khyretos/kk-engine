#include "kke/Application.h"
#include "kke/DayNight.h"
#include "kke/ScriptClock.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using kke::DayNight;

TEST(DayNight, TheSunRisesEastCrossesSouthAndSetsWest) {
    DayNight d;
    const DayNight::Look dawn = d.lookAt(6.0f), noon = d.lookAt(12.0f), dusk = d.lookAt(18.0f), midnight = d.lookAt(0.0f);
    EXPECT_NEAR(dawn.sunElevation, 0.0f, 1e-3f);
    EXPECT_NEAR(dawn.sunAzimuth, 90.0f, 1e-3f); // east (+x)
    EXPECT_GT(dawn.towardsSun.x, 0.99f);
    EXPECT_NEAR(noon.sunElevation, d.noonElevation, 1e-3f);
    EXPECT_NEAR(noon.sunAzimuth, 180.0f, 1e-3f); // south (+z)
    EXPECT_GT(noon.towardsSun.z, 0.0f);
    EXPECT_NEAR(dusk.sunAzimuth, 270.0f, 1e-3f); // west
    EXPECT_LT(midnight.sunElevation, -30.0f);
    // Hours wrap.
    EXPECT_FLOAT_EQ(d.sunElevationAt(36.0f), d.sunElevationAt(12.0f));
    EXPECT_FLOAT_EQ(d.sunElevationAt(-1.0f), d.sunElevationAt(23.0f));
    // A short winter day: later up, earlier down, lower.
    DayNight winter;
    winter.sunrise = 8.5f;
    winter.sunset = 16.5f;
    winter.noonElevation = 15.0f;
    EXPECT_LT(winter.sunElevationAt(8.0f), 0.0f);
    EXPECT_NEAR(winter.sunElevationAt(12.5f), 15.0f, 1e-3f);
}

TEST(DayNight, TheLightFollowsTheSunThroughDuskToMoonlight) {
    DayNight d;
    const DayNight::Look noon = d.lookAt(12.0f), golden = d.lookAt(17.3f), night = d.lookAt(1.0f);
    // Day: a bright white-ish sun from above, a blue sky.
    EXPECT_FALSE(noon.moonlit);
    EXPECT_GT(noon.keyIntensity, 2.5f);
    EXPECT_LT(noon.keyDirection.y, -0.8f);
    EXPECT_GT(noon.sky.zenith.b, noon.sky.zenith.r * 3.0f);
    // Golden hour: weaker, warm (more red than blue), a warm horizon.
    EXPECT_GT(golden.sunElevation, 0.0f);
    EXPECT_LT(golden.keyIntensity, noon.keyIntensity);
    EXPECT_GT(golden.keyColor.r, golden.keyColor.b * 1.4f);
    EXPECT_GT(golden.sky.horizon.r, golden.sky.horizon.b);
    // Night: blue moonlight from above, a dark sky, more exposure.
    EXPECT_TRUE(night.moonlit);
    EXPECT_GT(night.keyIntensity, 0.1f);
    EXPECT_LT(night.keyIntensity, 0.5f);
    EXPECT_GT(night.keyColor.b, night.keyColor.r);
    EXPECT_LT(night.keyDirection.y, -0.4f);
    EXPECT_LT(night.sky.zenith.b, 0.03f);
    EXPECT_GT(night.exposure, 1.5f);
    // No jumps: through the whole day the key light changes smoothly.
    float prev = d.lookAt(0.0f).keyIntensity;
    for (int m = 1; m <= 24 * 60; ++m) {
        const float k = d.lookAt(float(m) / 60.0f).keyIntensity;
        EXPECT_LT(std::abs(k - prev), 0.05f) << "minute " << m;
        prev = k;
    }
}

TEST(DayNight, AdvanceReportsHoursDawnAndDuskInOrderAcrossMidnight) {
    DayNight d;
    d.dayLengthSeconds = 240.0f; // 10 s an hour
    EXPECT_TRUE(d.advance(10.0f).empty()); // not running
    d.running = true;
    d.hour = 5.5f;
    std::vector<DayNight::Crossed> c = d.advance(10.0f); // 5:30 -> 6:30
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c[0].event, DayNight::Event::Hour);
    EXPECT_EQ(c[0].hour, 6);
    EXPECT_EQ(c[1].event, DayNight::Event::Dawn);
    EXPECT_NEAR(d.hour, 6.5f, 1e-4f);
    d.hour = 23.5f;
    c = d.advance(10.0f); // past midnight
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].hour, 0);
    EXPECT_NEAR(d.hour, 0.5f, 1e-4f);
    d.hour = 17.0f;
    c = d.advance(15.0f); // 17:00 -> 18:30
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c[0].hour, 18);
    EXPECT_EQ(c[1].event, DayNight::Event::Dusk);
    // A huge step is a day at most.
    d.hour = 12.0f;
    EXPECT_EQ(d.advance(1e6f).size(), 26u);
    EXPECT_NEAR(d.hour, 12.0f, 1e-3f);
}

TEST(DayNight, ApplyLightsTheSceneAndSkiesChangeOnlyWhenWorthIt) {
    DayNight d;
    kke::Lighting lighting;
    lighting.lights[1].enabled = true; // a mood's fill
    DayNight::apply(d.lookAt(9.0f), lighting, false);
    EXPECT_TRUE(lighting.lights[0].enabled);
    EXPECT_FALSE(lighting.lights[1].enabled);
    EXPECT_EQ(lighting.sky.kind, kke::Sky::Kind::None); // the sky waits
    DayNight::apply(d.lookAt(9.0f), lighting, true);
    EXPECT_EQ(lighting.sky.kind, kke::Sky::Kind::Gradient);
    EXPECT_TRUE(lighting.sky.lightsScene);
    // A game minute at midday changes nothing worth a new sky; an hour at dusk does.
    EXPECT_FALSE(DayNight::skyChanged(d.lookAt(12.0f).sky, d.lookAt(12.0f + 1.0f / 60.0f).sky));
    EXPECT_TRUE(DayNight::skyChanged(d.lookAt(17.5f).sky, d.lookAt(18.5f).sky));
}

namespace {
std::string lastError(const kke::ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }
} // namespace

TEST(ScriptClock, LuaRunsTheClockAndHearsDawnDuskAndHours) {
    kke::ScriptVM vm;
    kke::ScriptClock sc(nullptr);
    sc.bind(vm);
    ASSERT_TRUE(vm.runString(R"(
        events = {}
        hook.Add("Hour", "t", function(e) events[#events + 1] = "h" .. e.hour end)
        hook.Add("Dawn", "t", function(e) events[#events + 1] = "dawn" end)
        hook.Add("Dusk", "t", function(e) events[#events + 1] = "dusk" end)
        assert(not clock.running())
        clock.start{ hour = 5.75, dayLength = 24 }      -- a day in 24 s: an hour a second
        assert(clock.running() and clock.time() == "05:45", clock.time())
        assert(not clock.isDay())
    )", "time.lua")) << lastError(vm);
    EXPECT_TRUE(sc.active());
    for (int i = 0; i < 10; ++i) sc.update(0.05f, vm); // half an hour
    ASSERT_TRUE(vm.runString(R"(
        assert(#events == 2 and events[1] == "h6" and events[2] == "dawn", table.concat(events, ","))
        assert(clock.isDay())
        local dir, elevation = clock.sun()
        assert(dir.x > 0.9 and elevation > 0 and elevation < 10)
        clock.pause()
        assert(not clock.running())
        clock.set(18.25)
        assert(clock.time() == "18:15")
        assert(clock.dayLength(600) == 600)
    )", "time.lua")) << lastError(vm);
    sc.update(1.0f, vm); // paused: no time passes
    EXPECT_NEAR(sc.clock().hour, 18.25f, 1e-4f);
    // Unloading the script that started it lets go of the lighting.
    sc.release("other.lua");
    EXPECT_TRUE(sc.active());
    sc.release("time.lua");
    EXPECT_FALSE(sc.active());
    EXPECT_FALSE(vm.runString("clock.set(0/0)", "time.lua"));
}
