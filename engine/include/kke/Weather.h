#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace kke {

// Weather (ENG-F81, docs/MOODS.md "Weather"): what Unreal's Ultra Dynamic
// Weather and Unity's weather packs do, as a small system any game can run.
//
// A weather type is a handful of numbers (cloud cover, rain, snow, fog, wind,
// gusts, temperature, lightning, how much sun and sky light get through).
// Changing weather blends every number smoothly over seconds, so rain
// builds up instead of switching on. On top of the blend the system works out
// what a game needs to draw and to simulate:
//
//   - what actually falls: snow turns to rain above 2 degrees
//   - a wind that gusts, with a direction
//   - the temperature, warmer by day than at night
//   - how wet the ground is (it soaks up rain and dries in sun and wind) and how
//     deep the snow lies (it builds in the cold and melts as it warms)
//   - lightning: strikes at random, a flash that flickers, and the thunder that
//     reaches you later (343 m/s: a strike 2 km away is heard 6 s after it is seen)
//   - events when weather changes and rain or snow begins and ends
//
// Weather can follow a schedule of its own (a random walk over the types:
// clear weather clouds over, rain eases off to drizzle...). The system knows
// nothing of the engine: ScriptWeather (kke/ScriptWeather.h) turns it into
// particles, lighting and fog. Unit-tested in tests/test_weather.cpp.

struct WeatherType {
    std::string name;
    float clouds = 0.0f;       // 0..1 cover
    float rain = 0.0f;         // 0..1 intensity (0.25 drizzle, 0.6 rain, 1 downpour)
    float snow = 0.0f;         // 0..1
    float fog = 0.0f;          // 0..1 (1: you cannot see 50 m)
    float wind = 2.0f;         // m/s on average
    float gust = 0.3f;         // 0..1: how much the wind varies, as a fraction of itself
    float temperature = 15.0f; // degrees C at the warmest part of the day
    float lightning = 0.0f;    // strikes a minute
    float sunlight = 1.0f;     // multiplies the sun's intensity
    float ambient = 1.0f;      // multiplies the sky's light
    float saturation = 1.0f;   // multiplies the colours' saturation (grey days)
};

struct WeatherState {
    // The type's numbers, blended while weather changes.
    float clouds = 0.0f, rain = 0.0f, snow = 0.0f, fog = 0.0f, wind = 2.0f, gust = 0.3f, temperature = 15.0f, lightning = 0.0f;
    float sunlight = 1.0f, ambient = 1.0f, saturation = 1.0f;
    // What the system works out.
    float rainFall = 0.0f;          // rain that falls as rain (snow above 2 degrees is rain)
    float snowFall = 0.0f;
    float fogDensity = 0.0f;        // extra fog, per metre, for Fog::density
    float windSpeed = 2.0f;         // now, gusts included, m/s
    float windDirection = 0.0f;     // degrees the wind blows towards, 0 = +z (north), 90 = +x
    glm::vec3 windVector{ 0.0f };   // m/s
    float wetness = 0.0f;           // 0 dry, 1 soaked
    float snowCover = 0.0f;         // 0 bare, 1 white
    float flash = 0.0f;             // 0..1 lightning brightness right now
    float visibility = 20000.0f;    // metres, roughly
};

struct WeatherEvent {
    enum class Kind : uint8_t { Changing, Changed, RainStarted, RainStopped, SnowStarted, SnowStopped, Lightning, Thunder } kind = Kind::Changed;
    std::string name;      // Changing: the weather it goes to (`from` is the one it leaves); Changed: the one it reached
    std::string from;
    float distance = 0.0f; // Lightning, Thunder: metres away
    float bearing = 0.0f;  // Lightning: degrees, as the wind direction
    float delay = 0.0f;    // Lightning: seconds until its thunder is heard
};

class Weather {
public:
    explicit Weather(uint32_t seed = 1);

    static constexpr size_t kMaxTypes = 64;

    // ---- the types
    // The defaults are defined already: clear, partly_cloudy, overcast, drizzle, rain, storm, fog, snow, blizzard, heat.
    bool defineType(const WeatherType& type, std::string* error = nullptr);
    bool hasType(const std::string& name) const { return m_types.count(name) != 0; }
    const WeatherType* type(const std::string& name) const;
    std::vector<std::string> typeNames() const;
    // { "types": [ {name, clouds, ...} ], "transitions": { "clear": { "partly_cloudy": 3 } } }
    bool fromJson(const nlohmann::json& j, std::string* error = nullptr);

    // ---- changing
    // Blends to a type over `seconds` (0: at once). False for an unknown name.
    bool setWeather(const std::string& name, float seconds = 30.0f);
    // The weather it is on, or on its way to.
    const std::string& current() const { return m_target; }
    bool changing() const { return m_blending; }
    // Weather that changes by itself: after a random time between the two, it picks a next type by the
    // transition weights (the defaults: clear clouds over, rain eases to drizzle...) and blends to it over
    // `transition` seconds. Off with randomWeather(false).
    void randomWeather(bool on, float minSeconds = 120.0f, float maxSeconds = 300.0f, float transition = 40.0f);
    bool random() const { return m_random; }
    // Which types may follow `from` and how likely (relative weights).
    void setTransitions(const std::string& from, const std::map<std::string, float>& next);

    // ---- the world around it
    void setWindDirection(float degrees) { m_windDirection = degrees; }
    // Hour of the day (0-24): the temperature is warmest at 15:00 and coldest before dawn.
    void setTimeOfDay(float hour) { m_hour = hour; }
    // Degrees between the warmest and the coldest part of the day.
    float dailyRange = 8.0f;
    // Seconds it takes the ground to soak up to full wetness in heavy rain, and to dry in full sun; and the snow to build or melt.
    float soakSeconds = 60.0f, drySeconds = 300.0f, snowBuildSeconds = 120.0f, meltSeconds = 600.0f;

    void update(float dt);
    const WeatherState& state() const { return m_state; }
    std::vector<WeatherEvent> takeEvents();
    float time() const { return static_cast<float>(m_time); }
    // Wetness and snow cover set by hand (a game starting in a snowy place).
    void setGround(float wetness, float snowCover);

private:
    using Params = std::array<float, 11>;
    static Params paramsOf(const WeatherType& t);
    Params blended() const;
    void applyParams(const Params& p);
    void begin(const std::string& to, float seconds);
    void pickNext();
    float unit();
    void push(WeatherEvent e);

    std::map<std::string, WeatherType> m_types;
    std::map<std::string, std::map<std::string, float>> m_transitions;
    WeatherState m_state;
    std::string m_target = "clear", m_previous = "clear";
    Params m_from{}, m_to{};
    bool m_blending = false;
    double m_elapsed = 0.0, m_duration = 0.0, m_time = 0.0;
    bool m_random = false;
    float m_randomMin = 120.0f, m_randomMax = 300.0f, m_randomTransition = 40.0f;
    double m_hold = 0.0;
    float m_windDirection = 0.0f, m_hour = 12.0f;
    float m_tempBase = 18.0f; // the blended temperature of the warmest part of the day
    float m_phase[3] = { 0.0f, 0.0f, 0.0f };
    bool m_raining = false, m_snowing = false;
    struct Pulse {
        double start;
        float amplitude;
    };
    std::vector<Pulse> m_pulses;
    struct Thunder {
        double at;
        float distance;
    };
    std::vector<Thunder> m_thunder;
    std::mt19937 m_rng;
    std::vector<WeatherEvent> m_events;
};

} // namespace kke
