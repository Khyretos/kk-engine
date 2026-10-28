// timeline.* in Lua (kke/ScriptTimeline.h, ENG-F27).
#include "kke/ScriptTimeline.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <filesystem>
#include <fstream>

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
    ScriptTimeline tl;
    explicit Fixture(const std::string& folder = {}) : tl(folder) {
        tl.bind(vm);
        vm.onUnload = [this](const std::string& source) { tl.release(source); };
    }
    bool run(const std::string& code, const std::string& source = "console") { return vm.runString(code, source); }
    void tick(float dt) { tl.update(dt, vm); }
};

const char* kDoor = R"(
    log = {}
    door = timeline.create{
        duration = 2,
        tracks = { { target = "door", property = "height", keys = { { t = 0, v = 0 }, { t = 2, v = 10 } } } },
        events = { { t = 0.5, name = "creak", data = "wood" }, { t = 1.5, name = "slam" } },
    }
)";

} // namespace

TEST(ScriptTimeline, PlaysAndReportsEventsAndValues) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kDoor) + R"(
        heights = {}
        names = {}
        hook.Add("TimelineEvent", "t", function(e) names[#names + 1] = e.name .. ":" .. e.data .. "@" .. e.time end)
        timeline.bind(door, "door", function(property, value) heights[#heights + 1] = string.format("%s=%g", property, value) end)
        done = false
        hook.Add("TimelineDone", "t", function(e) done = e.timeline == door end)
        timeline.play(door)
    )")) << lastError(f.vm);
    for (int i = 0; i < 9; ++i) f.tick(0.25f);
    ASSERT_TRUE(f.run("n = #names h = #heights first = names[1] second = names[2] lastHeight = heights[#heights] t = timeline.time(door) p = timeline.playing(door) fin = timeline.finished(door)")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "n"), 2.0);
    EXPECT_EQ(globalString(f.vm, "first"), "creak:wood@0.5");
    EXPECT_EQ(globalString(f.vm, "second"), "slam:@1.5");
    EXPECT_GE(globalNumber(f.vm, "h"), 8.0) << "a value every step";
    EXPECT_EQ(globalString(f.vm, "lastHeight"), "height=10");
    EXPECT_TRUE(globalBool(f.vm, "done"));
    EXPECT_EQ(globalNumber(f.vm, "t"), 2.0);
    EXPECT_FALSE(globalBool(f.vm, "p"));
    EXPECT_TRUE(globalBool(f.vm, "fin"));
}

TEST(ScriptTimeline, CallsBackAtTheNextFrameNotInsideTheCall) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kDoor) + R"(
        calls = 0
        timeline.bind(door, "door", function() calls = calls + 1 end)
        timeline.seek(door, 1)
        right_after = calls
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "right_after"), 0.0) << "queued: a script is never called back from inside its own call";
    f.tick(0.0f);
    ASSERT_TRUE(f.run("after = calls"));
    EXPECT_EQ(globalNumber(f.vm, "after"), 1.0) << "delivered at the next update, even a paused timeline's seek";
}

TEST(ScriptTimeline, ValuesCarryTheirKindToTheBoundFunction) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tl = timeline.create{
            tracks = {
                { target = "lamp", property = "intensity", keys = { { t = 0, v = 1 }, { t = 1, v = 3 } } },
                { target = "lamp", property = "position", kind = "vec3", keys = { { t = 0, v = Vec(0, 0, 0) }, { t = 1, v = Vec(2, 4, 6) } } },
                { target = "lamp", property = "color", kind = "color", keys = { { t = 0, v = { 1, 0, 0 } }, { t = 1, v = { 0, 0, 1, 0.5 } } } },
                { target = "lamp", property = "rotation", kind = "quat", keys = { { t = 0, v = { 0, 0, 0, 1 } }, { t = 1, v = { 0, 0, 0, 1 } } } },
            } }
        got = {}
        timeline.bind(tl, "lamp", function(property, value) got[property] = value end)
        timeline.seek(tl, 0.5)
    )")) << lastError(f.vm);
    f.tick(0.0f);
    ASSERT_TRUE(f.run(R"(
        intensity = got.intensity
        px, py, pz = got.position.x, got.position.y, got.position.z
        r, g, b, a = got.color.r, got.color.g, got.color.b, got.color.a
        qw = got.rotation.w
    )")) << lastError(f.vm);
    EXPECT_NEAR(globalNumber(f.vm, "intensity"), 2.0, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "px"), 1.0, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "py"), 2.0, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "pz"), 3.0, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "r"), 0.5, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "b"), 0.5, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "a"), 0.75, 1e-5);
    EXPECT_NEAR(globalNumber(f.vm, "qw"), 1.0, 1e-5);
}

TEST(ScriptTimeline, ABindingCanBeForOneProperty) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tl = timeline.create{ tracks = {
            { target = "a", property = "x", keys = { { t = 0, v = 1 } } },
            { target = "a", property = "y", keys = { { t = 0, v = 2 } } } } }
        onlyX, any = 0, 0
        timeline.bind(tl, "a", "x", function() onlyX = onlyX + 1 end)
        timeline.seek(tl, 0)
    )")) << lastError(f.vm);
    f.tick(0.0f);
    ASSERT_TRUE(f.run(R"(
        timeline.bind(tl, "a", function() any = any + 1 end)
        timeline.unbind(tl, "a", "x")
        timeline.seek(tl, 0)
    )")) << lastError(f.vm);
    f.tick(0.0f);
    ASSERT_TRUE(f.run("a1, a2 = onlyX, any"));
    EXPECT_EQ(globalNumber(f.vm, "a1"), 1.0);
    EXPECT_EQ(globalNumber(f.vm, "a2"), 2.0) << "a target-wide binding hears both properties after the x one was removed";
}

TEST(ScriptTimeline, SpansAndClipsReachTheHooks) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tl = timeline.create{ duration = 4,
            spans = { { target = "torch", start = 0.5, ["end"] = 1.5 } },
            clips = { { target = "hero", clip = "Walk", start = 1, ["end"] = 3, loop = true, speed = 2, blend = 0.4 } } }
        seen = {}
        hook.Add("TimelineActive", "t", function(e) seen[#seen + 1] = "active:" .. e.target .. ":" .. tostring(e.on) end)
        hook.Add("TimelineClip", "t", function(e) seen[#seen + 1] = string.format("clip:%s:%s:%s@%g%s:x%g:b%.1f", e.kind, e.target, e.clip, e.time, e.loop and ":loop" or "", e.speed, e.blend) end)
        timeline.play(tl)
    )")) << lastError(f.vm);
    for (int i = 0; i < 20; ++i) f.tick(0.25f);
    ASSERT_TRUE(f.run("all = table.concat(seen, ' ')")) << lastError(f.vm);
    const std::string all = globalString(f.vm, "all");
    const size_t on = all.find("active:torch:true"), off = all.find("active:torch:false");
    const size_t start = all.find("clip:start:hero:Walk@0:loop:x2:b0.4"), stop = all.find("clip:stop:hero:Walk@3.5:loop:x2:b0.4");
    EXPECT_NE(on, std::string::npos) << all;
    EXPECT_NE(off, std::string::npos) << all;
    EXPECT_NE(start, std::string::npos) << all;
    EXPECT_NE(stop, std::string::npos) << all;
    EXPECT_LT(on, start) << "the torch (0.5 s) comes on before the clip (1 s) starts";
    EXPECT_LT(start, off) << "and the clip starts before the torch goes off (1.5 s)";
    EXPECT_LT(off, stop);
}

TEST(ScriptTimeline, MarkersAndLoops) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tl = timeline.create{ duration = 2, wrap = "loop",
            markers = { { t = 0.5, name = "intro" }, { t = 1.5, name = "outro" } } }
        marks, loops = {}, {}
        hook.Add("TimelineMarker", "t", function(e) marks[#marks + 1] = e.name end)
        hook.Add("TimelineLoop", "t", function(e) loops[#loops + 1] = e.count end)
        timeline.play(tl)
    )")) << lastError(f.vm);
    for (int i = 0; i < 12; ++i) f.tick(0.5f);
    ASSERT_TRUE(f.run(R"(
        nm, nl = #marks, #loops
        lastLoop = loops[#loops]
        found = timeline.seekToMarker(tl, "outro")
        t1 = timeline.time(tl)
        missing = timeline.seekToMarker(tl, "nowhere")
        lp = timeline.loops(tl)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "nm"), 6.0) << "two markers in each of three laps";
    EXPECT_EQ(globalNumber(f.vm, "nl"), 2.0);
    EXPECT_EQ(globalNumber(f.vm, "lastLoop"), 2.0);
    EXPECT_TRUE(globalBool(f.vm, "found"));
    EXPECT_EQ(globalNumber(f.vm, "t1"), 1.5);
    EXPECT_FALSE(globalBool(f.vm, "missing"));
    EXPECT_EQ(globalNumber(f.vm, "lp"), 2.0);
}

TEST(ScriptTimeline, PlayOptionsSpeedPauseAndStop) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kDoor) + R"(
        timeline.play(door, { from = 1, speed = 2 })
        s1 = timeline.speed(door)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "s1"), 2.0);
    f.tick(0.25f);
    ASSERT_TRUE(f.run("t1 = timeline.time(door) timeline.pause(door)")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "t1"), 1.5);
    f.tick(1.0f);
    ASSERT_TRUE(f.run("t2 = timeline.time(door) timeline.resume(door)")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "t2"), 1.5) << "paused";
    f.tick(0.125f);
    ASSERT_TRUE(f.run("t3 = timeline.time(door) timeline.speed(door, -1) timeline.stop(door) t4 = timeline.time(door) pl = timeline.playing(door)")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "t3"), 1.75);
    EXPECT_EQ(globalNumber(f.vm, "t4"), 0.0);
    EXPECT_FALSE(globalBool(f.vm, "pl"));
    EXPECT_FALSE(f.run("timeline.play(door, { wrap = 'sideways' })"));
    EXPECT_NE(lastError(f.vm).find("wrap is"), std::string::npos) << lastError(f.vm);
}

TEST(ScriptTimeline, ExplainsAMistakeWithItsPath) {
    Fixture f;
    EXPECT_FALSE(f.run("timeline.create{ tracks = { { target = 'a', property = 'x', keys = { { v = 1 } } } } }"));
    EXPECT_NE(lastError(f.vm).find("timeline.create: timeline.tracks[0].keys[0]: needs t"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("timeline.create{ wrap = 'bouncy' }"));
    EXPECT_NE(lastError(f.vm).find("wrap is"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("timeline.play(99)"));
    EXPECT_NE(lastError(f.vm).find("no timeline 99"), std::string::npos) << lastError(f.vm);
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("timeline.create('a')"));
    f.vm.clearErrors();
    EXPECT_FALSE(f.run("tl = timeline.create{} timeline.seek(tl, 0 / 0)"));
    EXPECT_NE(lastError(f.vm).find("must be a number"), std::string::npos) << lastError(f.vm);
    // an empty one is fine: empty Lua tables are empty lists
    f.vm.clearErrors();
    EXPECT_TRUE(f.run("e = timeline.create{ tracks = {}, events = {}, spans = {} }")) << lastError(f.vm);
}

TEST(ScriptTimeline, LoadsAFileInsideTheGamesFolderOnly) {
    const std::string dir = ::testing::TempDir() + "kke_timeline_load";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/cutscenes");
    std::ofstream(dir + "/cutscenes/intro.json") << R"({ "duration": 3, "events": [ { "t": 1, "name": "hello" } ] })";
    std::ofstream(dir + "/cutscenes/bad.json") << R"({ "events": [ { "name": "no time" } ] })";
    std::ofstream(dir + "/cutscenes/broken.json") << "{ nope";
    std::ofstream(dir + "/../kke_outside_tl.json") << R"({ "duration": 1 })";
    Fixture f(dir);
    ASSERT_TRUE(f.run(R"(
        intro = timeline.load("cutscenes/intro.json")
        dur = timeline.duration(intro)
        bad, whyBad = timeline.load("cutscenes/bad.json")
        broken, whyBroken = timeline.load("cutscenes/broken.json")
        outside, whyOutside = timeline.load("../kke_outside_tl.json")
        abs, whyAbs = timeline.load("/etc/passwd")
        missing, whyMissing = timeline.load("nope.json")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "dur"), 3.0);
    EXPECT_FALSE(globalBool(f.vm, "bad"));
    EXPECT_NE(globalString(f.vm, "whyBad").find("events[0]: needs t"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyBroken").find("not valid JSON"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyOutside").find("inside the game's folder"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyAbs").find("inside the game's folder"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyMissing").find("can't open"), std::string::npos);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(::testing::TempDir() + "kke_outside_tl.json");
}

TEST(ScriptTimeline, TimelinesFollowTheirScriptsAndCanBeDestroyed) {
    Fixture f;
    ASSERT_TRUE(f.run("a = timeline.create{} b = timeline.create{} n = timeline.count()", "one"));
    ASSERT_TRUE(f.run("c = timeline.create{ duration = 5 }", "two"));
    EXPECT_EQ(globalNumber(f.vm, "n", "one"), 2.0);
    EXPECT_EQ(f.tl.count(), 3u);
    f.vm.unload("one");
    EXPECT_EQ(f.tl.count(), 1u) << "unloading a script frees its timelines";
    ASSERT_TRUE(f.run("gone = timeline.create{} timeline.destroy(gone) alive = timeline.exists(gone) js = timeline.json(c)", "two"));
    EXPECT_FALSE(globalBool(f.vm, "alive", "two"));
    EXPECT_NE(globalString(f.vm, "js", "two").find("\"duration\": 5"), std::string::npos);
    EXPECT_EQ(f.tl.count(), 1u);
    EXPECT_FALSE(f.run("timeline.play(gone)", "two"));
    EXPECT_NE(lastError(f.vm).find("no timeline"), std::string::npos);
}

TEST(ScriptTimeline, TheNumberOfTimelinesIsLimited) {
    Fixture f;
    EXPECT_FALSE(f.run("for i = 1, 300 do timeline.create{} end"));
    EXPECT_NE(lastError(f.vm).find("256 timelines already"), std::string::npos) << lastError(f.vm);
    EXPECT_EQ(f.tl.count(), ScriptTimeline::kMaxTimelines);
}

TEST(ScriptTimeline, NestedTimelinesInLua) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        tl = timeline.create{ duration = 4, subs = {
            { start = 1, speed = 2, timeline = {
                tracks = { { target = "door", property = "height", keys = { { t = 0, v = 0 }, { t = 2, v = 8 } } } },
                events = { { t = 1, name = "inner" } } } } } }
        h, ev = nil, {}
        timeline.bind(tl, "door", function(p, v) h = v end)
        hook.Add("TimelineEvent", "t", function(e) ev[#ev + 1] = e.name end)
        timeline.play(tl)
    )")) << lastError(f.vm);
    for (int i = 0; i < 6; ++i) f.tick(0.25f);
    ASSERT_TRUE(f.run("height, count = h, #ev")) << lastError(f.vm);
    EXPECT_NEAR(globalNumber(f.vm, "height"), 4.0, 1e-4) << "0.5 s into the nested one at double speed: a second of its own";
    EXPECT_EQ(globalNumber(f.vm, "count"), 1.0);
}
