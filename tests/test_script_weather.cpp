// weather.* in Lua (kke/ScriptWeather.h, ENG-F81).
#include "kke/Application.h"
#include "kke/ScriptVM.h"
#include "kke/ScriptWeather.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <map>

using namespace kke;

namespace {

double globalNumber(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const double v = lua_tonumber(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string globalString(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    std::string v = lua_isstring(vm.state(), -1) ? lua_tostring(vm.state(), -1) : "";
    lua_pop(vm.state(), 2);
    return v;
}
bool globalBool(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const bool v = lua_toboolean(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string lastError(const ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }

struct Fixture {
    ScriptVM vm;
    ScriptWeather weather;
    Lighting lighting;
    glm::vec3 camera{ 10.0f, 2.0f, 5.0f };
    float hour = -1.0f;
    struct Stream {
        std::string effect;
        int count = 0;
        glm::vec3 position{ 0.0f };
        float floor = 0.0f;
        int calls = 0;
    };
    std::map<std::string, Stream> streams;
    glm::vec3 wind{ 0.0f };

    Fixture() {
        lighting.lights[0].enabled = true;
        lighting.lights[0].intensity = 2.5f;
        lighting.exposure = 1.0f;
        lighting.ambientColor = glm::vec3(0.2f, 0.3f, 0.4f);
        weather.attach(&lighting);
        weather.precipitation = [this](const std::string& family, const std::string& effect, int count, const glm::vec3& position, float floor) {
            Stream& s = streams[family];
            s.effect = effect;
            s.count = count;
            s.position = position;
            s.floor = floor;
            ++s.calls;
        };
        weather.wind = [this](const glm::vec3& w) { wind = w; };
        // a dark storm without lightning, for tests that look at the light (a flash would make them depend on chance)
        WeatherType dark = *weather.weather().type("storm");
        dark.name = "dark_storm";
        dark.lightning = 0.0f;
        weather.weather().defineType(dark);
        weather.bind(vm);
        vm.onUnload = [this](const std::string& source) { weather.release(source); };
    }
    bool run(const std::string& code, const std::string& source = "console") { return vm.runString(code, source); }
    void tick(int n = 1, float dt = 0.1f) {
        for (int i = 0; i < n; ++i) weather.update(dt, vm, camera, hour);
    }
};

} // namespace

TEST(ScriptWeather, StaysQuietUntilAScriptUsesIt) {
    Fixture f;
    const float sun = f.lighting.lights[0].intensity;
    f.tick(50);
    EXPECT_FALSE(f.weather.active());
    EXPECT_EQ(f.lighting.lights[0].intensity, sun) << "a game without weather is not touched";
    EXPECT_TRUE(f.streams.empty());
    EXPECT_EQ(f.wind, glm::vec3(0.0f));
}

TEST(ScriptWeather, RainFallsAroundTheCameraAndDimsTheLight) {
    Fixture f;
    ASSERT_TRUE(f.run("ok, why = weather.set('rain', 0) cur = weather.current()")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    EXPECT_EQ(globalString(f.vm, "cur"), "rain");
    f.tick(5);
    const auto& rain = f.streams["rain"];
    EXPECT_EQ(rain.effect, "rain");
    EXPECT_EQ(rain.count, 2) << "rain 0.6: two emitters";
    EXPECT_EQ(rain.position, f.camera + glm::vec3(0, 10, 0)) << "above the camera";
    EXPECT_NEAR(rain.floor, f.camera.y - 1.8f, 1e-5f) << "the ground is below it";
    EXPECT_EQ(f.streams["snow"].effect, "") << "no snow";
    EXPECT_EQ(f.streams["snow"].count, 0);
    EXPECT_LT(f.lighting.lights[0].intensity, 2.5f * 0.35f) << "the sun is dimmed (rain lets 30% through)";
    EXPECT_LT(f.lighting.ambientColor.g, 0.3f);
    EXPECT_LT(f.lighting.grade.saturation, 1.0f) << "and the colours grey";
    EXPECT_GT(f.lighting.fog.density, 0.0f);
    EXPECT_TRUE(f.lighting.fog.enabled) << "rain brings a little fog";
    EXPECT_GT(glm::length(f.wind), 0.5f) << "the particles get the wind";
    // moves with the camera
    f.camera = glm::vec3(50, 3, -20);
    f.tick();
    EXPECT_EQ(f.streams["rain"].position, f.camera + glm::vec3(0, 10, 0));
}

TEST(ScriptWeather, TheAmountOfRainPicksTheEffectAndTheNumberOfEmitters) {
    Fixture f;
    auto effectFor = [&](const char* weather) {
        f.run(std::string("weather.set('") + weather + "', 0)");
        f.tick(2);
        return std::make_pair(f.streams["rain"].effect, f.streams["rain"].count);
    };
    EXPECT_EQ(effectFor("clear"), (std::pair<std::string, int>{ "", 0 }));
    EXPECT_EQ(effectFor("drizzle"), (std::pair<std::string, int>{ "rain", 1 }));
    EXPECT_EQ(effectFor("rain"), (std::pair<std::string, int>{ "rain", 2 }));
    EXPECT_EQ(effectFor("storm"), (std::pair<std::string, int>{ "downpour", 1 })) << "a cloudburst has its own effect";
    const auto snow = [&](const char* w) {
        f.run(std::string("weather.set('") + w + "', 0)");
        f.tick(2);
        return std::make_pair(f.streams["snow"].effect, f.streams["snow"].count);
    };
    EXPECT_EQ(snow("snow"), (std::pair<std::string, int>{ "snow", 2 }));
    EXPECT_EQ(snow("blizzard"), (std::pair<std::string, int>{ "snow", 3 }));
    EXPECT_EQ(f.streams["rain"].count, 0) << "and it stopped raining";
}

TEST(ScriptWeather, SnowAtTheCameraFallsLowerThanRain) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('snow', 0)"));
    f.tick(2);
    EXPECT_EQ(f.streams["snow"].position, f.camera + glm::vec3(0, 7.5f, 0));
}

TEST(ScriptWeather, TheLightingFollowsWhoeverElseSetsIt) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('overcast', 0)")) << lastError(f.vm);
    f.tick(3);
    const float dimmed = f.lighting.lights[0].intensity;
    EXPECT_NEAR(dimmed, 2.5f * 0.45f, 0.02f);
    f.tick(20);
    EXPECT_NEAR(f.lighting.lights[0].intensity, dimmed, 1e-4f) << "applied over and over, it does not compound";
    // a mood or the clock sets a brighter sun: that is the new base
    f.lighting.lights[0].intensity = 5.0f;
    f.lighting.exposure = 1.4f;
    f.tick();
    EXPECT_NEAR(f.lighting.lights[0].intensity, 5.0f * 0.45f, 0.02f);
    EXPECT_NEAR(f.lighting.exposure, 1.4f * (0.5f + 0.5f * 0.8f), 0.01f);
    EXPECT_NEAR(f.lighting.ambientColor.g, 0.3f * 0.8f, 1e-4f) << "what the other setter did not touch is not dimmed twice";
    f.tick(10);
    EXPECT_NEAR(f.lighting.lights[0].intensity, 5.0f * 0.45f, 0.02f) << "and again it stays";
    // fog set by a mood adds up with the weather's
    f.lighting.fog.enabled = true;
    f.lighting.fog.density = 0.01f;
    f.tick();
    EXPECT_NEAR(f.lighting.fog.density, 0.01f + 0.02f * 0.05f, 1e-4f);
}

TEST(ScriptWeather, StoppingPutsTheLightingBack) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('dark_storm', 0)"));
    f.tick(5);
    EXPECT_LT(f.lighting.lights[0].intensity, 1.0f);
    EXPECT_TRUE(f.lighting.fog.enabled);
    ASSERT_TRUE(f.run("weather.stop()")) << lastError(f.vm);
    EXPECT_FALSE(f.weather.active());
    EXPECT_NEAR(f.lighting.lights[0].intensity, 2.5f, 1e-5f);
    EXPECT_NEAR(f.lighting.exposure, 1.0f, 1e-5f);
    EXPECT_FALSE(f.lighting.fog.enabled);
    EXPECT_NEAR(f.lighting.ambientColor.g, 0.3f, 1e-5f);
    EXPECT_NEAR(f.lighting.grade.saturation, 1.0f, 1e-5f);
    EXPECT_EQ(f.streams["rain"].count, 0) << "the rain stopped";
    EXPECT_EQ(f.wind, glm::vec3(0.0f));
    f.tick(10);
    EXPECT_NEAR(f.lighting.lights[0].intensity, 2.5f, 1e-5f) << "and it stays off";
    // it can start again
    ASSERT_TRUE(f.run("weather.set('rain', 0)"));
    f.tick(3);
    EXPECT_LT(f.lighting.lights[0].intensity, 1.0f);
    // if someone else changed the lighting meanwhile, stopping does not undo their change
    f.lighting.lights[0].intensity = 4.0f;
    ASSERT_TRUE(f.run("weather.stop()"));
    EXPECT_NEAR(f.lighting.lights[0].intensity, 4.0f, 1e-5f);
}

TEST(ScriptWeather, TheSwitchesTurnTheEffectsOffOneByOne) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('rain', 0) weather.lighting(false)"));
    f.tick(3);
    EXPECT_NEAR(f.lighting.lights[0].intensity, 2.5f, 1e-5f) << "no lighting";
    EXPECT_EQ(f.streams["rain"].count, 2) << "but the rain falls";
    ASSERT_TRUE(f.run("weather.particles(false)"));
    EXPECT_EQ(f.streams["rain"].count, 0) << "particles off: they stop";
    const int calls = f.streams["rain"].calls;
    f.tick(5);
    EXPECT_EQ(f.streams["rain"].calls, calls) << "and are left alone";
    ASSERT_TRUE(f.run("weather.lighting(true)"));
    f.tick(2);
    EXPECT_LT(f.lighting.lights[0].intensity, 1.0f);
}

TEST(ScriptWeather, HooksAnnounceChangesRainAndLightning) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        log = {}
        for _, name in ipairs{ "WeatherChanging", "WeatherChanged", "RainStarted", "RainStopped", "Lightning", "Thunder" } do
            hook.Add(name, "t", function(e)
                local text = name
                if e.to then text = text .. ":" .. e.from .. ">" .. e.to end
                if e.weather then text = text .. ":" .. e.weather end
                if e.distance then text = text .. ":" .. (e.distance > 0 and "far" or "?") end
                log[#log + 1] = text
            end)
        end
        weather.set("storm", 10)
    )")) << lastError(f.vm);
    f.tick(200, 0.1f); // 20 s
    ASSERT_TRUE(f.run("all = table.concat(log, ' ')")) << lastError(f.vm);
    const std::string all = globalString(f.vm, "all");
    EXPECT_NE(all.find("WeatherChanging:clear>storm"), std::string::npos) << all;
    EXPECT_NE(all.find("RainStarted:storm"), std::string::npos) << all;
    EXPECT_NE(all.find("WeatherChanged:storm"), std::string::npos) << all;
    EXPECT_LT(all.find("RainStarted"), all.find("WeatherChanged"));
    f.tick(3000, 0.1f); // five minutes of storm
    ASSERT_TRUE(f.run("all2 = table.concat(log, ' ')"));
    const std::string more = globalString(f.vm, "all2");
    EXPECT_NE(more.find("Lightning:storm:far"), std::string::npos) << "a storm has lightning";
    EXPECT_NE(more.find("Thunder:storm:far"), std::string::npos) << "and thunder, later";
    ASSERT_TRUE(f.run("weather.set('clear', 5) log = {}"));
    f.tick(100, 0.1f);
    ASSERT_TRUE(f.run("all3 = table.concat(log, ' ')"));
    EXPECT_NE(globalString(f.vm, "all3").find("RainStopped"), std::string::npos);
}

TEST(ScriptWeather, TheLightningFlashLightsTheScene) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('storm', 0) weather.lighting(true)"));
    float brightest = 0.0f, dimmest = 1e9f;
    for (int i = 0; i < 60000; ++i) { // ten minutes in 10 ms steps
        f.tick(1, 0.01f);
        brightest = std::max(brightest, f.lighting.lights[0].intensity);
        dimmest = std::min(dimmest, f.lighting.lights[0].intensity);
    }
    EXPECT_LT(dimmest, 2.5f * 0.2f) << "a storm is dark";
    EXPECT_GT(brightest, 2.0f) << "and a flash is very bright";
}

TEST(ScriptWeather, TheStateTableTellsWhatTheWorldIsDoing) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('snow', 0) weather.time(15) weather.ground(0.3, 0.2)")) << lastError(f.vm);
    f.tick(10);
    ASSERT_TRUE(f.run(R"(
        s = weather.state()
        snowing, raining = s.snowFall, s.rainFall
        temp, wet, cover, cur, changing = s.temperature, s.wetness, s.snowCover, s.weather, s.changing
        wx, wy, wz = s.windVector.x, s.windVector.y, s.windVector.z
        vis, speed = s.visibility, s.windSpeed
    )")) << lastError(f.vm);
    EXPECT_NEAR(globalNumber(f.vm, "snowing"), 0.5, 1e-4);
    EXPECT_EQ(globalNumber(f.vm, "raining"), 0.0);
    EXPECT_NEAR(globalNumber(f.vm, "temp"), -2.0, 0.01) << "15:00: the type's temperature";
    EXPECT_GT(globalNumber(f.vm, "wet"), 0.0);
    EXPECT_GT(globalNumber(f.vm, "cover"), 0.2) << "the snow builds on what was there";
    EXPECT_EQ(globalString(f.vm, "cur"), "snow");
    EXPECT_FALSE(globalBool(f.vm, "changing"));
    EXPECT_EQ(globalNumber(f.vm, "wy"), 0.0);
    EXPECT_NEAR(std::sqrt(globalNumber(f.vm, "wx") * globalNumber(f.vm, "wx") + globalNumber(f.vm, "wz") * globalNumber(f.vm, "wz")), globalNumber(f.vm, "speed"), 1e-3);
    EXPECT_LT(globalNumber(f.vm, "vis"), 1500.0);
}

TEST(ScriptWeather, TheTimeOfDayComesFromTheClockUnlessSetByHand) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('clear', 0)"));
    f.hour = 15.0f;
    f.tick(2);
    const float warm = f.weather.weather().state().temperature;
    f.hour = 3.0f;
    f.tick(2);
    EXPECT_LT(f.weather.weather().state().temperature, warm - 5.0f) << "the clock's hour is used";
    ASSERT_TRUE(f.run("weather.time(15)"));
    f.tick(2);
    EXPECT_NEAR(f.weather.weather().state().temperature, warm, 0.01f) << "set by hand: the clock is ignored";
    ASSERT_TRUE(f.run("weather.time(nil)"));
    f.tick(2);
    EXPECT_LT(f.weather.weather().state().temperature, warm - 5.0f) << "back to the clock";
}

TEST(ScriptWeather, RandomWeatherChangesBySelf) {
    Fixture f;
    ASSERT_TRUE(f.run("changes = 0 hook.Add('WeatherChanging', 't', function() changes = changes + 1 end) weather.random(20, 30, 5)")) << lastError(f.vm);
    EXPECT_TRUE(f.weather.active());
    f.tick(6000, 0.1f); // ten minutes
    ASSERT_TRUE(f.run("n = changes"));
    EXPECT_GT(globalNumber(f.vm, "n"), 8.0);
    ASSERT_TRUE(f.run("weather.random(false) changes = 0"));
    f.tick(3000, 0.1f);
    ASSERT_TRUE(f.run("n2 = changes"));
    EXPECT_EQ(globalNumber(f.vm, "n2"), 0.0);
}

TEST(ScriptWeather, TypesCanBeDefinedAndConfigured) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        ok1 = weather.define{ name = "sandstorm", fog = 0.8, wind = 20, gust = 0.9, temperature = 38, sunlight = 0.4, saturation = 0.6 }
        ok2 = weather.config{ transitions = { clear = { sandstorm = 1 }, sandstorm = { clear = 1 } } }
        names = table.concat(weather.types(), ",")
        set = weather.set("sandstorm", 0)
    )")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok1"));
    EXPECT_TRUE(globalBool(f.vm, "ok2"));
    EXPECT_NE(globalString(f.vm, "names").find("sandstorm"), std::string::npos);
    EXPECT_TRUE(globalBool(f.vm, "set"));
    f.tick(3);
    EXPECT_NEAR(f.weather.weather().state().wind, 20.0f, 1e-4f);
    EXPECT_LT(f.lighting.grade.saturation, 0.7f);
}

TEST(ScriptWeather, MistakesComeBackWithAReason) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        a, whyA = weather.set("tornado")
        b, whyB = weather.define{ name = "x", rain = 3 }
        c, whyC = weather.define{ rain = 0.5 }
        d, whyD = weather.config{ types = 5 }
    )")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "a"));
    EXPECT_NE(globalString(f.vm, "whyA").find("no weather called 'tornado'"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "b"));
    EXPECT_NE(globalString(f.vm, "whyB").find("rain is 0 to 1"), std::string::npos) << globalString(f.vm, "whyB");
    EXPECT_FALSE(globalBool(f.vm, "c"));
    EXPECT_NE(globalString(f.vm, "whyC").find("needs a name"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "d"));
    EXPECT_FALSE(f.run("weather.wind(0 / 0)"));
    EXPECT_FALSE(f.run("weather.time(0 / 0)"));
}

TEST(ScriptWeather, TheWeatherGoesWithTheScriptThatStartedIt) {
    Fixture f;
    ASSERT_TRUE(f.run("weather.set('dark_storm', 0)", "level")) << lastError(f.vm);
    f.tick(5);
    EXPECT_TRUE(f.weather.active());
    EXPECT_LT(f.lighting.lights[0].intensity, 1.0f);
    f.vm.unload("another script");
    EXPECT_TRUE(f.weather.active()) << "another script leaving does not stop it";
    f.vm.unload("level");
    EXPECT_FALSE(f.weather.active());
    EXPECT_NEAR(f.lighting.lights[0].intensity, 2.5f, 1e-5f) << "the lighting is back";
    EXPECT_EQ(f.streams["rain"].count, 0);
}
