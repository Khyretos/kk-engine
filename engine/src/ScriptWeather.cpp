#include "kke/ScriptWeather.h"

#include "kke/Application.h"
#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <cmath>

namespace kke {

using nlohmann::json;

ScriptWeather::Applied ScriptWeather::read(const Lighting& l) {
    Applied a;
    a.sun = l.lights[0].intensity;
    a.sunEnabled = l.lights[0].enabled;
    a.exposure = l.exposure;
    a.fogDensity = l.fog.density;
    a.fogEnabled = l.fog.enabled;
    a.saturation = l.grade.saturation;
    a.ambient = l.ambientColor;
    return a;
}

void ScriptWeather::write(Lighting& l, const Applied& a) {
    l.lights[0].intensity = a.sun;
    l.exposure = a.exposure;
    l.fog.density = a.fogDensity;
    l.fog.enabled = a.fogEnabled;
    l.grade.saturation = a.saturation;
    l.ambientColor = a.ambient;
}

void ScriptWeather::applyLighting() {
    const WeatherState& s = m_weather.state();
    const Applied now = read(*m_lighting);
    if (!m_haveSet) {
        m_base = now;
    } else { // a field something else set (a mood, the clock) is the new base for that field; the others are still ours
        if (now.sun != m_set.sun || now.sunEnabled != m_set.sunEnabled) m_base.sun = now.sun, m_base.sunEnabled = now.sunEnabled;
        if (now.exposure != m_set.exposure) m_base.exposure = now.exposure;
        if (now.ambient != m_set.ambient) m_base.ambient = now.ambient;
        if (now.fogDensity != m_set.fogDensity || now.fogEnabled != m_set.fogEnabled) m_base.fogDensity = now.fogDensity, m_base.fogEnabled = now.fogEnabled;
        if (now.saturation != m_set.saturation) m_base.saturation = now.saturation;
    }
    Applied out = m_base;
    out.sun = m_base.sun * s.sunlight + s.flash * 12.0f;
    out.exposure = m_base.exposure * (0.5f + 0.5f * s.ambient) * (1.0f + 0.8f * s.flash);
    out.ambient = m_base.ambient * s.ambient;
    out.fogDensity = (m_base.fogEnabled ? m_base.fogDensity : 0.0f) + s.fogDensity;
    out.fogEnabled = m_base.fogEnabled || s.fogDensity > 0.0005f;
    out.saturation = m_base.saturation * s.saturation;
    write(*m_lighting, out);
    m_set = read(*m_lighting);
    m_haveSet = true;
}

void ScriptWeather::restoreLighting() {
    if (m_lighting && m_haveSet) { // each field goes back unless something else has changed it since
        const Applied now = read(*m_lighting);
        Applied out = now;
        if (now.sun == m_set.sun) out.sun = m_base.sun;
        if (now.exposure == m_set.exposure) out.exposure = m_base.exposure;
        if (now.ambient == m_set.ambient) out.ambient = m_base.ambient;
        if (now.fogDensity == m_set.fogDensity && now.fogEnabled == m_set.fogEnabled) out.fogDensity = m_base.fogDensity, out.fogEnabled = m_base.fogEnabled;
        if (now.saturation == m_set.saturation) out.saturation = m_base.saturation;
        write(*m_lighting, out);
    }
    m_haveSet = false;
}

void ScriptWeather::stopParticles() {
    if (precipitation) {
        precipitation("rain", "", 0, glm::vec3(0.0f), 0.0f);
        precipitation("snow", "", 0, glm::vec3(0.0f), 0.0f);
    }
    if (wind) wind(glm::vec3(0.0f));
    m_lastWind = glm::vec3(1.0e9f);
}

void ScriptWeather::release(const std::string& source) {
    if (!m_active || m_owner != source) return;
    stopParticles();
    restoreLighting();
    m_active = false;
    m_owner.clear();
}

void ScriptWeather::update(float dt, ScriptVM& vm, const glm::vec3& camera, float hour) {
    if (!m_active) return;
    if (m_hourFromClock && hour >= 0.0f) m_weather.setTimeOfDay(hour);
    m_weather.update(dt);
    for (const WeatherEvent& e : m_weather.takeEvents()) {
        const char* hook = nullptr;
        switch (e.kind) {
        case WeatherEvent::Kind::Changing: hook = "WeatherChanging"; break;
        case WeatherEvent::Kind::Changed: hook = "WeatherChanged"; break;
        case WeatherEvent::Kind::RainStarted: hook = "RainStarted"; break;
        case WeatherEvent::Kind::RainStopped: hook = "RainStopped"; break;
        case WeatherEvent::Kind::SnowStarted: hook = "SnowStarted"; break;
        case WeatherEvent::Kind::SnowStopped: hook = "SnowStopped"; break;
        case WeatherEvent::Kind::Lightning: hook = "Lightning"; break;
        case WeatherEvent::Kind::Thunder: hook = "Thunder"; break;
        }
        vm.callHookWith(hook, [&e](lua_State* L) {
            lua_createtable(L, 0, 5);
            lua_pushstring(L, e.name.c_str());
            lua_setfield(L, -2, e.kind == WeatherEvent::Kind::Changing ? "to" : "weather");
            if (e.kind == WeatherEvent::Kind::Changing) {
                lua_pushstring(L, e.from.c_str());
                lua_setfield(L, -2, "from");
            }
            if (e.kind == WeatherEvent::Kind::Lightning || e.kind == WeatherEvent::Kind::Thunder) {
                lua_pushnumber(L, static_cast<double>(e.distance));
                lua_setfield(L, -2, "distance");
                lua_pushnumber(L, static_cast<double>(e.delay));
                lua_setfield(L, -2, "delay");
            }
            if (e.kind == WeatherEvent::Kind::Lightning) {
                lua_pushnumber(L, static_cast<double>(e.bearing));
                lua_setfield(L, -2, "bearing");
            }
            return 1;
        });
    }
    const WeatherState& s = m_weather.state();
    if (m_particles && precipitation) {
        std::string rainEffect, snowEffect;
        int rainCount = 0, snowCount = 0;
        if (s.rainFall >= 0.02f) {
            if (s.rainFall >= 0.9f) {
                rainEffect = "downpour";
                rainCount = 1;
            } else {
                rainEffect = "rain";
                rainCount = std::max(1, static_cast<int>(std::ceil(s.rainFall * 3.0f)));
            }
        }
        if (s.snowFall >= 0.02f) {
            snowEffect = "snow";
            snowCount = std::max(1, static_cast<int>(std::ceil(s.snowFall * 3.0f)));
        }
        const float floor = camera.y - groundBelow;
        precipitation("rain", rainEffect, rainCount, camera + glm::vec3(0.0f, rainHeight, 0.0f), floor);
        precipitation("snow", snowEffect, snowCount, camera + glm::vec3(0.0f, snowHeight, 0.0f), floor);
    }
    if (wind && glm::length(s.windVector - m_lastWind) > 0.05f) {
        wind(s.windVector);
        m_lastWind = s.windVector;
    }
    if (m_lighting && m_lightingOn) applyLighting();
}

void ScriptWeather::bind(ScriptVM& vm) {
    auto fail = [](lua_State* L, const std::string& why) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, why.c_str());
        return 2;
    };
    auto start = [this, &vm]() {
        if (!m_active) {
            m_active = true;
            m_owner = vm.currentSource();
        }
    };
    // weather.set("rain" [, seconds]) blends to it over 30 seconds (0: at once) -> true, or false and why
    vm.registerFunction("weather", "set", [this, start, fail](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        const double seconds = lua_isnoneornil(L, 2) ? 30.0 : luaL_checknumber(L, 2);
        start();
        if (!m_weather.setWeather(name, static_cast<float>(seconds))) return fail(L, "no weather called '" + name + "' (weather.types() lists them)");
        lua_pushboolean(L, 1);
        return 1;
    });
    vm.registerFunction("weather", "start", [start](lua_State*) {
        start();
        return 0;
    });
    vm.registerFunction("weather", "current", [this](lua_State* L) {
        lua_pushstring(L, m_weather.current().c_str());
        return 1;
    });
    vm.registerFunction("weather", "changing", [this](lua_State* L) {
        lua_pushboolean(L, m_weather.changing());
        return 1;
    });
    vm.registerFunction("weather", "types", [this](lua_State* L) {
        const std::vector<std::string> names = m_weather.typeNames();
        lua_createtable(L, static_cast<int>(names.size()), 0);
        for (size_t i = 0; i < names.size(); ++i) {
            lua_pushstring(L, names[i].c_str());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    });
    // weather.define{name = "sandstorm", fog = 0.8, wind = 20, ...}: a new type, or a changed default
    vm.registerFunction("weather", "define", [this, fail](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        json j = json::object();
        j["types"] = json::array({ ScriptVM::toJson(L, 1) });
        std::string error;
        if (!m_weather.fromJson(j, &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    // weather.config{types = {...}, transitions = {clear = {partly_cloudy = 3}}}
    vm.registerFunction("weather", "config", [this, fail](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        std::string error;
        if (!m_weather.fromJson(ScriptVM::toJson(L, 1), &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    // weather.random(minSeconds, maxSeconds [, transitionSeconds]) / weather.random(false)
    vm.registerFunction("weather", "random", [this, start](lua_State* L) {
        if (lua_isboolean(L, 1) && !lua_toboolean(L, 1)) {
            m_weather.randomWeather(false);
            return 0;
        }
        start();
        const double lo = lua_isnoneornil(L, 1) ? 120.0 : luaL_checknumber(L, 1);
        const double hi = lua_isnoneornil(L, 2) ? lo * 2.0 : luaL_checknumber(L, 2);
        const double tr = lua_isnoneornil(L, 3) ? 40.0 : luaL_checknumber(L, 3);
        m_weather.randomWeather(true, static_cast<float>(lo), static_cast<float>(hi), static_cast<float>(tr));
        return 0;
    });
    vm.registerFunction("weather", "wind", [this](lua_State* L) {
        const double d = luaL_checknumber(L, 1);
        if (!std::isfinite(d)) luaL_error(L, "weather.wind: a direction in degrees");
        m_weather.setWindDirection(static_cast<float>(d));
        return 0;
    });
    // weather.time(hour) sets the time of day by hand; weather.time(nil) follows the clock again
    vm.registerFunction("weather", "time", [this](lua_State* L) {
        if (lua_isnoneornil(L, 1)) {
            m_hourFromClock = true;
            return 0;
        }
        const double h = luaL_checknumber(L, 1);
        if (!std::isfinite(h)) luaL_error(L, "weather.time: an hour, 0 to 24");
        m_hourFromClock = false;
        m_weather.setTimeOfDay(static_cast<float>(h));
        return 0;
    });
    vm.registerFunction("weather", "ground", [this](lua_State* L) {
        m_weather.setGround(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_optnumber(L, 2, static_cast<double>(m_weather.state().snowCover))));
        return 0;
    });
    // weather.state() -> {clouds, rain, snow, rainFall, snowFall, fog, visibility, windSpeed, windVector, temperature, wetness, snowCover, flash, ...}
    vm.registerFunction("weather", "state", [this](lua_State* L) {
        const WeatherState& s = m_weather.state();
        lua_createtable(L, 0, 24);
        const struct { const char* key; float v; } numbers[] = {
            { "clouds", s.clouds }, { "rain", s.rain }, { "snow", s.snow }, { "fog", s.fog }, { "wind", s.wind }, { "gust", s.gust },
            { "temperature", s.temperature }, { "lightning", s.lightning }, { "sunlight", s.sunlight }, { "ambient", s.ambient },
            { "saturation", s.saturation }, { "rainFall", s.rainFall }, { "snowFall", s.snowFall }, { "fogDensity", s.fogDensity },
            { "windSpeed", s.windSpeed }, { "windDirection", s.windDirection }, { "wetness", s.wetness }, { "snowCover", s.snowCover },
            { "flash", s.flash }, { "visibility", s.visibility },
        };
        for (const auto& n : numbers) {
            lua_pushnumber(L, static_cast<double>(n.v));
            lua_setfield(L, -2, n.key);
        }
        ScriptVM::pushVec3(L, s.windVector);
        lua_setfield(L, -2, "windVector");
        lua_pushstring(L, m_weather.current().c_str());
        lua_setfield(L, -2, "weather");
        lua_pushboolean(L, m_weather.changing());
        lua_setfield(L, -2, "changing");
        return 1;
    });
    vm.registerFunction("weather", "particles", [this](lua_State* L) {
        m_particles = lua_toboolean(L, 1) != 0;
        if (!m_particles) stopParticles();
        return 0;
    });
    vm.registerFunction("weather", "lighting", [this](lua_State* L) {
        m_lightingOn = lua_toboolean(L, 1) != 0;
        if (!m_lightingOn) restoreLighting();
        return 0;
    });
    // weather.stop(): the effects stop, the lighting goes back; weather.set starts it again
    vm.registerFunction("weather", "stop", [this](lua_State*) {
        if (!m_active) return 0;
        stopParticles();
        restoreLighting();
        m_active = false;
        m_owner.clear();
        return 0;
    });
}

} // namespace kke
