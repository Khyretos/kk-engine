// world.* in Lua (kke/ScriptWorld.h, ENG-F64 / ENG-F78).
#include "kke/ScriptVM.h"
#include "kke/ScriptWorld.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

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
    ScriptWorld world;
    glm::vec3 camera{ 0.0f };
    Fixture() {
        world.bind(vm);
        vm.onUnload = [this](const std::string& source) { world.release(source); };
    }
    bool run(const std::string& code, const std::string& source = "console") { return vm.runString(code, source); }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) world.update(0.1f, vm, camera);
    }
};

// a row of 6 cells along x, 10 m each: "t_0_0" .. "t_5_0"
const char* kRow = R"(
    loadedNames, unloadedNames = {}, {}
    ok, why = world.stream{
        grid = { prefix = "t", size = 10, x = { 0, 5 }, z = { 0, 0 } },
        loadRadius = 15, unloadRadius = 25, requiredRadius = 10, maxStarts = 4,
        load = function(cell) loadedNames[#loadedNames + 1] = cell.name end,
        unload = function(cell) unloadedNames[#unloadedNames + 1] = cell.name end,
    }
)";

} // namespace

TEST(ScriptWorld, CellsLoadAndUnloadAroundAFocusThroughTheScriptsFunctions) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + R"(
        world.focus("player", Vec(5, 0, 5))
    )")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    f.tick(2);
    ASSERT_TRUE(f.run("n = #loadedNames list = table.concat(loadedNames, ',') s0 = world.state('t_0_0') s5 = world.state('t_5_0') none = world.state('nothing') ")) << lastError(f.vm);
    // within 15 m of (5, 5): cells 0 (0 m), 1 (5 m), 2 (15 m)
    EXPECT_EQ(globalString(f.vm, "list"), "t_0_0,t_1_0,t_2_0");
    EXPECT_EQ(globalString(f.vm, "s0"), "loaded");
    EXPECT_EQ(globalString(f.vm, "s5"), "unloaded");
    EXPECT_EQ(globalString(f.vm, "none"), "") << "no such cell: nil";
    // move away: they unload, the far ones load
    ASSERT_TRUE(f.run("world.focus('player', Vec(59, 0, 5))")) << lastError(f.vm); // cell 2 is then 29 m away: past the 25 m unload radius
    f.tick(3);
    ASSERT_TRUE(f.run("ul = table.concat(unloadedNames, ',') ld = table.concat(loadedNames, ',') cur = table.concat(world.loaded(), ',')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "ul"), "t_0_0,t_1_0,t_2_0") << "farthest first";
    EXPECT_NE(globalString(f.vm, "ld").find("t_5_0"), std::string::npos);
    EXPECT_EQ(globalString(f.vm, "cur").find("t_0_0"), std::string::npos);
}

TEST(ScriptWorld, TheCellTableTellsWhereItIs) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        got = nil
        world.stream{
            cells = { { name = "yard", min = { 10, 20 }, max = Vec(30, 5, 60), data = "scenes/yard.json", persistent = true } },
            load = function(cell) got = cell end,
        }
    )")) << lastError(f.vm);
    f.tick();
    ASSERT_TRUE(f.run("name, data, minX, minZ, maxX, maxZ, cx, cz, persistent = got.name, got.data, got.minX, got.minZ, got.maxX, got.maxZ, got.x, got.z, got.persistent")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "name"), "yard");
    EXPECT_EQ(globalString(f.vm, "data"), "scenes/yard.json");
    EXPECT_EQ(globalNumber(f.vm, "minX"), 10.0);
    EXPECT_EQ(globalNumber(f.vm, "minZ"), 20.0);
    EXPECT_EQ(globalNumber(f.vm, "maxX"), 30.0);
    EXPECT_EQ(globalNumber(f.vm, "maxZ"), 60.0) << "a Vec gives its x and z";
    EXPECT_EQ(globalNumber(f.vm, "cx"), 20.0);
    EXPECT_EQ(globalNumber(f.vm, "cz"), 40.0);
    EXPECT_TRUE(globalBool(f.vm, "persistent"));
}

TEST(ScriptWorld, HooksTellWhenCellsComeAndGo) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + R"(
        events = {}
        hook.Add("CellLoaded", "t", function(e) events[#events + 1] = "+" .. e.name end)
        hook.Add("CellUnloaded", "t", function(e) events[#events + 1] = "-" .. e.name end)
        world.focus("p", Vec(5, 0, 5))
    )")) << lastError(f.vm);
    f.tick(2);
    ASSERT_TRUE(f.run("world.focus('p', Vec(59, 0, 5)) a = table.concat(events, ' ')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "a"), "+t_0_0 +t_1_0 +t_2_0");
    f.tick(2);
    ASSERT_TRUE(f.run("b = table.concat(events, ' ')")) << lastError(f.vm);
    const std::string b = globalString(f.vm, "b");
    EXPECT_NE(b.find("-t_0_0"), std::string::npos) << b;
    EXPECT_NE(b.find("+t_5_0"), std::string::npos) << b;
}

TEST(ScriptWorld, ALoadCanTakeAWhileAndSayWhenItIsDone) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        world.stream{ grid = { prefix = "t", size = 10, x = { 0, 0 }, z = { 0, 0 } }, loadRadius = 15,
            load = function(cell) return "pending" end }
        world.focus("p", Vec(5, 0, 5))
        done_early = world.done("t_0_0")
    )")) << lastError(f.vm);
    f.tick(2);
    ASSERT_TRUE(f.run("state1 = world.state('t_0_0') l1 = #world.loading() pr = world.progress() rd = world.ready()")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "state1"), "loading");
    EXPECT_EQ(globalNumber(f.vm, "l1"), 1.0);
    EXPECT_FALSE(globalBool(f.vm, "rd"));
    ASSERT_TRUE(f.run("ok = world.done('t_0_0') again = world.done('t_0_0')")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    EXPECT_FALSE(globalBool(f.vm, "again")) << "only a pending one can be finished";
    f.tick();
    ASSERT_TRUE(f.run("state2 = world.state('t_0_0') rd2 = world.ready() pr2 = world.progress()")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "state2"), "loaded");
    EXPECT_TRUE(globalBool(f.vm, "rd2"));
    EXPECT_EQ(globalNumber(f.vm, "pr2"), 1.0);
}

TEST(ScriptWorld, AFailedLoadSaysWhyAndIsTriedAgain) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tries = 0
        reasons = {}
        world.stream{ grid = { prefix = "t", size = 10, x = { 0, 0 }, z = { 0, 0 } }, loadRadius = 15, retryDelay = 0.3, maxRetries = 3,
            load = function(cell)
                tries = tries + 1
                if tries == 1 then return false, "file missing" end
                if tries == 2 then error("boom") end
            end }
        hook.Add("CellFailed", "t", function(e) reasons[#reasons + 1] = e.name .. ": " .. e.reason end)
        world.focus("p", Vec(5, 0, 5))
    )")) << lastError(f.vm);
    f.tick(20);
    ASSERT_TRUE(f.run("n = tries first = reasons[1] second = reasons[2] state = world.state('t_0_0')")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "n"), 3.0);
    EXPECT_EQ(globalString(f.vm, "first"), "t_0_0: file missing");
    EXPECT_NE(globalString(f.vm, "second").find("raised an error"), std::string::npos) << globalString(f.vm, "second");
    EXPECT_EQ(globalString(f.vm, "state"), "loaded") << "the third try works";
    // world.failed on a pending cell
    Fixture g;
    ASSERT_TRUE(g.run(R"(
        why = nil
        world.stream{ grid = { prefix = "t", size = 10, x = { 0, 0 }, z = { 0, 0 } }, loadRadius = 15, retryDelay = 100,
            load = function() return "pending" end }
        hook.Add("CellFailed", "t", function(e) why = e.reason end)
        world.focus("p", Vec(5, 0, 5))
    )")) << lastError(g.vm);
    g.tick(2);
    ASSERT_TRUE(g.run("r = world.failed('t_0_0', 'the network dropped') other = world.failed('nothing')")) << lastError(g.vm);
    EXPECT_TRUE(globalBool(g.vm, "r"));
    EXPECT_FALSE(globalBool(g.vm, "other"));
    g.tick(2);
    ASSERT_TRUE(g.run("w = why s = world.state('t_0_0')"));
    EXPECT_EQ(globalString(g.vm, "w"), "the network dropped");
    EXPECT_EQ(globalString(g.vm, "s"), "failed");
}

TEST(ScriptWorld, TheCameraCanBeTheFocus) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        world.stream{ grid = { prefix = "t", size = 10, x = { 0, 5 }, z = { 0, 0 } }, loadRadius = 12, unloadRadius = 20, followCamera = true }
    )")) << lastError(f.vm);
    f.camera = glm::vec3(55, 3, 5);
    f.tick(3);
    ASSERT_TRUE(f.run("a = world.state('t_5_0') b = world.state('t_0_0')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "a"), "loaded");
    EXPECT_EQ(globalString(f.vm, "b"), "unloaded");
    f.camera = glm::vec3(5, 3, 5);
    f.tick(8);
    ASSERT_TRUE(f.run("c = world.state('t_0_0') d = world.state('t_5_0')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "c"), "loaded");
    EXPECT_EQ(globalString(f.vm, "d"), "unloaded");
}

TEST(ScriptWorld, ReadinessForALoadingScreen) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + "world.focus('p', Vec(5, 0, 5)) before = world.progress() rd0 = world.ready()")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "before"), 0.0);
    EXPECT_FALSE(globalBool(f.vm, "rd0"));
    f.tick(1);
    ASSERT_TRUE(f.run("rd1 = world.ready() p1 = world.progress()")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "rd1")) << "requiredRadius 10: cells 0 and 1, both loaded in one update (maxStarts 4)";
    EXPECT_EQ(globalNumber(f.vm, "p1"), 1.0);
}

TEST(ScriptWorld, PinForceAndDistance) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + R"(
        world.focus("p", Vec(5, 0, 5))
        pinned = world.pin("t_1_0")
        forced = world.force("t_5_0")
        badPin = world.pin("nothing")
        d0 = world.distance("t_0_0")
        d3 = world.distance("t_3_0")
        dn = world.distance("nothing")
    )")) << lastError(f.vm);
    f.tick(3);
    EXPECT_TRUE(globalBool(f.vm, "pinned"));
    EXPECT_TRUE(globalBool(f.vm, "forced"));
    EXPECT_FALSE(globalBool(f.vm, "badPin"));
    EXPECT_EQ(globalNumber(f.vm, "d0"), 0.0);
    EXPECT_EQ(globalNumber(f.vm, "d3"), 25.0);
    EXPECT_EQ(globalString(f.vm, "dn"), "");
    ASSERT_TRUE(f.run("far = world.state('t_5_0')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "far"), "loaded") << "forced: from anywhere";
    ASSERT_TRUE(f.run("world.unfocus('p') gone = world.unfocus('p')")) << lastError(f.vm);
    f.tick(3);
    ASSERT_TRUE(f.run("kept = world.state('t_1_0') dropped = world.state('t_0_0')")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "gone"));
    EXPECT_EQ(globalString(f.vm, "kept"), "loaded") << "pinned";
    EXPECT_EQ(globalString(f.vm, "dropped"), "unloaded") << "no one is near";
}

TEST(ScriptWorld, ANewWorldReplacesTheOldAndAMistakeLeavesItAlone) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + "world.focus('p', Vec(5, 0, 5))")) << lastError(f.vm);
    f.tick(2);
    ASSERT_TRUE(f.run("count = #loadedNames")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "count"), 3.0);
    // a mistake: refused, and the world carries on
    ASSERT_TRUE(f.run(R"(
        bad, why = world.stream{ cells = { { name = "x" } } }
        still = world.state("t_0_0")
    )")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "bad"));
    EXPECT_NE(globalString(f.vm, "why").find("cells[0] ('x') needs min and max"), std::string::npos) << globalString(f.vm, "why");
    EXPECT_EQ(globalString(f.vm, "still"), "loaded");
    // a good one: the old world's cells are unloaded through its unload function first
    ASSERT_TRUE(f.run(R"(
        newLoads = {}
        world.stream{ grid = { prefix = "new", size = 10, x = { 0, 0 }, z = { 0, 0 } }, loadRadius = 15,
            load = function(cell) newLoads[#newLoads + 1] = cell.name end }
        world.focus("p", Vec(5, 0, 5))
        goneCount = #unloadedNames
        gone = world.state("t_0_0")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "goneCount"), 3.0) << "the old cells went through the old unload function";
    EXPECT_EQ(globalString(f.vm, "gone"), "") << "and the old cells are no longer there";
    f.tick(2);
    ASSERT_TRUE(f.run("fresh = table.concat(newLoads, ',')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "fresh"), "new_0_0");
}

TEST(ScriptWorld, StopUnloadsEverythingAndReleaseForgetsWithoutCalling) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kRow) + "world.focus('p', Vec(5, 0, 5))", "level")) << lastError(f.vm);
    f.tick(2);
    EXPECT_TRUE(f.world.active());
    EXPECT_EQ(f.world.streamer().loaded().size(), 3u);
    ASSERT_TRUE(f.run("world.stop() n = #unloadedNames", "level")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "n", "level"), 3.0);
    EXPECT_FALSE(f.world.active());
    // another script's world: unloading the script drops it, and its functions are not called (they are gone)
    Fixture g;
    ASSERT_TRUE(g.run(std::string(kRow) + "world.focus('p', Vec(5, 0, 5))", "level")) << lastError(g.vm);
    g.tick(2);
    ASSERT_TRUE(g.run("calls = 0", "other"));
    g.vm.unload("other");
    EXPECT_TRUE(g.world.active()) << "another script leaving does not touch it";
    g.vm.unload("level");
    EXPECT_FALSE(g.world.active());
    EXPECT_EQ(g.world.streamer().cellCount(), 0u);
    g.vm.clearErrors();
    g.tick(3); // and nothing more happens (no calls into a script that is gone)
    EXPECT_TRUE(g.vm.errors().empty());
}

TEST(ScriptWorld, MistakesInTheDescriptionComeBackWithAReason) {
    Fixture f;
    auto fails = [&](const std::string& call, const std::string& expect) {
        f.vm.clearErrors();
        const std::string code = "ok, why = world.stream" + call;
        ASSERT_TRUE(f.run(code)) << code << " " << lastError(f.vm);
        EXPECT_FALSE(globalBool(f.vm, "ok")) << call;
        EXPECT_NE(globalString(f.vm, "why").find(expect), std::string::npos) << "'" << globalString(f.vm, "why") << "' should say '" << expect << "'";
        EXPECT_FALSE(f.world.active());
    };
    fails("{}", "no cells");
    fails("{ cells = {} }", "no cells");
    fails("{ cells = 5 }", "cells is a list");
    fails("{ cells = { { min = {0, 0}, max = {1, 1} } } }", "cells[0] needs a name");
    fails("{ cells = { { name = 'a', min = {0, 0} } } }", "needs min and max");
    fails("{ cells = { { name = 'a', min = {0, 0}, max = {0, 5} } } }", "no area");
    fails("{ cells = { { name = 'a', min = {0, 0}, max = {5, 5} }, { name = 'a', min = {9, 0}, max = {15, 5} } } }", "'a' already");
    fails("{ cells = { { name = 'a', min = {0, 0}, max = {5, 5}, loadRadius = 'far' } } }", "radii and bias are numbers");
    fails("{ grid = { size = 10 } }", "x = {first, last}");
    fails("{ grid = { size = 0, x = {0, 1}, z = {0, 1} } }", "size above 0");
    fails("{ grid = { size = 10, x = {3, 1}, z = {0, 1} } }", "before");
    fails("{ grid = { size = 10, x = {0, 100000}, z = {0, 100000} } }", "too many cells");
    fails("{ grid = { size = 10, x = {0, 1}, z = {0, 1} }, loadRadius = 0 }", "loadRadius must be above 0");
    fails("{ grid = { size = 10, x = {0, 1}, z = {0, 1} }, loadRadius = 'near' }", "numbers");
    EXPECT_FALSE(f.run("world.stream(5)"));
}
