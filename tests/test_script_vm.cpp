#include "kke/ScriptVM.h"
#include "kke/modules/ScriptModule.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <cstdio>
#include <fstream>

using namespace kke;

namespace {
// A script's own global (scripts are isolated: each has its own _ENV).
double globalNumber(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const double v = lua_tonumber(vm.state(), -1);
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
std::string globalString(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    std::string v = lua_isstring(vm.state(), -1) ? lua_tostring(vm.state(), -1) : "";
    lua_pop(vm.state(), 2);
    return v;
}
bool isNil(ScriptVM& vm, const char* name) {
    lua_getglobal(vm.state(), name);
    const bool nil = lua_isnil(vm.state(), -1);
    lua_pop(vm.state(), 1);
    return nil;
}
std::string writeTemp(const std::string& name, const std::string& code) {
    const std::string path = ::testing::TempDir() + name;
    std::ofstream(path) << code;
    return path;
}
} // namespace

TEST(ScriptVM, HooksRunWithArgumentsInOrder) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(
        total = 0
        order = ""
        hook.Add("Think", "a", function(dt) total = total + dt; order = order .. "a" end)
        hook.Add("Think", "b", function(dt) total = total + dt * 10; order = order .. "b" end)
    )"));
    vm.callHook("Think", 0.5);
    EXPECT_DOUBLE_EQ(globalNumber(vm, "total"), 5.5);
    EXPECT_EQ(vm.hookCount("Think"), 2u);
    ASSERT_TRUE(vm.runString(R"(hook.Remove("Think", "a"))"));
    EXPECT_EQ(vm.hookCount("Think"), 1u);
}

TEST(ScriptVM, AFailingHookDoesNotStopTheOthers) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(
        ran = 0
        hook.Add("Tick", "bad", function() error("boom") end)
        hook.Add("Tick", "good", function() ran = ran + 1 end)
    )", "mygame.lua"));
    vm.callHook("Tick", 0.016, 1);
    vm.callHook("Tick", 0.016, 2);
    EXPECT_EQ(globalNumber(vm, "ran", "mygame.lua"), 2.0);
    ASSERT_EQ(vm.errors().size(), 2u);
    EXPECT_EQ(vm.errors()[0].source, "mygame.lua");
    EXPECT_NE(vm.errors()[0].message.find("boom"), std::string::npos);
    EXPECT_NE(vm.errors()[0].message.find("mygame.lua:3"), std::string::npos) << vm.errors()[0].message; // file:line
}

TEST(ScriptVM, SyntaxErrorIsReportedNotFatal) {
    ScriptVM vm;
    EXPECT_FALSE(vm.runString("this is not lua", "broken.lua"));
    ASSERT_EQ(vm.errors().size(), 1u);
    EXPECT_TRUE(vm.runString("x = 1"));
}

TEST(ScriptVM, EndlessLoopIsStopped) {
    ScriptVM::Limits lim;
    lim.instructionsPerCall = 2'000'000;
    ScriptVM vm(lim);
    EXPECT_FALSE(vm.runString("while true do end", "loop.lua"));
    ASSERT_FALSE(vm.errors().empty());
    EXPECT_NE(vm.errors().back().message.find("too long"), std::string::npos) << vm.errors().back().message;
    // And inside a hook: stopped, the VM still works afterwards.
    ASSERT_TRUE(vm.runString(R"(hook.Add("Think", "spin", function() while true do end end))"));
    vm.callHook("Think", 0.1);
    EXPECT_TRUE(vm.runString("after = 42"));
    EXPECT_EQ(globalNumber(vm, "after"), 42.0);
}

TEST(ScriptVM, MemoryIsCapped) {
    ScriptVM::Limits lim;
    lim.memoryBytes = 4u * 1024u * 1024u;
    ScriptVM vm(lim);
    EXPECT_FALSE(vm.runString("local t = {} for i = 1, 1e8 do t[i] = string.rep('x', 100) .. i end", "hog.lua"));
    ASSERT_FALSE(vm.errors().empty());
    EXPECT_NE(vm.errors().back().message.find("memory"), std::string::npos) << vm.errors().back().message;
    EXPECT_LE(vm.memoryUsed(), lim.memoryBytes);
    EXPECT_TRUE(vm.runString("ok = true"));
}

TEST(ScriptVM, SandboxHasNoFileOrOsAccess) {
    ScriptVM vm;
    EXPECT_TRUE(isNil(vm, "io"));
    EXPECT_TRUE(isNil(vm, "os"));
    EXPECT_TRUE(isNil(vm, "debug"));
    EXPECT_TRUE(isNil(vm, "package"));
    EXPECT_TRUE(isNil(vm, "require"));
    EXPECT_TRUE(isNil(vm, "dofile"));
    EXPECT_TRUE(isNil(vm, "loadfile"));
    EXPECT_TRUE(isNil(vm, "load"));
    EXPECT_TRUE(vm.runString("dumped = string.dump == nil"));
    EXPECT_TRUE(globalBool(vm, "dumped"));
}

TEST(ScriptVM, TimersSimpleAndRepeating) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(
        once, rep = 0, 0
        timer.Simple(1.0, function() once = once + 1 end)
        timer.Create("r", 0.5, 3, function() rep = rep + 1 end)
    )"));
    vm.updateTimers(0.4);
    EXPECT_EQ(globalNumber(vm, "once"), 0.0);
    vm.updateTimers(0.5);
    EXPECT_EQ(globalNumber(vm, "rep"), 1.0);
    vm.updateTimers(1.0);
    EXPECT_EQ(globalNumber(vm, "once"), 1.0);
    EXPECT_EQ(globalNumber(vm, "rep"), 2.0);
    for (double t = 1.1; t < 5.0; t += 0.1) vm.updateTimers(t);
    EXPECT_EQ(globalNumber(vm, "once"), 1.0);
    EXPECT_EQ(globalNumber(vm, "rep"), 3.0); // 3 repetitions, then gone
}

TEST(ScriptVM, ReloadReplacesAScriptsHooksAndTimers) {
    ScriptVM vm;
    std::vector<std::string> unloaded;
    vm.onUnload = [&](const std::string& s) { unloaded.push_back(s); };
    const std::string path = writeTemp("kke_reload_test.lua", R"(
        hook.Add("Think", "count", function() hits = (hits or 0) + 1 end)
        timer.Create("t", 10, 0, function() end)
    )");
    ASSERT_TRUE(vm.runFile(path));
    ASSERT_TRUE(vm.runString(R"(hook.Add("Think", "other", function() end))", "other.lua"));
    EXPECT_EQ(vm.hookCount("Think"), 2u);
    std::ofstream(path) << R"(hook.Add("Think", "count", function() hits = (hits or 0) + 100 end))";
    ASSERT_TRUE(vm.reloadFile(path));
    EXPECT_EQ(vm.hookCount("Think"), 2u); // replaced, not duplicated; other.lua untouched
    ASSERT_EQ(unloaded.size(), 1u);
    EXPECT_EQ(unloaded[0], path);
    vm.callHook("Think", 0.0);
    EXPECT_EQ(globalNumber(vm, "hits", path), 100.0); // a fresh environment: hits started from nil again
    ASSERT_TRUE(vm.runString("timerGone = not timer.Exists('t')"));
    EXPECT_TRUE(globalBool(vm, "timerGone"));
    std::remove(path.c_str());
}

TEST(ScriptVM, BoundFunctionsAndVec) {
    ScriptVM vm;
    glm::vec3 got(0.0f);
    vm.registerFunction("test", "take", [&](lua_State* L) { got = ScriptVM::toVec3(L, 1); return 0; });
    vm.registerFunction("test", "give", [](lua_State* L) { ScriptVM::pushVec3(L, glm::vec3(1, 2, 3)); return 1; });
    vm.registerFunction("test", "fail", [](lua_State* L) { return luaL_error(L, "bad argument from C++"); });
    ASSERT_TRUE(vm.runString(R"(
        local v = test.give() * 2 + Vec(1, 0, 0)
        test.take(v)
        len = Vec(3, 4, 0):length()
        ok, msg = pcall(test.fail)
    )"));
    EXPECT_EQ(got, glm::vec3(3, 4, 6));
    EXPECT_EQ(globalNumber(vm, "len"), 5.0);
    EXPECT_NE(globalString(vm, "msg").find("bad argument"), std::string::npos);
}

TEST(ScriptVM, PrintGoesToTheSinkWithTheSource) {
    ScriptVM vm;
    std::string src, text;
    vm.printSink = [&](const std::string& s, const std::string& t) { src = s; text = t; };
    ASSERT_TRUE(vm.runString("print('hello', 42, Vec(1,2,3))", "greeter.lua"));
    EXPECT_EQ(src, "greeter.lua");
    EXPECT_EQ(text, "hello\t42\tVec(1.000, 2.000, 3.000)");
}

TEST(ScriptVM, HookRegisteredFromATimerBelongsToItsScript) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(timer.Simple(0, function() hook.Add("Think", "late", function() end) end))", "a.lua"));
    vm.updateTimers(1.0);
    EXPECT_EQ(vm.hookCount("Think"), 1u);
    vm.unload("a.lua");
    EXPECT_EQ(vm.hookCount("Think"), 0u);
}

TEST(ScriptVM, EachScriptHasItsOwnGlobals) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString("score = 10; function helper() return 1 end", "a.lua"));
    ASSERT_TRUE(vm.runString("seen = score; hasHelper = helper ~= nil; score = 99", "b.lua"));
    EXPECT_TRUE(globalBool(vm, "seen", "b.lua") == false); // nil
    EXPECT_FALSE(globalBool(vm, "hasHelper", "b.lua"));
    EXPECT_EQ(globalNumber(vm, "score", "a.lua"), 10.0); // b's write stayed in b
    // _G is the script's own table too.
    ASSERT_TRUE(vm.runString("_G.viaG = 5", "a.lua"));
    EXPECT_EQ(globalNumber(vm, "viaG", "a.lua"), 5.0);
    EXPECT_TRUE(isNil(vm, "viaG"));
}

TEST(ScriptVM, ScriptsShareOnPurposeThroughSharedAndHooks) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(
        shared.best = 42
        hook.Add("AskScore", "a", function() return 7 end)
    )", "a.lua"));
    ASSERT_TRUE(vm.runString("best = shared.best; asked = hook.Run('AskScore')", "b.lua"));
    EXPECT_EQ(globalNumber(vm, "best", "b.lua"), 42.0);
    EXPECT_EQ(globalNumber(vm, "asked", "b.lua"), 7.0);
}

TEST(ScriptVM, EngineTablesAreReadOnlyForScripts) {
    ScriptVM vm;
    vm.registerFunction("physics", "count", [](lua_State* L) { lua_pushinteger(L, 3); return 1; });
    // Each of these would break every other script if it worked.
    EXPECT_FALSE(vm.runString("physics.count = function() return 0 end", "evil1.lua"));
    EXPECT_FALSE(vm.runString("hook.Add = nil", "evil2.lua"));
    EXPECT_FALSE(vm.runString("string.upper = string.lower", "evil3.lua"));
    EXPECT_FALSE(vm.runString("setmetatable(physics, {})", "evil4.lua"));
    EXPECT_FALSE(vm.runString("getmetatable(Vec(0,0,0)).__add = nil", "evil5.lua"));
    EXPECT_FALSE(vm.runString("getmetatable('').__index = {}", "evil6.lua"));
    ASSERT_TRUE(vm.runString(R"(
        n = physics.count()
        up = ("abc"):upper()
        v = Vec(1, 2, 3) + Vec(1, 1, 1)
        sum = v.x + v.y + v.z
        local k = 0
        for name in pairs(physics) do k = k + 1 end
        fields = k
    )", "good.lua"));
    EXPECT_EQ(globalNumber(vm, "n", "good.lua"), 3.0);
    EXPECT_EQ(globalString(vm, "up", "good.lua"), "ABC");
    EXPECT_EQ(globalNumber(vm, "sum", "good.lua"), 9.0);
    EXPECT_EQ(globalNumber(vm, "fields", "good.lua"), 1.0); // pairs sees through the read-only view
    ASSERT_GE(vm.errors().size(), 6u);
    EXPECT_NE(vm.errors()[0].message.find("read-only"), std::string::npos) << vm.errors()[0].message;
}

TEST(ScriptVM, SharedGlobalsModeStillAvailable) {
    ScriptVM::Limits lim;
    lim.isolateScripts = false;
    ScriptVM vm(lim);
    ASSERT_TRUE(vm.runString("score = 10", "a.lua"));
    ASSERT_TRUE(vm.runString("seen = score", "b.lua"));
    EXPECT_EQ(globalNumber(vm, "seen", "b.lua"), 10.0);
    lua_getglobal(vm.state(), "seen");
    EXPECT_EQ(lua_tonumber(vm.state(), -1), 10.0);
    lua_pop(vm.state(), 1);
}

TEST(ScriptVM, RunawayInsidePcallIsHardStoppedAndUnloaded) {
    ScriptVM::Limits lim;
    lim.instructionsPerCall = 1'000'000;
    ScriptVM vm(lim);
    std::vector<std::string> stopped, unloaded;
    vm.onStopped = [&](const std::string& s) { stopped.push_back(s); };
    vm.onUnload = [&](const std::string& s) { unloaded.push_back(s); };
    ASSERT_TRUE(vm.runString(R"(
        hook.Add("Think", "spin", function()
            while true do pcall(function() while true do end end) end
        end)
    )", "stubborn.lua"));
    ASSERT_TRUE(vm.runString(R"(ticks = 0 hook.Add("Think", "count", function() ticks = ticks + 1 end))", "fine.lua"));
    vm.callHook("Think", 0.016); // stubborn.lua runs first and never returns
    ASSERT_EQ(stopped.size(), 1u);
    EXPECT_EQ(stopped[0], "stubborn.lua");
    EXPECT_EQ(unloaded, std::vector<std::string>{"stubborn.lua"});
    EXPECT_EQ(vm.hookCount("Think"), 1u); // its hook is gone, fine.lua's stays
    ASSERT_EQ(vm.stoppedScripts().size(), 1u);
    EXPECT_NE(vm.errors().back().message.find("was stopped"), std::string::npos) << vm.errors().back().message;
    vm.callHook("Think", 0.016);
    EXPECT_EQ(globalNumber(vm, "ticks", "fine.lua"), 1.0);
    EXPECT_EQ(vm.currentSource(), "");
}

TEST(ScriptVM, RunawayTimerAndLoadAreStoppedToo) {
    ScriptVM::Limits lim;
    lim.instructionsPerCall = 1'000'000;
    ScriptVM vm(lim);
    ASSERT_TRUE(vm.runString("timer.Simple(0, function() while true do pcall(error) end end)", "t.lua"));
    vm.updateTimers(1.0);
    ASSERT_EQ(vm.stoppedScripts().size(), 1u);
    EXPECT_EQ(vm.stoppedScripts()[0], "t.lua");
    EXPECT_FALSE(vm.runString("while true do pcall(function() end) end", "load.lua"));
    EXPECT_EQ(vm.stoppedScripts().size(), 2u);
    // A script with its own coroutines: stopped all the same.
    EXPECT_FALSE(vm.runString("local co = coroutine.wrap(function() while true do end end) while true do pcall(co) end", "co.lua"));
    EXPECT_EQ(vm.stoppedScripts().size(), 3u);
    EXPECT_TRUE(vm.runString("ok = 1"));
}

TEST(ScriptVM, YieldOutsideACoroutineIsAnError) {
    ScriptVM vm;
    EXPECT_FALSE(vm.runString("coroutine.yield()", "y.lua"));
    ASSERT_FALSE(vm.errors().empty());
    EXPECT_NE(vm.errors().back().message.find("coroutine"), std::string::npos);
    EXPECT_TRUE(vm.stoppedScripts().empty());
    // Coroutines inside a script work normally.
    ASSERT_TRUE(vm.runString(R"(
        local gen = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end)
        total = gen() + gen() + gen()
    )"));
    EXPECT_EQ(globalNumber(vm, "total"), 6.0);
}

TEST(ScriptVM, CpuTimeIsChargedToEachScript) {
    ScriptVM vm;
    ASSERT_TRUE(vm.runString(R"(hook.Add("Think", "busy", function() local x = 0 for i = 1, 200000 do x = x + i end end))", "busy.lua"));
    ASSERT_TRUE(vm.runString(R"(hook.Add("Think", "idle", function() end))", "idle.lua"));
    for (int i = 0; i < 5; ++i) vm.callHook("Think", 0.016);
    EXPECT_GT(vm.cpuSeconds("busy.lua"), 0.0);
    EXPECT_GT(vm.cpuSeconds("busy.lua"), vm.cpuSeconds("idle.lua"));
    EXPECT_EQ(vm.cpuSeconds("never.lua"), 0.0);
}

TEST(ScriptVM, CallbacksKeptByRefRunAsTheirScript) {
    ScriptVM vm;
    int saved = 0;
    vm.registerFunction("test", "keep", [&](lua_State* L) { saved = vm.ref(L, 1); return 0; });
    ASSERT_TRUE(vm.runString("test.keep(function(a, b) got = a + b; error('oops') end)", "cb.lua"));
    ASSERT_GT(saved, 0);
    EXPECT_FALSE(vm.callRef(saved, "cb.lua", [](lua_State* L) { lua_pushnumber(L, 2); lua_pushnumber(L, 3); return 2; }));
    EXPECT_EQ(globalNumber(vm, "got", "cb.lua"), 5.0);
    ASSERT_FALSE(vm.errors().empty());
    EXPECT_EQ(vm.errors().back().source, "cb.lua");
    EXPECT_NE(vm.errors().back().message.find("oops"), std::string::npos);
    vm.unref(saved);
    EXPECT_FALSE(vm.callRef(saved, "cb.lua"));
    EXPECT_FALSE(vm.callRef(0, "cb.lua"));
}

TEST(ScriptVM, ValuesRoundTripAsBytes) {
    ScriptVM vm;
    lua_State* L = vm.state();
    ASSERT_TRUE(vm.runString(R"(msg = { name = "Kees", score = 12, ratio = 0.5, ok = true, list = { 1, 2, "three" }, nested = { deep = { x = -1 } } })"));
    vm.pushEnv("console");
    lua_getfield(L, -1, "msg");
    std::string bytes, err;
    ASSERT_TRUE(ScriptVM::encodeValue(L, -1, bytes, err)) << err;
    lua_pop(L, 2);
    ASSERT_TRUE(ScriptVM::decodeValue(L, bytes, err)) << err;
    vm.pushEnv("console");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "back");
    lua_pop(L, 2);
    ASSERT_TRUE(vm.runString(R"(
        same = back.name == "Kees" and back.score == 12 and math.type(back.score) == "integer" and back.ratio == 0.5
           and back.ok == true and #back.list == 3 and back.list[3] == "three" and back.nested.deep.x == -1
    )"));
    EXPECT_TRUE(globalBool(vm, "same"));
    for (const char* v : {"nil", "false", "42", "'text'", "{}"}) {
        ASSERT_TRUE(vm.runString(std::string("v = ") + v));
        vm.pushEnv("console");
        lua_getfield(L, -1, "v");
        EXPECT_TRUE(ScriptVM::encodeValue(L, -1, bytes, err)) << v << ": " << err;
        lua_pop(L, 2);
        EXPECT_TRUE(ScriptVM::decodeValue(L, bytes, err)) << v << ": " << err;
        lua_pop(L, 1);
    }
}

TEST(ScriptVM, UnsendableValuesAndBadBytesAreRefused) {
    ScriptVM vm;
    lua_State* L = vm.state();
    std::string bytes, err;
    auto encodeGlobal = [&](const char* code) {
        EXPECT_TRUE(vm.runString(code));
        vm.pushEnv("console");
        lua_getfield(L, -1, "v");
        const bool ok = ScriptVM::encodeValue(L, -1, bytes, err, 256);
        lua_pop(L, 2);
        return ok;
    };
    EXPECT_FALSE(encodeGlobal("v = { f = print }"));
    EXPECT_NE(err.find("function"), std::string::npos) << err;
    EXPECT_FALSE(encodeGlobal("v = {} v.self = v"));
    EXPECT_NE(err.find("deep"), std::string::npos) << err;
    EXPECT_FALSE(encodeGlobal("v = string.rep('x', 1000)"));
    EXPECT_NE(err.find("too big"), std::string::npos) << err;
    const int top = lua_gettop(L);
    // Bytes off the network: anything may arrive.
    for (const std::string& bad : {std::string(), std::string("\x09"), std::string("\x05\xff\xff\xff\x7f" "abc"),
                                  std::string("\x06\x01\x00\x00\x00", 5), std::string("\x06\x01\x00\x00\x00\x00\x01", 7),
                                  std::string("\x01\x01", 2), std::string("\x03\x00", 2)}) {
        EXPECT_FALSE(ScriptVM::decodeValue(L, bad, err)) << "accepted " << bad.size() << " bad bytes";
        EXPECT_EQ(lua_gettop(L), top); // nothing left behind
    }
    // Deep nesting in the bytes is refused too, without recursing forever.
    std::string deep;
    for (int i = 0; i < 100; ++i) deep += std::string("\x06\x01\x00\x00\x00\x04", 6) + std::string(8, '\0');
    EXPECT_FALSE(ScriptVM::decodeValue(L, deep, err));
    EXPECT_EQ(lua_gettop(L), top);
}

TEST(ScriptModule, ServerScriptsRunOnlyWhereThePhysicsIsTheTruth) {
    EXPECT_TRUE(ScriptModule::runsHere("scripts/sv_rules.lua", true));
    EXPECT_FALSE(ScriptModule::runsHere("scripts/sv_rules.lua", false));
    EXPECT_TRUE(ScriptModule::runsHere("scripts/cl_hud.lua", false));
    EXPECT_TRUE(ScriptModule::runsHere("scripts/sh_shared.lua", false));
    EXPECT_TRUE(ScriptModule::runsHere("scripts/toys.lua", false));
    EXPECT_TRUE(ScriptModule::runsHere("C:/game/scripts/sv_rules.lua", true));
    EXPECT_TRUE(ScriptModule::runsHere("scripts/sv_folder/rules.lua", false)); // the file name decides, not the folder
}
