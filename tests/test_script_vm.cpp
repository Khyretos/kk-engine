#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <cstdio>
#include <fstream>

using namespace kke;

namespace {
double globalNumber(ScriptVM& vm, const char* name) {
    lua_getglobal(vm.state(), name);
    const double v = lua_tonumber(vm.state(), -1);
    lua_pop(vm.state(), 1);
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
    EXPECT_EQ(globalNumber(vm, "ran"), 2.0);
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
    lua_getglobal(vm.state(), "dumped");
    EXPECT_TRUE(lua_toboolean(vm.state(), -1));
    lua_pop(vm.state(), 1);
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
    EXPECT_EQ(globalNumber(vm, "hits"), 100.0);
    ASSERT_TRUE(vm.runString("timerGone = not timer.Exists('t')"));
    lua_getglobal(vm.state(), "timerGone");
    EXPECT_TRUE(lua_toboolean(vm.state(), -1));
    lua_pop(vm.state(), 1);
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
    lua_getglobal(vm.state(), "msg");
    EXPECT_NE(std::string(lua_tostring(vm.state(), -1)).find("bad argument"), std::string::npos);
    lua_pop(vm.state(), 1);
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
