// bt.* in Lua (kke/ScriptBehavior.h, ENG-F71): trees whose leaves are Lua functions.
#include "kke/ScriptBehavior.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <string>

using namespace kke;

namespace {

double globalNumber(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const double v = lua_tonumber(vm.state(), -1);
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
bool globalBool(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const bool v = lua_toboolean(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string lastError(const ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }

struct Fixture {
    ScriptVM vm;
    ScriptBehavior bt;
    Fixture() {
        bt.bind(vm);
        vm.onUnload = [this](const std::string& source) { bt.release(source); };
    }
    bool run(const std::string& code, const std::string& source = "console") { return vm.runString(code, source); }
};

} // namespace

TEST(ScriptBehavior, RunsATreeOfLuaFunctions) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        log = {}
        tree = bt.create{ "sequence",
            { "condition", function(bb) return bb.hp > 0 end },
            { "action", function(bb, dt) log[#log + 1] = "hit" bb.hits = (bb.hits or 0) + 1 return "success" end },
            function(bb) log[#log + 1] = "bare function" end,
            { "wait", 0.25 },
            { "set", "done", true } }
        bt.set(tree, "hp", 10)
        r1 = bt.tick(tree, 0.1)
        r2 = bt.tick(tree, 0.1)
        r3 = bt.tick(tree, 0.1)
        hits = bt.get(tree, "hits")
        done = bt.get(tree, "done")
        entries = #log
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "r1"), "running") << "the wait is still running";
    EXPECT_EQ(globalString(f.vm, "r2"), "running");
    EXPECT_EQ(globalString(f.vm, "r3"), "success");
    EXPECT_EQ(globalNumber(f.vm, "hits"), 1.0) << "a finished child is not run again while the wait runs";
    EXPECT_TRUE(globalBool(f.vm, "done"));
    EXPECT_EQ(globalNumber(f.vm, "entries"), 2.0);
    EXPECT_EQ(f.bt.treeCount(), 1u);
}

TEST(ScriptBehavior, TheBlackboardActsLikeATable) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tree = bt.create{ "action", function(bb)
            bb.number = 4
            bb.text = "orc"
            bb.flag = true
            bb.home = Vec(1, 2, 3)
            seen = bb.number + 1
            same = bb.text == "orc"
            x = bb.home.x
            missing = bb.nothing == nil
            bb.number = nil
            gone = bb.number == nil
        end }
        bt.tick(tree, 0.1)
        outside = bt.get(tree, "text")
        flag = bt.get(tree, "flag")
        kept = bt.get(tree, "number")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "seen"), 5.0);
    EXPECT_TRUE(globalBool(f.vm, "same"));
    EXPECT_EQ(globalNumber(f.vm, "x"), 1.0);
    EXPECT_TRUE(globalBool(f.vm, "missing"));
    EXPECT_TRUE(globalBool(f.vm, "gone"));
    EXPECT_EQ(globalString(f.vm, "outside"), "orc");
    EXPECT_TRUE(globalBool(f.vm, "flag"));
    EXPECT_EQ(globalString(f.vm, "kept"), "") << "a nil write removed it";
    // a function can't be stored
    EXPECT_FALSE(f.run("bt.tick(bt.create{ 'action', function(bb) bb.f = print end }, 0.1)"));
    EXPECT_NE(lastError(f.vm).find("a value is a number, boolean, string or Vec"), std::string::npos) << lastError(f.vm);
}

TEST(ScriptBehavior, DecoratorsTakeTheirNumberFirst) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        n = 0
        tree = bt.create{ "repeat", 3, function() n = n + 1 end }
        a = bt.tick(tree, 0.1)
        b = bt.tick(tree, 0.1)
        c = bt.tick(tree, 0.1)
        cd = bt.create{ "cooldown", 1, function() return true end }
        d1 = bt.tick(cd, 0.1)
        d2 = bt.tick(cd, 0.1)
        inv = bt.create{ "inverter", { "check", "x", "==", 5 } }
        bt.set(inv, "x", 5)
        i1 = bt.tick(inv, 0.1)
        ch = bt.create{ "retry", 2, function() return false end }
        r1 = bt.tick(ch, 0.1)
        r2 = bt.tick(ch, 0.1)
        forever = bt.create{ "repeat", function() end }
        f1 = bt.tick(forever, 0.1)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "a"), "running");
    EXPECT_EQ(globalString(f.vm, "b"), "running");
    EXPECT_EQ(globalString(f.vm, "c"), "success");
    EXPECT_EQ(globalNumber(f.vm, "n"), 3.0);
    EXPECT_EQ(globalString(f.vm, "d1"), "success");
    EXPECT_EQ(globalString(f.vm, "d2"), "failure");
    EXPECT_EQ(globalString(f.vm, "i1"), "failure");
    EXPECT_EQ(globalString(f.vm, "r1"), "running");
    EXPECT_EQ(globalString(f.vm, "r2"), "failure");
    EXPECT_EQ(globalString(f.vm, "f1"), "running");
}

TEST(ScriptBehavior, ReactiveBranchesAbortAndTellTheLosingAction) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        aborted = 0
        tree = bt.create{ "reactive_selector",
            { "reactive_sequence", { "check", "danger" }, { "action", function() return "running" end } },
            { "action", function() return "running" end, function(bb) aborted = aborted + 1 end } }
        s1 = bt.tick(tree, 0.1)
        bt.set(tree, "danger", true)
        s2 = bt.tick(tree, 0.1)
        after = aborted
        bt.reset(tree)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "s1"), "running");
    EXPECT_EQ(globalString(f.vm, "s2"), "running");
    EXPECT_EQ(globalNumber(f.vm, "after"), 1.0) << "the pre-empted action heard about it";
}

TEST(ScriptBehavior, DefinedTreesAreReusedByName) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        count = 0
        bt.define("greet", { "sequence", function() count = count + 1 end, { "set", "greeted", true } })
        tree = bt.create{ "sequence", "greet", { "subtree", "greet" } }
        result = bt.tick(tree, 0.1)
        greeted = bt.get(tree, "greeted")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "result"), "success");
    EXPECT_EQ(globalNumber(f.vm, "count"), 2.0);
    EXPECT_TRUE(globalBool(f.vm, "greeted"));
}

TEST(ScriptBehavior, ExplainsABadTreeWithItsPath) {
    Fixture f;
    EXPECT_FALSE(f.run("bt.create{ 'sequnce', function() end }"));
    EXPECT_NE(lastError(f.vm).find("bt.create: root: unknown type 'sequnce'"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("bt.create{ 'sequence', function() end, { 'action' } }"));
    EXPECT_NE(lastError(f.vm).find("root.children[1]: action takes a function"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("bt.create{ 'sequence', 42 }"));
    EXPECT_NE(lastError(f.vm).find("root.children[0]: a node is a table"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("bt.create{ 'sequence', 'nobody' }"));
    EXPECT_NE(lastError(f.vm).find("no tree called 'nobody'"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("bt.create{ 'check' }"));
    EXPECT_NE(lastError(f.vm).find("needs \"key\""), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("bt.create{ { 'sequence' } }"));
    EXPECT_NE(lastError(f.vm).find("first item of a node is its type"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("t = {} t[1] = 'sequence' t[2] = t bt.create(t)"));
    EXPECT_NE(lastError(f.vm).find("deep"), std::string::npos) << lastError(f.vm);
    EXPECT_EQ(f.bt.treeCount(), 0u);
}

TEST(ScriptBehavior, ALeafThatBreaksStopsTheTickWithItsName) {
    Fixture f;
    EXPECT_FALSE(f.run("t = bt.create{ 'action', function() error('boom') end, label = 'jump' } bt.tick(t, 0.1)"));
    EXPECT_NE(lastError(f.vm).find("bt.tick: jump: "), std::string::npos) << lastError(f.vm);
    EXPECT_NE(lastError(f.vm).find("boom"), std::string::npos);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("t = bt.create{ 'action', function() return 'maybe' end } bt.tick(t, 0.1)"));
    EXPECT_NE(lastError(f.vm).find("returned \"maybe\""), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("t = bt.create{ 'action', function() return 5 end } bt.tick(t, 0.1)"));
    EXPECT_NE(lastError(f.vm).find("returned a number"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    // the tree is still usable afterwards
    ASSERT_TRUE(f.run("ok = bt.create{ 'success' } r = bt.tick(ok, 0.1)")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "r"), "success");
}

TEST(ScriptBehavior, ARunawayLeafIsStoppedLikeAnyScript) {
    Fixture f;
    EXPECT_FALSE(f.run("t = bt.create{ 'action', function() while true do end end } bt.tick(t, 0.1)", "runaway"));
    EXPECT_FALSE(f.vm.errors().empty());
}

TEST(ScriptBehavior, TreesFollowTheirScriptsAndHandlesAreChecked) {
    Fixture f;
    ASSERT_TRUE(f.run("a = bt.create{ 'success' } b = bt.create{ 'failure' } ex = bt.exists(a) n = bt.count()", "one"));
    ASSERT_TRUE(f.run("c = bt.create{ 'success', label = 'x' }", "two"));
    EXPECT_TRUE(globalBool(f.vm, "ex", "one"));
    EXPECT_EQ(globalNumber(f.vm, "n", "one"), 2.0);
    EXPECT_EQ(f.bt.treeCount(), 3u);
    f.vm.unload("one");
    EXPECT_EQ(f.bt.treeCount(), 1u) << "unloading a script frees its trees";
    EXPECT_FALSE(f.run("bt.tick(" + std::to_string(static_cast<int>(globalNumber(f.vm, "a", "one"))) + ", 0.1)", "console"));
    EXPECT_NE(lastError(f.vm).find("no behaviour tree"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    ASSERT_TRUE(f.run("gone = bt.create{ 'success' } bt.destroy(gone) existsAfter = bt.exists(gone)"));
    EXPECT_FALSE(globalBool(f.vm, "existsAfter"));
    EXPECT_EQ(f.bt.treeCount(), 1u);
    // a tree can't be ticked from inside itself
    EXPECT_FALSE(f.run("me = bt.create{ 'action', function() bt.tick(me, 0.1) end } bt.tick(me, 0.1)"));
    EXPECT_NE(lastError(f.vm).find("already ticking"), std::string::npos) << lastError(f.vm);
}

TEST(ScriptBehavior, DescribeAndStatusReportWhatHappened) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        t = bt.create{ "selector", { "check", "hp", "<", 30 }, { "action", function() return "running" end, label = "melee" } }
        bt.set(t, "hp", 80)
        bt.tick(t, 0.1)
        status = bt.status(t)
        text = bt.describe(t)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "status"), "running");
    const std::string text = globalString(f.vm, "text");
    EXPECT_NE(text.find("selector"), std::string::npos);
    EXPECT_NE(text.find("check \"hp < 30\" [failure]"), std::string::npos) << text;
    EXPECT_NE(text.find("action \"melee\" [running]"), std::string::npos) << text;
}

TEST(ScriptBehavior, SeedsMakeRandomChoicesRepeatable) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        function run(seed)
            local order = {}
            local t = bt.create({ "random_selector",
                function() order[#order + 1] = "a" return false end,
                function() order[#order + 1] = "b" return false end,
                function() order[#order + 1] = "c" return false end }, { seed = seed })
            bt.tick(t, 0.1)
            return table.concat(order)
        end
        one, two, three = run(5), run(5), run(6)
        same = one == two
    )")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "same"));
    EXPECT_EQ(globalString(f.vm, "one").size(), 3u);
}
