#pragma once

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct lua_State;

namespace kke {

// One sandboxed Lua 5.4 state for gameplay scripts, Garry's Mod style
// (docs/SCRIPTING.md): scripts register callbacks with hook.Add, schedule with
// timer.Simple / timer.Create, and call whatever the engine binds
// (physics.*, audio.*, input.*, ...). Pure logic, no GPU: tested in
// tests/test_script_vm.cpp. ScriptModule puts one in a game.
//
// Robust first, because creators will write broken scripts:
// - A failing hook is reported (file:line + traceback) and skipped; the
//   other hooks, the other scripts and the game keep running.
// - Runaway scripts (an infinite loop) are stopped after a per-call
//   instruction budget, for good: every call into Lua runs on its own
//   coroutine, and the budget check yields it away, past any pcall the
//   script wraps around its loop. The script is then unloaded (its hooks
//   and timers removed) until it's fixed and reloaded.
// - Each script has its own globals (a per-script _ENV); engine and
//   library tables are read-only views. Scripts share on purpose through
//   the `shared` table and hook.Run. (isolateScripts = false: GMod-style
//   shared globals.)
// - Memory is capped (default 64 MB); an allocation over the cap is a
//   normal Lua "not enough memory" error, not a crash.
// - The sandbox has no file, OS, process, debug or module-loading access:
//   base (minus dofile/loadfile/load), string (minus dump), table, math,
//   utf8, coroutine. Everything else comes from engine bindings.
// - Every hook and timer remembers the script that made it, so
//   reloadFile() replaces one script's behaviour without restarting.
//
// Lua is compiled as C++ here, so a Lua error unwinds C++ frames
// properly (no longjmp over destructors) inside bound functions.
class ScriptVM {
public:
    struct Limits {
        size_t memoryBytes = 64u * 1024u * 1024u;
        uint64_t instructionsPerCall = 20'000'000; // ~0.05-0.3 s of Lua on one core
        bool isolateScripts = true;                // per-script globals (see above)
    };
    struct Error {
        std::string source;   // script path, "console", or "engine"
        std::string message;  // with file:line and a traceback when there is one
    };

    ScriptVM();
    explicit ScriptVM(const Limits& limits);
    ~ScriptVM();
    ScriptVM(const ScriptVM&) = delete;
    ScriptVM& operator=(const ScriptVM&) = delete;

    lua_State* state() { return m_L; }

    // Runs a file / a string as its own script `source`. False on a syntax
    // or runtime error (also recorded in errors()).
    bool runFile(const std::string& path);
    bool runString(const std::string& code, const std::string& source = "console");
    // Removes everything `path` registered (hooks, timers, and anything an
    // onUnload listener cleans up), then runs it again.
    bool reloadFile(const std::string& path);
    void unload(const std::string& source);

    // hook.Run(event, ...) from C++. Numbers, strings and bools as args.
    template <typename... Args>
    void callHook(const std::string& event, const Args&... args) {
        beginHook(event);
        (pushArg(args), ...);
        endHook(int(sizeof...(Args)));
    }
    // For args that need building (tables): push them in `push`, return the count.
    void callHookWith(const std::string& event, const std::function<int(lua_State*)>& push);
    size_t hookCount(const std::string& event) const;

    // Fires due timers. `now` in seconds, monotonic.
    void updateTimers(double now);
    double now() const { return m_now; }

    // Binds `table.name` (table created on first use) to a C++ function.
    // The function gets the lua_State and returns how many values it pushed.
    using Fn = std::function<int(lua_State*)>;
    void registerFunction(const std::string& table, const std::string& name, Fn fn);
    // Called with the source whenever a script is unloaded (before a
    // reload, or unload()): engine bindings free what that script spawned.
    std::function<void(const std::string& source)> onUnload;
    // Which script is running right now ("" outside any script).
    const std::string& currentSource() const { return m_current; }
    // Pushes `source`'s global environment (its _ENV table) onto the stack;
    // the real global table when scripts aren't isolated.
    void pushEnv(const std::string& source);
    // Called when a runaway script was stopped and unloaded.
    std::function<void(const std::string& source)> onStopped;
    // Scripts stopped that way and not reloaded since.
    const std::vector<std::string>& stoppedScripts() const { return m_stopped; }
    // Wall time spent running each script's code, in seconds, since start
    // (the Scripts panel shows the per-frame difference).
    double cpuSeconds(const std::string& source) const;
    const std::map<std::string, double>& cpuTimes() const { return m_cpuSeconds; }

    // Callbacks kept by bindings (ui.onClick, ...): ref() keeps the value at
    // `index` alive and returns a handle (0 for nil), unref() lets it go.
    // (Use the lua_State the binding was given: script code runs on
    // coroutines, not on state().) callRef() calls such a function as script `source` (its budget, its
    // errors, its CPU time), with the arguments `push` pushes.
    int ref(lua_State* L, int index);
    void unref(int ref);
    bool callRef(int ref, const std::string& source, const std::function<int(lua_State*)>& push = {});

    // A Lua value as bytes and back (net messages): nil, booleans, numbers,
    // strings and tables of those, nested up to 16 deep, at most `maxBytes`.
    // False with a reason for anything else (functions, cycles, too big),
    // or for bytes that aren't a valid encoding (they came off the network).
    static bool encodeValue(lua_State* L, int index, std::string& out, std::string& error, size_t maxBytes = 1024);
    static bool decodeValue(lua_State* L, const std::string& bytes, std::string& error);

    // print() and log output.
    std::function<void(const std::string& source, const std::string& text)> printSink;

    const std::vector<Error>& errors() const { return m_errors; }
    void clearErrors() { m_errors.clear(); }
    size_t memoryUsed() const { return m_memUsed; }
    const Limits& limits() const { return m_limits; }

    // Vec(x, y, z) tables with + - * and :length() etc. (defined by the
    // bootstrap); these read/write them from C++. toVec3 also accepts {x,y,z}.
    static void pushVec3(lua_State* L, const glm::vec3& v);
    static glm::vec3 toVec3(lua_State* L, int index, const glm::vec3& fallback = glm::vec3(0.0f));
    // Field helpers for option tables: spawn{pos=..., size=...}.
    static float fieldNumber(lua_State* L, int table, const char* key, float fallback);
    static bool fieldBool(lua_State* L, int table, const char* key, bool fallback);
    static glm::vec3 fieldVec3(lua_State* L, int table, const char* key, const glm::vec3& fallback);
    static std::string fieldString(lua_State* L, int table, const char* key, const std::string& fallback);

    // Used by the internals; public for the C trampolines.
    void reportError(const std::string& source, const std::string& message);
    void resetBudget();
    bool chargeBudget(uint64_t n) { m_budgetUsed += n; return m_budgetUsed <= m_limits.instructionsPerCall; }
    bool stopping() const { return m_stopping; }
    void beginStop() { m_stopping = true; }
    void switchSource(const std::string& source);
    size_t& memUsedRef() { return m_memUsed; }

private:
    bool runChunk(const std::string& code, const std::string& chunkName, const std::string& source);
    bool resume(int nargs, int nresults, const std::string& source);
    void stopScript(const std::string& source);
    void beginHook(const std::string& event);
    void endHook(int nargs);
    void pushArg(double v);
    void pushArg(float v) { pushArg(double(v)); }
    // Whole numbers go in as Lua integers: an id passed to a hook ("Break")
    // must satisfy the bindings' integer checks (breakable.pieces(id)).
    void pushArg(int v) { pushInteger(v); }
    void pushArg(uint32_t v) { pushInteger(static_cast<int64_t>(v)); }
    void pushArg(uint64_t v) { pushInteger(static_cast<int64_t>(v)); }
    void pushInteger(int64_t v);
    void pushArg(bool v);
    void pushArg(const std::string& v);
    void pushArg(const char* v) { pushArg(std::string(v)); }

    Limits m_limits;
    lua_State* m_L = nullptr;
    size_t m_memUsed = 0;
    uint64_t m_budgetUsed = 0;
    double m_now = 0.0;
    bool m_stopping = false;
    std::string m_current;
    std::chrono::steady_clock::time_point m_switchedAt = std::chrono::steady_clock::now();
    std::map<std::string, double> m_cpuSeconds;
    std::vector<std::string> m_stopped;
    std::vector<Error> m_errors;
    std::deque<Fn> m_functions; // stable addresses: Lua holds pointers to these
    int m_runHookRef = -1, m_timersRef = -1, m_unloadRef = -1, m_envRef = -1;
};

} // namespace kke
