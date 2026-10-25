#include "kke/ScriptClock.h"

#include "kke/Application.h"
#include "kke/Mood.h"
#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kke {

void ScriptClock::light(bool forceSky) {
    if (!m_app || !m_active) return;
    const DayNight::Look look = m_clock.look();
    const bool sky = forceSky || (m_sinceSky >= skyInterval && DayNight::skyChanged(look.sky, m_lastSky));
    DayNight::apply(look, m_app->lighting(), sky);
    if (sky) {
        m_lastSky = look.sky;
        m_sinceSky = 0.0f;
        ++m_skyUpdates;
    }
}

void ScriptClock::update(float dt, ScriptVM& vm) {
    if (!m_active) return;
    // Someone chose a mood (mood.set, a scene): that look wins.
    if (m_app && m_app->mood().name != m_mood) {
        m_active = false;
        m_clock.running = false;
        return;
    }
    m_sinceSky += dt;
    for (const DayNight::Crossed& c : m_clock.advance(dt)) {
        const char* name = c.event == DayNight::Event::Dawn ? "Dawn" : c.event == DayNight::Event::Dusk ? "Dusk" : "Hour";
        const int hour = c.hour;
        vm.callHookWith(name, [hour](lua_State* L) {
            lua_newtable(L);
            lua_pushinteger(L, hour);
            lua_setfield(L, -2, "hour");
            return 1;
        });
    }
    light(false);
}

void ScriptClock::release(const std::string& source) {
    if (m_active && m_owner == source) {
        m_active = false;
        m_clock.running = false;
    }
}

void ScriptClock::bind(ScriptVM& vm) {
    auto begin = [this, &vm]() {
        m_active = true;
        m_owner = vm.currentSource();
        m_mood = m_app ? m_app->mood().name : std::string();
        m_sinceSky = 0.0f;
    };
    auto hourArg = [](lua_State* L, int idx, const char* what) {
        const double h = luaL_checknumber(L, idx);
        if (!std::isfinite(h)) luaL_error(L, "%s: the hour must be a number (0-24)", what);
        const float w = std::fmod(float(h), 24.0f);
        return w < 0.0f ? w + 24.0f : w;
    };
    // clock.start{hour, dayLength, noon, sunrise, sunset}: time passes, the sun moves
    vm.registerFunction("clock", "start", [this, begin, hourArg](lua_State* L) {
        if (lua_istable(L, 1)) {
            lua_getfield(L, 1, "hour");
            if (!lua_isnil(L, -1)) m_clock.hour = hourArg(L, -1, "clock.start");
            lua_pop(L, 1);
            m_clock.dayLengthSeconds = std::clamp(ScriptVM::fieldNumber(L, 1, "dayLength", m_clock.dayLengthSeconds), 1.0f, 864000.0f);
            m_clock.noonElevation = std::clamp(ScriptVM::fieldNumber(L, 1, "noon", m_clock.noonElevation), 5.0f, 90.0f);
            m_clock.sunrise = std::clamp(ScriptVM::fieldNumber(L, 1, "sunrise", m_clock.sunrise), 0.0f, 23.0f);
            m_clock.sunset = std::clamp(ScriptVM::fieldNumber(L, 1, "sunset", m_clock.sunset), m_clock.sunrise + 1.0f, 24.0f);
        }
        m_clock.running = true;
        begin();
        light(true);
        return 0;
    });
    // clock.set(hour): jump there (frozen unless started)
    vm.registerFunction("clock", "set", [this, begin, hourArg](lua_State* L) {
        m_clock.hour = hourArg(L, 1, "clock.set");
        if (!m_active) begin();
        light(true);
        return 0;
    });
    // clock.pause() / clock.resume(): time stands still, the look stays
    vm.registerFunction("clock", "pause", [this](lua_State*) {
        m_clock.running = false;
        return 0;
    });
    vm.registerFunction("clock", "resume", [this, begin](lua_State*) {
        m_clock.running = true;
        if (!m_active) {
            begin();
            light(true);
        }
        return 0;
    });
    // clock.stop(): the clock lets go of the lighting (it stays as it was)
    vm.registerFunction("clock", "stop", [this](lua_State*) {
        m_clock.running = false;
        m_active = false;
        return 0;
    });
    vm.registerFunction("clock", "hour", [this](lua_State* L) {
        lua_pushnumber(L, m_clock.hour);
        return 1;
    });
    // clock.time() -> "07:30"
    vm.registerFunction("clock", "time", [this](lua_State* L) {
        const int minutes = int(std::floor(m_clock.hour * 60.0f)) % (24 * 60);
        char text[8];
        std::snprintf(text, sizeof text, "%02d:%02d", minutes / 60, minutes % 60);
        lua_pushstring(L, text);
        return 1;
    });
    vm.registerFunction("clock", "isDay", [this](lua_State* L) {
        lua_pushboolean(L, m_clock.isDay());
        return 1;
    });
    vm.registerFunction("clock", "running", [this](lua_State* L) {
        lua_pushboolean(L, m_clock.running && m_active);
        return 1;
    });
    // clock.sun() -> direction towards the sun, elevation in degrees
    vm.registerFunction("clock", "sun", [this](lua_State* L) {
        const DayNight::Look l = m_clock.look();
        ScriptVM::pushVec3(L, l.towardsSun);
        lua_pushnumber(L, l.sunElevation);
        return 2;
    });
    // clock.dayLength(seconds): how long a whole day takes
    vm.registerFunction("clock", "dayLength", [this](lua_State* L) {
        if (lua_gettop(L) >= 1) m_clock.dayLengthSeconds = std::clamp(float(luaL_checknumber(L, 1)), 1.0f, 864000.0f);
        lua_pushnumber(L, m_clock.dayLengthSeconds);
        return 1;
    });
}

} // namespace kke
