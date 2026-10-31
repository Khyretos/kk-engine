// profile.*: zones and captures for the profiler (kke/Profiler.h, docs/PROFILING.md).
#include "kke/Profiler.h"
#include "kke/modules/ScriptModule.h"

#include <lauxlib.h>
#include <lua.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kke {

void ScriptModule::bindProfile() {
    ScriptVM& vm = *m_vm;

    // profile.capture([seconds [, path]]): records zones, frame times, GPU
    // time and memory for `seconds` (5 by default), then writes a trace
    // Perfetto opens. Returns the file's path, or nil and why not.
    vm.registerFunction("profile", "capture", [](lua_State* L) {
        const double seconds = luaL_optnumber(L, 1, 5.0);
        if (seconds <= 0.0 || seconds > 600.0) return luaL_error(L, "profile.capture: seconds %f is outside 0..600", seconds);
        Profiler::CaptureOptions options;
        options.seconds = seconds;
        options.path = lua_isstring(L, 2) ? lua_tostring(L, 2) : Profiler::newCapturePath();
        std::string error;
        if (!Profiler::get().startCapture(options, &error)) {
            lua_pushnil(L);
            lua_pushstring(L, error.c_str());
            return 2;
        }
        lua_pushstring(L, options.path.c_str());
        return 1;
    });

    // profile.stop(): ends a capture now and saves it. Returns the path, or
    // nil and why not.
    vm.registerFunction("profile", "stop", [](lua_State* L) {
        std::string error;
        if (!Profiler::get().stopCapture(&error)) {
            lua_pushnil(L);
            lua_pushstring(L, error.c_str());
            return 2;
        }
        const std::string path = Profiler::get().lastCapturePath();
        if (path.empty()) lua_pushnil(L);
        else lua_pushstring(L, path.c_str());
        return 1;
    });

    // profile.active(): is a capture running.
    vm.registerFunction("profile", "active", [](lua_State* L) {
        lua_pushboolean(L, Profiler::get().capturing());
        return 1;
    });

    // profile.push(name) ... profile.pop(): a zone round some of your script,
    // shown in a capture next to the engine's own.
    auto stack = std::make_shared<std::vector<std::pair<uint32_t, double>>>();
    vm.registerFunction("profile", "push", [stack](lua_State* L) {
        const char* name = luaL_checkstring(L, 1);
        if (stack->size() >= 64) return luaL_error(L, "profile.push: more than 64 zones open (a missing profile.pop?)");
        Profiler& p = Profiler::get();
        stack->emplace_back(p.intern(std::string("lua: ") + name), p.capturing() ? p.nowUs() : -1.0);
        return 0;
    });
    vm.registerFunction("profile", "pop", [stack](lua_State* L) {
        if (stack->empty()) return luaL_error(L, "profile.pop: no profile.push is open");
        const auto [id, start] = stack->back();
        stack->pop_back();
        Profiler& p = Profiler::get();
        if (start >= 0.0) p.zone(id, start, p.nowUs() - start);
        return 0;
    });

    // profile.modules([limit]): the slowest modules' CPU time over the last
    // 120 frames, slowest first: { {name="Physics.update", avg=1.2, max=3.4, last=1.1}, ... } (ms).
    vm.registerFunction("profile", "modules", [](lua_State* L) {
        const int limit = static_cast<int>(luaL_optinteger(L, 1, 8));
        const auto stats = Profiler::get().moduleStats(static_cast<size_t>(std::max(1, std::min(limit, 64))));
        lua_createtable(L, static_cast<int>(stats.size()), 0);
        int i = 1;
        for (const Profiler::ModuleStat& s : stats) {
            lua_createtable(L, 0, 4);
            lua_pushstring(L, s.name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushnumber(L, s.avgMs);
            lua_setfield(L, -2, "avg");
            lua_pushnumber(L, s.maxMs);
            lua_setfield(L, -2, "max");
            lua_pushnumber(L, s.lastMs);
            lua_setfield(L, -2, "last");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    // profile.frame(): the frame time over the last 120 frames, in ms:
    // avg, worst.
    vm.registerFunction("profile", "frame", [](lua_State* L) {
        lua_pushnumber(L, Profiler::get().averageFrameMs());
        lua_pushnumber(L, Profiler::get().worstFrameMs());
        return 2;
    });
}

} // namespace kke
