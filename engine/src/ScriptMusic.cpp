#include "kke/ScriptMusic.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kke {

using nlohmann::json;

namespace {

std::string inside(const std::string& folder, const std::string& relative) {
    namespace fs = std::filesystem;
    if (folder.empty() || relative.empty()) return {};
    const fs::path rel(relative);
    if (rel.is_absolute()) return {};
    for (const auto& part : rel)
        if (part == "..") return {};
    return (fs::path(folder) / rel).string();
}

} // namespace

void ScriptMusic::attach(AudioMixer* mixer, std::function<SoundHandle(const std::string&)> loader, std::string folder) {
    if (!mixer) return;
    m_backend = std::make_unique<MixerMusicBackend>(*mixer, std::move(loader));
    m_director.setBackend(m_backend.get());
    m_folder = std::move(folder);
}

void ScriptMusic::release(const std::string& source) {
    if (!m_owner.empty() && m_owner == source) {
        m_director.clear();
        m_owner.clear();
    }
}

void ScriptMusic::update(float dt, ScriptVM& vm) {
    if (!m_backend) return;
    m_director.update(dt);
    for (const MusicDirector::Event& e : m_director.takeEvents()) {
        const char* hook = "MusicBeat";
        switch (e.kind) {
        case MusicDirector::Event::Kind::Beat: hook = "MusicBeat"; break;
        case MusicDirector::Event::Kind::Bar: hook = "MusicBar"; break;
        case MusicDirector::Event::Kind::Segment: hook = "MusicSegment"; break;
        case MusicDirector::Event::Kind::Ended: hook = "MusicEnded"; break;
        case MusicDirector::Event::Kind::Stinger: hook = "MusicStinger"; break;
        }
        vm.callHookWith(hook, [&e](lua_State* L) {
            lua_createtable(L, 0, 4);
            lua_pushstring(L, e.name.c_str());
            lua_setfield(L, -2, e.kind == MusicDirector::Event::Kind::Stinger ? "name" : "segment");
            if (e.kind == MusicDirector::Event::Kind::Beat || e.kind == MusicDirector::Event::Kind::Bar) {
                lua_pushinteger(L, e.beat);
                lua_setfield(L, -2, "beat");
                lua_pushinteger(L, e.bar);
                lua_setfield(L, -2, "bar");
            }
            return 1;
        });
    }
}

void ScriptMusic::bind(ScriptVM& vm) {
    auto fail = [](lua_State* L, const std::string& why) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, why.c_str());
        return 2;
    };
    // music.define{segments = {...}, transitions = {...}, stingers = {...}} -> true, or false and why
    vm.registerFunction("music", "define", [this, fail](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        std::string error;
        if (!m_director.fromJson(ScriptVM::toJson(L, 1), &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    // music.load("music/score.json"): the same from a file inside the game's folder
    vm.registerFunction("music", "load", [this, fail](lua_State* L) {
        const std::string path = inside(m_folder, luaL_checkstring(L, 1));
        if (path.empty()) return fail(L, "the file must be inside the game's folder");
        std::ifstream in(path, std::ios::binary);
        if (!in) return fail(L, path + ": can't open");
        std::stringstream ss;
        ss << in.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded()) return fail(L, path + ": not valid JSON");
        std::string error;
        if (!m_director.fromJson(j, &error)) return fail(L, path + ": " + error);
        lua_pushboolean(L, 1);
        return 1;
    });
    // music.play(segment) goes there by the transition rules; playNow(segment [, fadeIn]) cuts in
    vm.registerFunction("music", "play", [this, &vm, fail](lua_State* L) {
        std::string error;
        m_owner = vm.currentSource();
        if (!m_director.play(luaL_checkstring(L, 1), &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    vm.registerFunction("music", "playNow", [this, &vm, fail](lua_State* L) {
        std::string error;
        m_owner = vm.currentSource();
        const float fade = lua_isnoneornil(L, 2) ? 0.0f : static_cast<float>(luaL_checknumber(L, 2));
        if (!m_director.playNow(luaL_checkstring(L, 1), fade, &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    vm.registerFunction("music", "stop", [this](lua_State* L) {
        m_director.stop(lua_isnoneornil(L, 1) ? 1.0f : static_cast<float>(luaL_checknumber(L, 1)));
        return 0;
    });
    vm.registerFunction("music", "stinger", [this, fail](lua_State* L) {
        std::string error;
        if (!m_director.stinger(luaL_checkstring(L, 1), &error)) return fail(L, error);
        lua_pushboolean(L, 1);
        return 1;
    });
    // music.intensity([v]); music.param(name [, v])
    vm.registerFunction("music", "intensity", [this](lua_State* L) {
        if (lua_isnoneornil(L, 1)) {
            lua_pushnumber(L, static_cast<double>(m_director.intensity()));
            return 1;
        }
        const double v = luaL_checknumber(L, 1);
        if (!std::isfinite(v)) luaL_error(L, "music.intensity: a number");
        m_director.setIntensity(static_cast<float>(v));
        return 0;
    });
    vm.registerFunction("music", "param", [this](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        if (lua_isnoneornil(L, 2)) {
            lua_pushnumber(L, static_cast<double>(m_director.parameter(name)));
            return 1;
        }
        const double v = luaL_checknumber(L, 2);
        if (!std::isfinite(v)) luaL_error(L, "music.param: a number");
        m_director.setParameter(name, static_cast<float>(v));
        return 0;
    });
    vm.registerFunction("music", "current", [this](lua_State* L) {
        lua_pushstring(L, m_director.current().c_str());
        return 1;
    });
    vm.registerFunction("music", "pending", [this](lua_State* L) {
        lua_pushstring(L, m_director.pending().c_str());
        return 1;
    });
    vm.registerFunction("music", "playing", [this](lua_State* L) {
        lua_pushboolean(L, m_director.playing());
        return 1;
    });
    vm.registerFunction("music", "beat", [this](lua_State* L) {
        lua_pushnumber(L, static_cast<double>(m_director.beat()));
        return 1;
    });
    vm.registerFunction("music", "bar", [this](lua_State* L) {
        lua_pushinteger(L, m_director.bar());
        return 1;
    });
    vm.registerFunction("music", "position", [this](lua_State* L) {
        lua_pushnumber(L, m_director.position());
        return 1;
    });
}

} // namespace kke
