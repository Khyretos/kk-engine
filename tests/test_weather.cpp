// Weather (kke/Weather.h, ENG-F81, docs/MOODS.md "Weather").
#include "kke/Weather.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

using namespace kke;
using nlohmann::json;
using K = WeatherEvent::Kind;

namespace {

// Runs the weather for `seconds`, collecting every event with the time it came.
struct Timed {
    float at;
    WeatherEvent e;
};
std::vector<Timed> run(Weather& w, float seconds, float dt = 0.1f) {
    std::vector<Timed> out;
    for (float t = 0.0f; t < seconds - 1e-4f; t += dt) {
        w.update(dt);
        for (const WeatherEvent& e : w.takeEvents()) out.push_back({ w.time(), e });
    }
    return out;
}

size_t count(const std::vector<Timed>& events, K kind) {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [&](const Timed& t) { return t.e.kind == kind; }));
}

} // namespace

TEST(Weather, TheDefaultTypesAreThere) {
    Weather w;
    for (const char* n : { "clear", "partly_cloudy", "overcast", "drizzle", "rain", "storm", "fog", "snow", "blizzard", "heat" }) EXPECT_TRUE(w.hasType(n)) << n;
    EXPECT_FALSE(w.hasType("tornado"));
    EXPECT_EQ(w.current(), "clear");
    EXPECT_LT(w.state().clouds, 0.1f);
    ASSERT_NE(w.type("storm"), nullptr);
    EXPECT_GT(w.type("storm")->lightning, 0.0f);
    EXPECT_EQ(w.type("nothing"), nullptr);
    EXPECT_GE(w.typeNames().size(), 10u);
}

TEST(Weather, TypesAreChecked) {
    Weather w;
    std::string error;
    WeatherType t;
    EXPECT_FALSE(w.defineType(t, &error));
    EXPECT_NE(error.find("needs a name"), std::string::npos);
    t.name = "dust";
    t.rain = 1.5f;
    EXPECT_FALSE(w.defineType(t, &error));
    EXPECT_NE(error.find("weather 'dust': rain is 0 to 1"), std::string::npos) << error;
    t.rain = 0.0f;
    t.wind = 500.0f;
    EXPECT_FALSE(w.defineType(t, &error));
    EXPECT_NE(error.find("wind is 0 to 100"), std::string::npos);
    t.wind = 30.0f;
    t.temperature = std::nanf("");
    EXPECT_FALSE(w.defineType(t, &error));
    EXPECT_NE(error.find("finite"), std::string::npos);
    t.temperature = 30.0f;
    t.fog = 0.5f;
    t.sunlight = 0.6f;
    EXPECT_TRUE(w.defineType(t, &error)) << error;
    EXPECT_TRUE(w.hasType("dust"));
    EXPECT_TRUE(w.setWeather("dust", 0.0f));
    EXPECT_NEAR(w.state().wind, 30.0f, 1e-5f);
    // the number of types is limited
    Weather many;
    for (size_t i = 0; i < Weather::kMaxTypes + 5; ++i) {
        WeatherType x;
        x.name = "t" + std::to_string(i);
        many.defineType(x);
    }
    EXPECT_LE(many.typeNames().size(), Weather::kMaxTypes);
}

TEST(Weather, ChangingIsInstantOrABlend) {
    Weather w;
    EXPECT_FALSE(w.setWeather("nowhere", 5.0f));
    EXPECT_EQ(w.current(), "clear");
    ASSERT_TRUE(w.setWeather("overcast", 0.0f));
    EXPECT_NEAR(w.state().clouds, 0.9f, 1e-6f);
    auto events = w.takeEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, K::Changing);
    EXPECT_EQ(events[0].from, "clear");
    EXPECT_EQ(events[0].name, "overcast");
    EXPECT_EQ(events[1].kind, K::Changed);
    EXPECT_FALSE(w.changing());
    // a blend: halfway through it is halfway between (smoothstep's middle)
    ASSERT_TRUE(w.setWeather("storm", 10.0f));
    EXPECT_TRUE(w.changing());
    EXPECT_EQ(w.current(), "storm") << "the weather it is going to";
    run(w, 5.0f);
    EXPECT_NEAR(w.state().rain, 0.5f * 1.0f, 0.02f);
    EXPECT_NEAR(w.state().wind, 0.5f * (5.0f + 14.0f), 0.2f);
    EXPECT_TRUE(w.changing());
    const auto rest = run(w, 5.5f);
    EXPECT_FALSE(w.changing());
    EXPECT_NEAR(w.state().rain, 1.0f, 1e-5f);
    EXPECT_EQ(count(rest, K::Changed), 1u);
    // blended values rise smoothly: no jumps
    Weather v;
    v.setWeather("rain", 20.0f);
    float last = 0.0f, biggestStep = 0.0f;
    for (int i = 0; i < 220; ++i) {
        v.update(0.1f);
        biggestStep = std::max(biggestStep, std::abs(v.state().rain - last));
        last = v.state().rain;
    }
    EXPECT_LT(biggestStep, 0.02f);
}

TEST(Weather, ANewChangeStartsFromWhereTheLastOneWas) {
    Weather w;
    w.setWeather("storm", 20.0f);
    run(w, 10.0f); // halfway to a storm
    const float rainNow = w.state().rain;
    EXPECT_GT(rainNow, 0.3f);
    EXPECT_LT(rainNow, 0.7f);
    w.setWeather("clear", 20.0f);
    w.update(0.01f);
    EXPECT_NEAR(w.state().rain, rainNow, 0.02f) << "no jump back: it continues from where it was";
    run(w, 20.5f);
    EXPECT_NEAR(w.state().rain, 0.0f, 1e-5f);
}

TEST(Weather, RainAndSnowAnnounceThemselvesWithoutFlickering) {
    Weather w;
    w.setWeather("rain", 20.0f);
    auto events = run(w, 25.0f);
    EXPECT_EQ(count(events, K::RainStarted), 1u);
    EXPECT_EQ(count(events, K::RainStopped), 0u);
    EXPECT_EQ(count(events, K::SnowStarted), 0u);
    w.setWeather("clear", 20.0f);
    events = run(w, 25.0f);
    EXPECT_EQ(count(events, K::RainStopped), 1u);
    EXPECT_EQ(count(events, K::RainStarted), 0u);
    // drizzle hovering near the threshold does not start and stop it every frame
    WeatherType edge;
    edge.name = "edge";
    edge.rain = 0.04f;
    w.defineType(edge);
    w.setWeather("edge", 0.0f);
    events = run(w, 30.0f);
    EXPECT_LE(count(events, K::RainStarted) + count(events, K::RainStopped), 1u);
    w.setWeather("snow", 10.0f);
    events = run(w, 15.0f);
    EXPECT_EQ(count(events, K::SnowStarted), 1u);
}

TEST(Weather, SnowFallsAsRainWhenItIsWarm) {
    Weather w;
    WeatherType slushy;
    slushy.name = "slushy";
    slushy.snow = 0.8f;
    slushy.temperature = 6.0f;
    ASSERT_TRUE(w.defineType(slushy));
    w.setWeather("slushy", 0.0f);
    w.setTimeOfDay(15.0f);
    w.update(0.1f);
    EXPECT_NEAR(w.state().snowFall, 0.0f, 1e-6f);
    EXPECT_NEAR(w.state().rainFall, 0.8f, 1e-5f) << "above 2 degrees it is rain";
    WeatherType cold = slushy;
    cold.name = "cold";
    cold.temperature = -3.0f;
    ASSERT_TRUE(w.defineType(cold));
    w.setWeather("cold", 0.0f);
    w.update(0.1f);
    EXPECT_NEAR(w.state().snowFall, 0.8f, 1e-5f);
    EXPECT_NEAR(w.state().rainFall, 0.0f, 1e-6f);
}

TEST(Weather, TheDayIsWarmerThanTheNight) {
    Weather w;
    w.setWeather("clear", 0.0f);
    w.dailyRange = 10.0f;
    w.setTimeOfDay(15.0f);
    w.update(0.1f);
    const float noon = w.state().temperature;
    EXPECT_NEAR(noon, w.type("clear")->temperature, 0.01f) << "the type's temperature is the warmest part of the day";
    w.setTimeOfDay(3.0f);
    w.update(0.1f);
    EXPECT_NEAR(w.state().temperature, noon - 10.0f, 0.01f);
    w.setTimeOfDay(9.0f);
    w.update(0.1f);
    EXPECT_NEAR(w.state().temperature, noon - 5.0f, 0.01f) << "half way between at mid morning";
    float cold = 100.0f, warm = -100.0f;
    for (int h = 0; h < 24; ++h) {
        w.setTimeOfDay(static_cast<float>(h));
        w.update(0.01f);
        cold = std::min(cold, w.state().temperature);
        warm = std::max(warm, w.state().temperature);
    }
    EXPECT_NEAR(warm - cold, 10.0f, 0.5f);
}

TEST(Weather, TheWindGustsAroundItsMeanAndBlowsTheWayItIsToldTo) {
    Weather w;
    w.setWeather("rain", 0.0f); // wind 7, gust 0.5
    w.setWindDirection(90.0f);  // towards +x
    double sum = 0.0;
    float lowest = 1e9f, highest = 0.0f;
    const int steps = 20000;
    for (int i = 0; i < steps; ++i) {
        w.update(0.05f);
        const WeatherState& s = w.state();
        sum += s.windSpeed;
        lowest = std::min(lowest, s.windSpeed);
        highest = std::max(highest, s.windSpeed);
        EXPECT_NEAR(glm::length(s.windVector), s.windSpeed, 1e-3f);
        EXPECT_GT(s.windVector.x, 0.0f) << "towards +x (give or take the slow drift of direction)";
        EXPECT_LT(std::abs(s.windDirection - 90.0f), 13.0f);
    }
    EXPECT_NEAR(sum / steps, 7.0, 0.7) << "the mean is the type's";
    EXPECT_LT(lowest, 5.0f);
    EXPECT_GT(highest, 9.0f);
    EXPECT_GE(lowest, 0.0f);
    EXPECT_LE(highest, 7.0f * 1.5f + 1e-3f) << "never more than mean x (1 + gust)";
    // a wind that does not gust is steady
    Weather calm;
    WeatherType steady;
    steady.name = "steady";
    steady.wind = 5.0f;
    steady.gust = 0.0f;
    calm.defineType(steady);
    calm.setWeather("steady", 0.0f);
    calm.update(1.0f);
    EXPECT_NEAR(calm.state().windSpeed, 5.0f, 1e-5f);
}

TEST(Weather, RainSoaksTheGroundAndTheSunDriesIt) {
    Weather w;
    w.setTimeOfDay(15.0f);
    w.setWeather("storm", 0.0f);
    float last = 0.0f;
    for (int i = 0; i < 700; ++i) { // 70 s of a downpour: it soaks in about a minute
        w.update(0.1f);
        EXPECT_GE(w.state().wetness, last) << i;
        last = w.state().wetness;
    }
    EXPECT_NEAR(w.state().wetness, 1.0f, 1e-5f);
    // dry weather: it dries, slowly, and faster in sun than under cloud
    auto dryTime = [](const char* weather) {
        Weather d;
        d.setTimeOfDay(15.0f);
        d.setWeather(weather, 0.0f);
        d.setGround(1.0f, 0.0f);
        float t = 0.0f;
        while (d.state().wetness > 0.5f && t < 5000.0f) {
            d.update(1.0f);
            t += 1.0f;
        }
        return t;
    };
    const float sunny = dryTime("clear"), grey = dryTime("overcast"), hot = dryTime("heat");
    EXPECT_GT(sunny, 20.0f) << "not at once";
    EXPECT_LT(sunny, grey) << "faster in the sun";
    EXPECT_LT(hot, grey) << "and in the heat";
    // frozen ground does not dry
    Weather ice;
    WeatherType frozen;
    frozen.name = "frozen";
    frozen.temperature = -10.0f;
    frozen.clouds = 0.1f;
    ice.defineType(frozen);
    ice.setWeather("frozen", 0.0f);
    ice.setGround(1.0f, 0.0f);
    for (int i = 0; i < 100; ++i) ice.update(1.0f);
    EXPECT_GT(ice.state().wetness, 0.85f);
}

TEST(Weather, SnowBuildsInTheColdAndMeltsAsItWarms) {
    Weather w;
    w.setTimeOfDay(15.0f);
    w.setWeather("blizzard", 0.0f);
    float last = 0.0f;
    for (int i = 0; i < 1300; ++i) { // 130 s: full snow takes 2 minutes of a blizzard
        w.update(0.1f);
        EXPECT_GE(w.state().snowCover, last) << i;
        last = w.state().snowCover;
    }
    EXPECT_NEAR(w.state().snowCover, 1.0f, 1e-5f);
    // a lighter snow builds slower
    Weather light;
    light.setTimeOfDay(15.0f);
    light.setWeather("snow", 0.0f);
    for (int i = 0; i < 600; ++i) light.update(0.1f);
    EXPECT_GT(light.state().snowCover, 0.2f);
    EXPECT_LT(light.state().snowCover, 0.6f);
    // warm weather melts it, into wet ground
    Weather melt;
    melt.setTimeOfDay(15.0f);
    melt.setWeather("clear", 0.0f);
    melt.setGround(0.0f, 1.0f);
    const auto at = [&](int seconds) {
        for (int i = 0; i < seconds; ++i) melt.update(1.0f);
        return melt.state().snowCover;
    };
    const float after30 = at(30);
    EXPECT_LT(after30, 1.0f);
    EXPECT_GT(melt.state().wetness, 0.0f) << "melting makes the ground wet";
    at(400);
    EXPECT_NEAR(melt.state().snowCover, 0.0f, 1e-5f);
    // rain melts it faster than sun
    auto timeToMelt = [](const char* weather) {
        Weather m;
        m.setTimeOfDay(15.0f);
        m.setWeather(weather, 0.0f);
        m.setGround(0.0f, 1.0f);
        float t = 0.0f;
        while (m.state().snowCover > 0.0f && t < 3000.0f) {
            m.update(1.0f);
            t += 1.0f;
        }
        return t;
    };
    EXPECT_LT(timeToMelt("rain"), timeToMelt("overcast"));
    // and it does not melt in the cold
    Weather cold;
    cold.setWeather("snow", 0.0f);
    cold.setGround(0.0f, 0.5f);
    cold.setTimeOfDay(3.0f);
    float before = cold.state().snowCover;
    for (int i = 0; i < 200; ++i) cold.update(1.0f);
    EXPECT_GE(cold.state().snowCover, before);
}

TEST(Weather, AStormStrikesFlashesAndThunders) {
    Weather w(7);
    w.setWeather("storm", 0.0f);
    const auto events = run(w, 600.0f, 0.05f); // ten minutes of a storm at 8 strikes a minute
    const size_t strikes = count(events, K::Lightning), thunder = count(events, K::Thunder);
    EXPECT_GT(strikes, 40u) << "about 80";
    EXPECT_LT(strikes, 130u);
    EXPECT_LE(thunder, strikes);
    EXPECT_GE(thunder + 10, strikes) << "all but the last few have been heard";
    // each thunder comes distance / 343 seconds after its flash
    std::vector<Timed> flashes;
    for (const Timed& t : events)
        if (t.e.kind == K::Lightning) flashes.push_back(t);
    size_t checked = 0;
    for (const Timed& t : events) {
        if (t.e.kind != K::Thunder) continue;
        // find the strike with this distance
        for (const Timed& f : flashes)
            if (std::abs(f.e.distance - t.e.distance) < 1e-3f) {
                EXPECT_NEAR(t.at - f.at, f.e.distance / 343.0f, 0.1f);
                EXPECT_NEAR(f.e.delay, f.e.distance / 343.0f, 1e-3f);
                EXPECT_GE(f.e.distance, 300.0f);
                EXPECT_LE(f.e.distance, 7800.0f);
                EXPECT_GE(f.e.bearing, 0.0f);
                EXPECT_LT(f.e.bearing, 360.0f);
                ++checked;
                break;
            }
    }
    EXPECT_GT(checked, 30u);
    // no strikes in clear weather
    Weather clear(7);
    EXPECT_EQ(count(run(clear, 300.0f), K::Lightning), 0u);
}

TEST(Weather, TheFlashFlickersAndFades) {
    Weather w(3);
    w.setWeather("storm", 0.0f);
    float peak = 0.0f;
    bool flashed = false;
    for (int i = 0; i < 20000 && !flashed; ++i) {
        w.update(0.01f);
        for (const WeatherEvent& e : w.takeEvents())
            if (e.kind == K::Lightning) flashed = true;
    }
    ASSERT_TRUE(flashed);
    // the flash of that strike: rises fast, a second pulse, gone within a second
    std::vector<float> trace;
    for (int i = 0; i < 150; ++i) {
        trace.push_back(w.state().flash);
        w.update(0.01f);
        w.takeEvents();
    }
    peak = *std::max_element(trace.begin(), trace.begin() + 10);
    EXPECT_GT(peak, 0.2f);
    EXPECT_LE(peak, 1.0f);
    // two pulses: it fades to a dip (about 0.17 s) and comes back with the second
    const size_t peakAt = static_cast<size_t>(std::max_element(trace.begin(), trace.begin() + 10) - trace.begin());
    const size_t dipAt = static_cast<size_t>(std::min_element(trace.begin() + static_cast<std::ptrdiff_t>(peakAt) + 1, trace.begin() + 19) - trace.begin());
    EXPECT_LT(trace[dipAt], peak * 0.5f) << "it fades between the pulses";
    const float second = *std::max_element(trace.begin() + static_cast<std::ptrdiff_t>(dipAt), trace.begin() + 40);
    EXPECT_GT(second, trace[dipAt] + 0.1f * peak) << "the second pulse";
    EXPECT_LT(second, peak) << "weaker than the first";
    // and it dies away (a calm after the storm)
    w.setWeather("clear", 0.0f);
    for (int i = 0; i < 300; ++i) w.update(0.01f);
    EXPECT_NEAR(w.state().flash, 0.0f, 1e-3f);
}

TEST(Weather, RandomWeatherWalksTheTransitionsAndRepeatsWithTheSeed) {
    auto sequence = [](uint32_t seed) {
        Weather w(seed);
        w.randomWeather(true, 20.0f, 40.0f, 5.0f);
        std::vector<std::string> names;
        for (int i = 0; i < 20000; ++i) {
            w.update(0.1f);
            for (const WeatherEvent& e : w.takeEvents())
                if (e.kind == K::Changing) names.push_back(e.from + ">" + e.name);
        }
        return names;
    };
    const auto a = sequence(5), b = sequence(5), c = sequence(6);
    EXPECT_GT(a.size(), 20u) << "a change every 20 to 45 seconds over half an hour";
    EXPECT_EQ(a, b) << "the same seed, the same weather";
    EXPECT_NE(a, c);
    // every step is one the default transitions allow
    Weather defaults;
    for (const std::string& step : a) {
        const size_t gt = step.find('>');
        const std::string from = step.substr(0, gt), to = step.substr(gt + 1);
        EXPECT_NE(from, to);
        EXPECT_TRUE(defaults.hasType(from) && defaults.hasType(to)) << step;
    }
    // storms only come out of rain, never out of clear skies
    for (const std::string& step : a) EXPECT_NE(step, "clear>storm");
    Weather off;
    off.randomWeather(true, 20.0f, 40.0f);
    off.randomWeather(false);
    EXPECT_FALSE(off.random());
    EXPECT_TRUE(run(off, 200.0f).empty());
}

TEST(Weather, TransitionsCanBeReplacedAndWeightsAreRespected) {
    Weather w(2);
    w.setTransitions("clear", { { "fog", 1.0f }, { "ghost", 5.0f } }); // a type that does not exist is skipped
    w.setTransitions("fog", { { "clear", 1.0f } });
    w.randomWeather(true, 10.0f, 12.0f, 1.0f);
    std::vector<std::string> names;
    for (int i = 0; i < 6000; ++i) {
        w.update(0.1f);
        for (const WeatherEvent& e : w.takeEvents())
            if (e.kind == K::Changing) names.push_back(e.name);
    }
    ASSERT_GT(names.size(), 10u);
    for (size_t i = 0; i < names.size(); ++i) EXPECT_EQ(names[i], i % 2 == 0 ? "fog" : "clear");
    // a weather with nothing after it stays
    Weather stuck(2);
    stuck.setTransitions("clear", {});
    stuck.randomWeather(true, 5.0f, 6.0f);
    EXPECT_EQ(count(run(stuck, 100.0f), K::Changing), 0u);
}

TEST(Weather, FogShortensTheView) {
    Weather w;
    w.setWeather("clear", 0.0f);
    w.update(0.1f);
    const float clear = w.state().visibility;
    w.setWeather("fog", 0.0f);
    w.update(0.1f);
    EXPECT_LT(w.state().visibility, 300.0f);
    EXPECT_GT(clear, 5000.0f);
    EXPECT_GT(w.state().fogDensity, 0.015f);
    w.setWeather("storm", 0.0f);
    w.update(0.1f);
    EXPECT_GT(w.state().fogDensity, 0.006f);
    EXPECT_LT(w.state().visibility, clear);
}

TEST(Weather, TypesAndTransitionsLoadFromJson) {
    Weather w;
    std::string error;
    const json j = json::parse(R"({
        "types": [
            { "name": "sandstorm", "clouds": 0.3, "fog": 0.8, "wind": 20, "gust": 0.9, "temperature": 38, "sunlight": 0.4, "saturation": 0.6 },
            { "name": "rain", "wind": 12 } ],
        "transitions": { "clear": { "sandstorm": 2 }, "sandstorm": { "clear": 1 } } })");
    ASSERT_TRUE(w.fromJson(j, &error)) << error;
    ASSERT_NE(w.type("sandstorm"), nullptr);
    EXPECT_FLOAT_EQ(w.type("sandstorm")->wind, 20.0f);
    EXPECT_FLOAT_EQ(w.type("sandstorm")->rain, 0.0f) << "a new type starts as clear weather";
    ASSERT_NE(w.type("rain"), nullptr);
    EXPECT_FLOAT_EQ(w.type("rain")->wind, 12.0f) << "changing a default changes only what is given";
    EXPECT_FLOAT_EQ(w.type("rain")->rain, 0.6f);
    auto bad = [&](const char* text, const std::string& expect) {
        Weather x;
        EXPECT_FALSE(x.fromJson(json::parse(text), &error)) << text;
        EXPECT_NE(error.find(expect), std::string::npos) << "'" << error << "' should say '" << expect << "'";
    };
    bad("[]", "an object");
    bad(R"({ "types": 5 })", "types is a list");
    bad(R"({ "types": [ 5 ] })", "types[0] needs a name");
    bad(R"({ "types": [ { "name": "x", "rain": "lots" } ] })", "types[0] ('x'): rain must be a number");
    bad(R"({ "types": [ { "name": "x", "rain": 2 } ] })", "types[0]: weather 'x': rain is 0 to 1");
    bad(R"({ "transitions": 5 })", "transitions is an object");
    bad(R"({ "transitions": { "clear": 5 } })", "transitions.clear is an object");
    bad(R"({ "transitions": { "clear": { "fog": -1 } } })", "weight of 0 or more");
    EXPECT_TRUE(Weather().fromJson(json::parse(R"({ "types": {}, "transitions": [] })"), &error)) << "empty Lua tables arrive like this";
}

TEST(Weather, TheGroundCanBeSetAndTimeStepsAreChecked) {
    Weather w;
    w.setGround(0.7f, 2.0f);
    EXPECT_FLOAT_EQ(w.state().wetness, 0.7f);
    EXPECT_FLOAT_EQ(w.state().snowCover, 1.0f) << "clamped";
    w.setGround(-1.0f, std::nanf(""));
    EXPECT_FLOAT_EQ(w.state().wetness, 0.0f);
    EXPECT_FLOAT_EQ(w.state().snowCover, 1.0f) << "not a number: unchanged";
    const float t = w.time();
    w.update(0.0f);
    w.update(-3.0f);
    w.update(std::nanf(""));
    EXPECT_FLOAT_EQ(w.time(), t) << "bad time steps are ignored";
    w.setWeather("storm", 5.0f);
    w.update(1.0e9f); // an enormous step (a long pause): no explosion
    EXPECT_LE(w.state().wetness, 1.0f);
    EXPECT_GE(w.state().wetness, 0.0f);
    EXPECT_TRUE(std::isfinite(w.state().windSpeed));
}

TEST(Weather, EventsAreTakenOnce) {
    Weather w;
    w.setWeather("rain", 5.0f);
    EXPECT_FALSE(w.takeEvents().empty());
    EXPECT_TRUE(w.takeEvents().empty());
}
