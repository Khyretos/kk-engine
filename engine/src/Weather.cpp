#include "kke/Weather.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

using nlohmann::json;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSoundSpeed = 343.0f;

WeatherType preset(const char* name, float clouds, float rain, float snow, float fog, float wind, float gust, float temperature, float lightning,
                   float sunlight, float ambient, float saturation) {
    WeatherType t;
    t.name = name;
    t.clouds = clouds;
    t.rain = rain;
    t.snow = snow;
    t.fog = fog;
    t.wind = wind;
    t.gust = gust;
    t.temperature = temperature;
    t.lightning = lightning;
    t.sunlight = sunlight;
    t.ambient = ambient;
    t.saturation = saturation;
    return t;
}

bool check(const WeatherType& t, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = "weather '" + t.name + "': " + why;
        return false;
    };
    if (t.name.empty()) {
        if (error) *error = "a weather type needs a name";
        return false;
    }
    for (float v : { t.clouds, t.rain, t.snow, t.fog, t.wind, t.gust, t.temperature, t.lightning, t.sunlight, t.ambient, t.saturation })
        if (!std::isfinite(v)) return fail("numbers must be finite");
    if (t.clouds < 0 || t.clouds > 1) return fail("clouds is 0 to 1");
    if (t.rain < 0 || t.rain > 1) return fail("rain is 0 to 1");
    if (t.snow < 0 || t.snow > 1) return fail("snow is 0 to 1");
    if (t.fog < 0 || t.fog > 1) return fail("fog is 0 to 1");
    if (t.gust < 0 || t.gust > 1) return fail("gust is 0 to 1");
    if (t.wind < 0 || t.wind > 100) return fail("wind is 0 to 100 m/s");
    if (t.temperature < -80 || t.temperature > 80) return fail("temperature is -80 to 80 degrees");
    if (t.lightning < 0 || t.lightning > 60) return fail("lightning is 0 to 60 strikes a minute");
    if (t.sunlight < 0 || t.sunlight > 4) return fail("sunlight is 0 to 4");
    if (t.ambient < 0 || t.ambient > 4) return fail("ambient is 0 to 4");
    if (t.saturation < 0 || t.saturation > 2) return fail("saturation is 0 to 2");
    return true;
}

} // namespace

Weather::Params Weather::paramsOf(const WeatherType& t) {
    return { t.clouds, t.rain, t.snow, t.fog, t.wind, t.gust, t.temperature, t.lightning, t.sunlight, t.ambient, t.saturation };
}

Weather::Weather(uint32_t seed) : m_rng(seed) {
    for (const WeatherType& t : {
             preset("clear", 0.05f, 0, 0, 0, 2, 0.2f, 18, 0, 1.0f, 1.0f, 1.0f),
             preset("partly_cloudy", 0.4f, 0, 0, 0, 4, 0.3f, 16, 0, 0.85f, 0.95f, 0.98f),
             preset("overcast", 0.9f, 0, 0, 0.05f, 5, 0.3f, 12, 0, 0.45f, 0.8f, 0.85f),
             preset("drizzle", 0.85f, 0.25f, 0, 0.1f, 4, 0.3f, 11, 0, 0.4f, 0.75f, 0.8f),
             preset("rain", 0.95f, 0.6f, 0, 0.2f, 7, 0.5f, 10, 0, 0.3f, 0.65f, 0.7f),
             preset("storm", 1.0f, 1.0f, 0, 0.3f, 14, 0.8f, 9, 8, 0.18f, 0.5f, 0.6f),
             preset("fog", 0.7f, 0, 0, 0.9f, 1, 0.1f, 8, 0, 0.5f, 0.85f, 0.7f),
             preset("snow", 0.9f, 0, 0.5f, 0.25f, 3, 0.3f, -2, 0, 0.5f, 0.9f, 0.7f),
             preset("blizzard", 1.0f, 0, 1.0f, 0.6f, 16, 0.8f, -8, 0, 0.25f, 0.8f, 0.5f),
             preset("heat", 0.0f, 0, 0, 0, 1, 0.1f, 35, 0, 1.1f, 1.0f, 1.05f),
         })
        m_types[t.name] = t;
    m_transitions = {
        { "clear", { { "partly_cloudy", 3 }, { "heat", 0.5f } } },
        { "partly_cloudy", { { "clear", 3 }, { "overcast", 2 }, { "fog", 0.5f } } },
        { "overcast", { { "partly_cloudy", 2 }, { "drizzle", 2 }, { "rain", 1 }, { "fog", 0.5f } } },
        { "drizzle", { { "overcast", 2 }, { "rain", 2 }, { "partly_cloudy", 1 } } },
        { "rain", { { "drizzle", 2 }, { "storm", 1 }, { "overcast", 2 } } },
        { "storm", { { "rain", 3 }, { "overcast", 1 } } },
        { "fog", { { "overcast", 2 }, { "clear", 1 } } },
        { "snow", { { "overcast", 2 }, { "blizzard", 1 }, { "partly_cloudy", 1 } } },
        { "blizzard", { { "snow", 3 } } },
        { "heat", { { "clear", 3 }, { "partly_cloudy", 1 } } },
    };
    for (float& p : m_phase) p = unit() * 2.0f * kPi;
    applyParams(paramsOf(m_types["clear"]));
    m_state.windSpeed = m_state.wind;
}

float Weather::unit() { return static_cast<float>(m_rng() >> 8) * (1.0f / 16777216.0f); } // the same on every platform

void Weather::push(WeatherEvent e) {
    if (m_events.size() < 4096) m_events.push_back(std::move(e));
}

bool Weather::defineType(const WeatherType& type, std::string* error) {
    if (!check(type, error)) return false;
    if (m_types.count(type.name) == 0 && m_types.size() >= kMaxTypes) {
        if (error) *error = "more than " + std::to_string(kMaxTypes) + " weather types";
        return false;
    }
    m_types[type.name] = type;
    return true;
}

const WeatherType* Weather::type(const std::string& name) const {
    const auto it = m_types.find(name);
    return it == m_types.end() ? nullptr : &it->second;
}

std::vector<std::string> Weather::typeNames() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : m_types) out.push_back(k);
    return out;
}

bool Weather::fromJson(const json& j, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!j.is_object()) return fail("weather is an object {types, transitions}");
    if (const auto ts = j.find("types"); ts != j.end() && !(ts->is_object() && ts->empty())) {
        if (!ts->is_array()) return fail("types is a list");
        for (size_t i = 0; i < ts->size(); ++i) {
            const json& tj = (*ts)[i];
            const std::string where = "types[" + std::to_string(i) + "]";
            if (!tj.is_object() || !tj.contains("name") || !tj["name"].is_string()) return fail(where + " needs a name");
            WeatherType t;
            t.name = tj["name"].get<std::string>();
            struct { const char* key; float* field; } fields[] = {
                { "clouds", &t.clouds }, { "rain", &t.rain }, { "snow", &t.snow }, { "fog", &t.fog }, { "wind", &t.wind }, { "gust", &t.gust },
                { "temperature", &t.temperature }, { "lightning", &t.lightning }, { "sunlight", &t.sunlight }, { "ambient", &t.ambient },
                { "saturation", &t.saturation },
            };
            // a type starts from the weather of the same name (change one thing about "rain"), or from clear
            if (const WeatherType* base = type(t.name)) t = *base;
            else if (const WeatherType* clear = type("clear")) {
                const std::string name = t.name;
                t = *clear;
                t.name = name;
            }
            for (auto& f : fields) {
                const auto it = tj.find(f.key);
                if (it == tj.end()) continue;
                if (!it->is_number()) return fail(where + " ('" + t.name + "'): " + f.key + " must be a number");
                *f.field = it->get<float>();
            }
            std::string why;
            if (!defineType(t, &why)) return fail(where + ": " + why);
        }
    }
    if (const auto tr = j.find("transitions"); tr != j.end() && !(tr->is_array() && tr->empty())) {
        if (!tr->is_object()) return fail("transitions is an object {from: {to: weight}}");
        for (auto it = tr->begin(); it != tr->end(); ++it) {
            if (!it->is_object()) return fail("transitions." + it.key() + " is an object {to: weight}");
            std::map<std::string, float> next;
            for (auto n = it->begin(); n != it->end(); ++n) {
                if (!n->is_number() || n->get<float>() < 0.0f) return fail("transitions." + it.key() + "." + n.key() + " is a weight of 0 or more");
                next[n.key()] = n->get<float>();
            }
            setTransitions(it.key(), next);
        }
    }
    return true;
}

void Weather::setTransitions(const std::string& from, const std::map<std::string, float>& next) { m_transitions[from] = next; }

void Weather::applyParams(const Params& p) {
    m_state.clouds = p[0];
    m_state.rain = p[1];
    m_state.snow = p[2];
    m_state.fog = p[3];
    m_state.wind = p[4];
    m_state.gust = p[5];
    m_tempBase = p[6];
    m_state.lightning = p[7];
    m_state.sunlight = p[8];
    m_state.ambient = p[9];
    m_state.saturation = p[10];
}

Weather::Params Weather::blended() const {
    return { m_state.clouds, m_state.rain, m_state.snow, m_state.fog, m_state.wind, m_state.gust, m_tempBase, m_state.lightning,
             m_state.sunlight, m_state.ambient, m_state.saturation };
}

void Weather::begin(const std::string& to, float seconds) {
    WeatherEvent e;
    e.kind = WeatherEvent::Kind::Changing;
    e.name = to;
    e.from = m_target;
    push(e);
    m_from = blended();
    m_to = paramsOf(m_types.at(to));
    m_target = to;
    if (!(seconds > 0.0f)) {
        applyParams(m_to);
        m_blending = false;
        WeatherEvent done;
        done.kind = WeatherEvent::Kind::Changed;
        done.name = to;
        push(done);
        return;
    }
    m_blending = true;
    m_elapsed = 0.0;
    m_duration = static_cast<double>(seconds);
}

bool Weather::setWeather(const std::string& name, float seconds) {
    if (!m_types.count(name)) return false;
    begin(name, std::isfinite(seconds) ? seconds : 0.0f);
    return true;
}

void Weather::randomWeather(bool on, float minSeconds, float maxSeconds, float transition) {
    m_random = on;
    m_randomMin = std::max(1.0f, std::isfinite(minSeconds) ? minSeconds : 120.0f);
    m_randomMax = std::max(m_randomMin, std::isfinite(maxSeconds) ? maxSeconds : 300.0f);
    m_randomTransition = std::max(0.0f, std::isfinite(transition) ? transition : 40.0f);
    m_hold = static_cast<double>(m_randomMin + unit() * (m_randomMax - m_randomMin));
}

void Weather::pickNext() {
    m_hold = static_cast<double>(m_randomMin + unit() * (m_randomMax - m_randomMin));
    const auto it = m_transitions.find(m_target);
    if (it == m_transitions.end()) return;
    float total = 0.0f;
    for (const auto& [name, w] : it->second)
        if (m_types.count(name) && w > 0.0f) total += w;
    if (total <= 0.0f) return;
    float r = unit() * total;
    for (const auto& [name, w] : it->second) {
        if (!m_types.count(name) || w <= 0.0f) continue;
        r -= w;
        if (r < 0.0f) {
            begin(name, m_randomTransition * (0.6f + 0.8f * unit()));
            return;
        }
    }
}

void Weather::setGround(float wetness, float snowCover) {
    if (std::isfinite(wetness)) m_state.wetness = std::clamp(wetness, 0.0f, 1.0f);
    if (std::isfinite(snowCover)) m_state.snowCover = std::clamp(snowCover, 0.0f, 1.0f);
}

void Weather::update(float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    dt = std::min(dt, 10.0f);
    m_time += static_cast<double>(dt);
    WeatherState& s = m_state;

    // the blend towards a new type
    if (m_blending) {
        m_elapsed += static_cast<double>(dt);
        const float t = static_cast<float>(std::min(1.0, m_elapsed / m_duration));
        const float e = t * t * (3.0f - 2.0f * t);
        Params p;
        for (size_t i = 0; i < p.size(); ++i) p[i] = m_from[i] + (m_to[i] - m_from[i]) * e;
        applyParams(p);
        if (t >= 1.0f) {
            applyParams(m_to);
            m_blending = false;
            WeatherEvent done;
            done.kind = WeatherEvent::Kind::Changed;
            done.name = m_target;
            push(done);
        }
    }
    if (m_random && !m_blending) {
        m_hold -= static_cast<double>(dt);
        if (m_hold <= 0.0) pickNext();
    }

    // wind: a mean, a gust pattern that never repeats exactly, a slow drift in direction
    {
        const double t = m_time;
        const float n = 0.6f * static_cast<float>(std::sin(0.7 * t + static_cast<double>(m_phase[0]))) +
                        0.3f * static_cast<float>(std::sin(1.9 * t + static_cast<double>(m_phase[1]))) +
                        0.1f * static_cast<float>(std::sin(5.3 * t + static_cast<double>(m_phase[2])));
        s.windSpeed = std::max(0.0f, s.wind * (1.0f + s.gust * n));
        float dir = m_windDirection + 12.0f * static_cast<float>(std::sin(0.05 * t + static_cast<double>(m_phase[1])));
        dir = std::fmod(dir, 360.0f);
        if (dir < 0.0f) dir += 360.0f;
        s.windDirection = dir;
        const float rad = dir * kPi / 180.0f;
        s.windVector = glm::vec3(std::sin(rad), 0.0f, std::cos(rad)) * s.windSpeed;
    }

    // temperature: the type's is the warmest part of the day (15:00); it is colder by night
    s.temperature = m_tempBase + dailyRange * 0.5f * (std::sin(2.0f * kPi * (m_hour - 9.0f) / 24.0f) - 1.0f);

    // what falls: snow is rain when it is warm
    const bool warm = s.temperature > 2.0f;
    s.rainFall = std::min(1.0f, s.rain + (warm ? s.snow : 0.0f));
    s.snowFall = warm ? 0.0f : s.snow;
    s.fogDensity = 0.02f * s.fog + 0.0015f * s.rainFall + 0.004f * s.snowFall;
    s.visibility = std::min(20000.0f, 3.912f / (s.fogDensity + 1.0e-6f));
    auto edge = [&](bool& on, float level, WeatherEvent::Kind started, WeatherEvent::Kind stopped) {
        if (!on && level > 0.05f) {
            on = true;
            WeatherEvent e;
            e.kind = started;
            e.name = m_target;
            push(e);
        } else if (on && level < 0.03f) {
            on = false;
            WeatherEvent e;
            e.kind = stopped;
            e.name = m_target;
            push(e);
        }
    };
    edge(m_raining, s.rainFall, WeatherEvent::Kind::RainStarted, WeatherEvent::Kind::RainStopped);
    edge(m_snowing, s.snowFall, WeatherEvent::Kind::SnowStarted, WeatherEvent::Kind::SnowStopped);

    // the ground: rain soaks it, sun and wind dry it; snow builds in the cold and melts as it warms
    const float sunny = s.sunlight * (1.0f - s.clouds);
    if (s.rainFall > 0.02f) {
        s.wetness += s.rainFall * dt / std::max(1.0f, soakSeconds);
    } else {
        float rate = (0.25f + 0.75f * std::clamp(sunny, 0.0f, 1.0f)) * (1.0f + s.windSpeed / 10.0f) / std::max(1.0f, drySeconds);
        if (s.temperature < 0.0f) rate *= 0.2f; // frozen: it does not dry
        s.wetness -= rate * dt;
    }
    if (s.snowFall > 0.02f && s.temperature <= 1.0f) {
        s.snowCover += s.snowFall * dt / std::max(1.0f, snowBuildSeconds);
    } else if (s.temperature > 0.0f && s.snowCover > 0.0f) {
        float melt = dt / std::max(1.0f, meltSeconds) * (1.0f + s.temperature / 5.0f + 2.0f * std::clamp(sunny, 0.0f, 1.0f));
        if (s.rainFall > 0.02f) melt += s.rainFall * dt / std::max(1.0f, meltSeconds) * 3.0f;
        const float gone = std::min(melt, s.snowCover);
        s.snowCover -= gone;
        s.wetness += gone * 0.5f;
    }
    s.wetness = std::clamp(s.wetness, 0.0f, 1.0f);
    s.snowCover = std::clamp(s.snowCover, 0.0f, 1.0f);

    // lightning: strikes at random, a flash that flickers, thunder that is heard later
    if (s.lightning > 0.01f) {
        const float perSecond = s.lightning / 60.0f;
        if (unit() < 1.0f - std::exp(-perSecond * dt)) {
            const float distance = 300.0f + std::pow(unit(), 1.5f) * 7500.0f;
            const float amplitude = std::clamp(1.2f - distance / 6000.0f, 0.25f, 1.0f);
            m_pulses.push_back({ m_time, amplitude });
            m_pulses.push_back({ m_time + 0.17, amplitude * 0.7f });
            m_thunder.push_back({ m_time + static_cast<double>(distance / kSoundSpeed), distance });
            WeatherEvent e;
            e.kind = WeatherEvent::Kind::Lightning;
            e.name = m_target;
            e.distance = distance;
            e.bearing = unit() * 360.0f;
            e.delay = distance / kSoundSpeed;
            push(e);
        }
    }
    float flash = 0.0f;
    for (const Pulse& p : m_pulses) {
        const float t = static_cast<float>(m_time - p.start);
        if (t < 0.0f) continue;
        flash = std::max(flash, p.amplitude * (t < 0.03f ? t / 0.03f : std::exp(-(t - 0.03f) / 0.08f)));
    }
    s.flash = std::clamp(flash, 0.0f, 1.0f);
    m_pulses.erase(std::remove_if(m_pulses.begin(), m_pulses.end(), [&](const Pulse& p) { return m_time - p.start > 1.0; }), m_pulses.end());
    for (auto it = m_thunder.begin(); it != m_thunder.end();) {
        if (it->at <= m_time) {
            WeatherEvent e;
            e.kind = WeatherEvent::Kind::Thunder;
            e.name = m_target;
            e.distance = it->distance;
            e.delay = it->distance / kSoundSpeed;
            push(e);
            it = m_thunder.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<WeatherEvent> Weather::takeEvents() {
    std::vector<WeatherEvent> out;
    out.swap(m_events);
    return out;
}

} // namespace kke
