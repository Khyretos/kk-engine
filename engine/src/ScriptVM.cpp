#include "kke/ScriptVM.h"

// Not <lua.hpp>: that wraps the headers in extern "C", and our Lua is
// compiled as C++ (see CMakeLists.txt), so its symbols are C++ ones.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace kke {

namespace {

// The Lua half of the scripting layer: hook, timer, Vec. Kept in Lua
// because it's easier to read and change there, and it's what a Garry's
// Mod scripter would expect to find (GMod's own hook.lua / timer.lua are
// Lua too). It receives its privileged helpers as arguments and keeps
// them as locals, so scripts can't reach them.
const char* kBootstrap = R"LUA(
local report, budgetReset, traceback, setCurrent, getCurrent = ...
local hooks = {}   -- event -> array of { name, fn, source }
local timers = {}  -- key -> { at, interval, reps, fn, source, name }
local now = 0

local function call(source, fn, ...)
  local prev = getCurrent()
  setCurrent(source)
  budgetReset()
  local r = table.pack(xpcall(fn, traceback, ...))
  setCurrent(prev)
  if not r[1] then report(source, tostring(r[2])) return nil end
  return table.unpack(r, 2, r.n)
end

hook = {}
function hook.Add(event, name, fn)
  assert(type(event) == "string", "hook.Add: event must be a string")
  assert(type(fn) == "function", "hook.Add: fn must be a function")
  local list = hooks[event] or {}
  hooks[event] = list
  for i, h in ipairs(list) do
    if h.name == name then list[i] = { name = name, fn = fn, source = getCurrent() } return end
  end
  list[#list + 1] = { name = name, fn = fn, source = getCurrent() }
end
function hook.Remove(event, name)
  local list = hooks[event]
  if not list then return end
  for i, h in ipairs(list) do if h.name == name then table.remove(list, i) return end end
end
function hook.GetTable()
  local t = {}
  for e, list in pairs(hooks) do
    t[e] = {}
    for _, h in ipairs(list) do t[e][h.name] = h.fn end
  end
  return t
end
-- Like GMod: the first hook that returns something stops the rest and
-- that value is the result.
function hook.Run(event, ...)
  local list = hooks[event]
  if not list then return nil end
  local copy = table.move(list, 1, #list, 1, {})
  for _, h in ipairs(copy) do
    local a, b, c = call(h.source, h.fn, ...)
    if a ~= nil then return a, b, c end
  end
end

timer = {}
function timer.Simple(delay, fn)
  timers[{}] = { at = now + (tonumber(delay) or 0), interval = 0, reps = 1, fn = fn, source = getCurrent() }
end
function timer.Create(name, delay, reps, fn)
  assert(type(fn) == "function", "timer.Create: fn must be a function")
  delay = tonumber(delay) or 0
  timers[name] = { at = now + delay, interval = delay, reps = tonumber(reps) or 1, fn = fn, source = getCurrent(), name = name }
end
function timer.Remove(name) timers[name] = nil end
function timer.Exists(name) return timers[name] ~= nil end

local function tick(t)
  now = t
  local due = {}
  for k, tm in pairs(timers) do if tm.at <= now then due[#due + 1] = k end end
  table.sort(due, function(a, b) return timers[a].at < timers[b].at end)
  for _, k in ipairs(due) do
    local tm = timers[k]
    if tm then
      if tm.reps == 1 then timers[k] = nil
      else
        if tm.reps > 1 then tm.reps = tm.reps - 1 end
        tm.at = tm.at + math.max(tm.interval, 0.001)
        if tm.at < now then tm.at = now + math.max(tm.interval, 0.001) end
      end
      call(tm.source, tm.fn)
    end
  end
end

local function unload(source)
  for _, list in pairs(hooks) do
    for i = #list, 1, -1 do if list[i].source == source then table.remove(list, i) end end
  end
  for k, tm in pairs(timers) do if tm.source == source then timers[k] = nil end end
end

local function count(event)
  local list = hooks[event]
  return list and #list or 0
end

-- Vec(x, y, z): the vector type every binding takes and returns.
local V = {}
V.__index = V
function Vec(x, y, z) return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, V) end
V.__add = function(a, b) return Vec(a.x + b.x, a.y + b.y, a.z + b.z) end
V.__sub = function(a, b) return Vec(a.x - b.x, a.y - b.y, a.z - b.z) end
V.__unm = function(a) return Vec(-a.x, -a.y, -a.z) end
V.__mul = function(a, b)
  if type(a) == "number" then return Vec(a * b.x, a * b.y, a * b.z) end
  if type(b) == "number" then return Vec(a.x * b, a.y * b, a.z * b) end
  return Vec(a.x * b.x, a.y * b.y, a.z * b.z)
end
V.__div = function(a, b) return Vec(a.x / b, a.y / b, a.z / b) end
V.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
V.__tostring = function(a) return string.format("Vec(%.3f, %.3f, %.3f)", a.x, a.y, a.z) end
function V:length() return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end
function V:normalized() local l = self:length() if l < 1e-9 then return Vec(0, 0, 0) end return self / l end
function V:dot(o) return self.x * o.x + self.y * o.y + self.z * o.z end
function V:cross(o) return Vec(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x) end

return hook.Run, tick, unload, V, count
)LUA";

ScriptVM* vmOf(lua_State* L) { return *static_cast<ScriptVM**>(lua_getextraspace(L)); }

void* allocate(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* vm = static_cast<ScriptVM*>(ud);
    size_t& used = vm->memUsedRef();
    const size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        std::free(ptr);
        used -= old;
        return nullptr;
    }
    if (nsize > old && used - old + nsize > vm->limits().memoryBytes) return nullptr; // Lua raises "not enough memory"
    void* p = std::realloc(ptr, nsize);
    if (p) used = used - old + nsize;
    return p;
}

bool g_aborting = false; // per process is fine: one VM steps at a time on the game thread

void countHook(lua_State* L, lua_Debug*) {
    ScriptVM* vm = vmOf(L);
    if (!vm->chargeBudget(1000) || g_aborting) {
        g_aborting = true;
        luaL_error(L, "script ran too long (an endless loop?) and was stopped after %I instructions",
                   (lua_Integer)vm->limits().instructionsPerCall);
    }
}

int luaReport(lua_State* L) {
    vmOf(L)->reportError(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    return 0;
}
int luaBudgetReset(lua_State* L) {
    vmOf(L)->resetBudget();
    g_aborting = false;
    return 0;
}
int luaSetCurrent(lua_State* L) {
    // The upvalue points at ScriptVM::m_current.
    auto* cur = static_cast<std::string*>(lua_touserdata(L, lua_upvalueindex(1)));
    *cur = luaL_optstring(L, 1, "");
    return 0;
}
int luaGetCurrent(lua_State* L) {
    auto* cur = static_cast<std::string*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushstring(L, cur->c_str());
    return 1;
}
int luaPrint(lua_State* L) {
    const int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += '\t';
        out.append(s, len);
        lua_pop(L, 1);
    }
    ScriptVM* vm = vmOf(L);
    if (vm->printSink) vm->printSink(vm->currentSource(), out);
    return 0;
}
int trampoline(lua_State* L) {
    auto* fn = static_cast<ScriptVM::Fn*>(lua_touserdata(L, lua_upvalueindex(1)));
    return (*fn)(L);
}

} // namespace

ScriptVM::ScriptVM() : ScriptVM(Limits{}) {}

ScriptVM::ScriptVM(const Limits& limits) : m_limits(limits) {
    m_L = lua_newstate(allocate, this);
    if (!m_L) return;
    *static_cast<ScriptVM**>(lua_getextraspace(m_L)) = this;

    // The safe standard libraries only.
    const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},       {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
                             {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine}};
    for (const luaL_Reg& l : libs) {
        luaL_requiref(m_L, l.name, l.func, 1);
        lua_pop(m_L, 1);
    }
    for (const char* name : {"dofile", "loadfile", "load", "require"}) {
        lua_pushnil(m_L);
        lua_setglobal(m_L, name);
    }
    lua_getglobal(m_L, "string");
    lua_pushnil(m_L);
    lua_setfield(m_L, -2, "dump");
    lua_pop(m_L, 1);
    lua_pushcfunction(m_L, luaPrint);
    lua_setglobal(m_L, "print");
    // debug.traceback only, kept in the registry; the debug library itself
    // never becomes a global.
    luaL_requiref(m_L, "debug", luaopen_debug, 0);
    lua_getfield(m_L, -1, "traceback");
    lua_setfield(m_L, LUA_REGISTRYINDEX, "kke.traceback");
    lua_pop(m_L, 1);

    lua_sethook(m_L, countHook, LUA_MASKCOUNT, 1000);

    if (luaL_loadbufferx(m_L, kBootstrap, std::strlen(kBootstrap), "=kke_bootstrap", "t") != LUA_OK) {
        reportError("engine", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return;
    }
    lua_pushcfunction(m_L, luaReport);
    lua_pushcfunction(m_L, luaBudgetReset);
    lua_getfield(m_L, LUA_REGISTRYINDEX, "kke.traceback");
    lua_pushlightuserdata(m_L, &m_current);
    lua_pushcclosure(m_L, luaSetCurrent, 1);
    lua_pushlightuserdata(m_L, &m_current);
    lua_pushcclosure(m_L, luaGetCurrent, 1);
    if (lua_pcall(m_L, 5, 5, 0) != LUA_OK) {
        reportError("engine", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return;
    }
    // Stack: runHook, tick, unload, V, count
    lua_setfield(m_L, LUA_REGISTRYINDEX, "kke.hookCount");
    lua_setfield(m_L, LUA_REGISTRYINDEX, "kke.Vec");
    m_unloadRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
    m_timersRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
    m_runHookRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
}

ScriptVM::~ScriptVM() {
    if (m_L) lua_close(m_L);
}

void ScriptVM::reportError(const std::string& source, const std::string& message) {
    if (m_errors.size() >= 200) m_errors.erase(m_errors.begin()); // a hook failing every frame mustn't grow forever
    m_errors.push_back({source, message});
    if (printSink) printSink(source, "ERROR: " + message);
}

bool ScriptVM::runChunk(const std::string& code, const std::string& chunkName, const std::string& source) {
    if (!m_L) return false;
    const std::string prev = m_current;
    m_current = source;
    resetBudget();
    g_aborting = false;
    lua_getfield(m_L, LUA_REGISTRYINDEX, "kke.traceback");
    const int handler = lua_gettop(m_L);
    bool ok = true;
    // "t": text only. Precompiled bytecode can crash the VM on purpose.
    if (luaL_loadbufferx(m_L, code.data(), code.size(), ("@" + chunkName).c_str(), "t") != LUA_OK) {
        reportError(source, lua_tostring(m_L, -1));
        ok = false;
    } else if (lua_pcall(m_L, 0, 0, handler) != LUA_OK) {
        reportError(source, lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "(error object is not a string)");
        ok = false;
    }
    lua_settop(m_L, handler - 1);
    m_current = prev;
    return ok;
}

bool ScriptVM::runFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        reportError(path, "cannot open file");
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return runChunk(ss.str(), path, path);
}

bool ScriptVM::runString(const std::string& code, const std::string& source) { return runChunk(code, source, source); }

void ScriptVM::unload(const std::string& source) {
    if (!m_L || m_unloadRef < 0) return;
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_unloadRef);
    lua_pushstring(m_L, source.c_str());
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK) {
        reportError("engine", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
    if (onUnload) onUnload(source);
}

bool ScriptVM::reloadFile(const std::string& path) {
    unload(path);
    return runFile(path);
}

void ScriptVM::beginHook(const std::string& event) {
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_runHookRef);
    lua_pushstring(m_L, event.c_str());
}

void ScriptVM::endHook(int nargs) {
    if (lua_pcall(m_L, nargs + 1, 0, 0) != LUA_OK) {
        reportError("engine", lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "hook.Run failed");
        lua_pop(m_L, 1);
    }
}

void ScriptVM::callHookWith(const std::string& event, const std::function<int(lua_State*)>& push) {
    if (!m_L || m_runHookRef < 0) return;
    beginHook(event);
    endHook(push(m_L));
}

size_t ScriptVM::hookCount(const std::string& event) const {
    if (!m_L) return 0;
    lua_getfield(m_L, LUA_REGISTRYINDEX, "kke.hookCount");
    lua_pushstring(m_L, event.c_str());
    size_t n = 0;
    if (lua_pcall(m_L, 1, 1, 0) == LUA_OK) n = size_t(lua_tointeger(m_L, -1));
    lua_pop(m_L, 1);
    return n;
}

void ScriptVM::pushArg(double v) { lua_pushnumber(m_L, v); }
void ScriptVM::pushArg(bool v) { lua_pushboolean(m_L, v); }
void ScriptVM::pushArg(const std::string& v) { lua_pushlstring(m_L, v.data(), v.size()); }

void ScriptVM::updateTimers(double now) {
    m_now = now;
    if (!m_L || m_timersRef < 0) return;
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_timersRef);
    lua_pushnumber(m_L, now);
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK) {
        reportError("engine", lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}

void ScriptVM::registerFunction(const std::string& table, const std::string& name, Fn fn) {
    if (!m_L) return;
    m_functions.push_back(std::move(fn));
    lua_getglobal(m_L, table.c_str());
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        lua_newtable(m_L);
        lua_pushvalue(m_L, -1);
        lua_setglobal(m_L, table.c_str());
    }
    lua_pushlightuserdata(m_L, &m_functions.back());
    lua_pushcclosure(m_L, trampoline, 1);
    lua_setfield(m_L, -2, name.c_str());
    lua_pop(m_L, 1);
}

void ScriptVM::pushVec3(lua_State* L, const glm::vec3& v) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, v.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z);
    lua_setfield(L, -2, "z");
    lua_getfield(L, LUA_REGISTRYINDEX, "kke.Vec");
    lua_setmetatable(L, -2);
}

glm::vec3 ScriptVM::toVec3(lua_State* L, int index, const glm::vec3& fallback) {
    index = lua_absindex(L, index);
    if (!lua_istable(L, index)) return fallback;
    glm::vec3 v = fallback;
    const char* named[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        lua_getfield(L, index, named[i]);
        if (lua_isnumber(L, -1)) v[i] = float(lua_tonumber(L, -1));
        else {
            lua_pop(L, 1);
            lua_rawgeti(L, index, i + 1);
            if (lua_isnumber(L, -1)) v[i] = float(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
    }
    return v;
}

float ScriptVM::fieldNumber(lua_State* L, int table, const char* key, float fallback) {
    if (!lua_istable(L, table)) return fallback;
    lua_getfield(L, table, key);
    const float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : fallback;
    lua_pop(L, 1);
    return v;
}

bool ScriptVM::fieldBool(lua_State* L, int table, const char* key, bool fallback) {
    if (!lua_istable(L, table)) return fallback;
    lua_getfield(L, table, key);
    const bool v = lua_isnil(L, -1) ? fallback : bool(lua_toboolean(L, -1));
    lua_pop(L, 1);
    return v;
}

glm::vec3 ScriptVM::fieldVec3(lua_State* L, int table, const char* key, const glm::vec3& fallback) {
    if (!lua_istable(L, table)) return fallback;
    table = lua_absindex(L, table);
    lua_getfield(L, table, key);
    const glm::vec3 v = lua_isnumber(L, -1) ? glm::vec3(float(lua_tonumber(L, -1))) : toVec3(L, -1, fallback);
    lua_pop(L, 1);
    return v;
}

std::string ScriptVM::fieldString(lua_State* L, int table, const char* key, const std::string& fallback) {
    if (!lua_istable(L, table)) return fallback;
    lua_getfield(L, table, key);
    std::string v = lua_isstring(L, -1) ? std::string(lua_tostring(L, -1)) : fallback;
    lua_pop(L, 1);
    return v;
}

} // namespace kke
