#include "kke/Profiler.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// The profiler (docs/PROFILING.md): zones and counters into a capture, the
// capture into a Chrome Trace file Perfetto opens, rolling per-module
// times for the Performance panel.

namespace {

using kke::Profiler;

void spin(double ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(ms);
    while (std::chrono::steady_clock::now() < until) {
    }
}

nlohmann::json parse(const std::string& text) {
    return nlohmann::json::parse(text);
}

} // namespace

TEST(Profiler, ZonesOutsideACaptureAreFreeAndRecordNothing) {
    Profiler p;
    const uint32_t id = p.intern("idle");
    EXPECT_FALSE(p.capturing());
    {
        Profiler::Scope s(id, p);
        spin(0.2);
    }
    p.counter(id, 1.0);
    ASSERT_TRUE(p.startCapture({}));
    ASSERT_TRUE(p.stopCapture());
    EXPECT_TRUE(p.summary().empty());
}

TEST(Profiler, NamesAreInternedOnce) {
    Profiler p;
    const uint32_t a = p.intern("Physics.update");
    EXPECT_EQ(p.intern("Physics.update"), a);
    EXPECT_NE(p.intern("Audio.update"), a);
    EXPECT_EQ(p.name(a), "Physics.update");
    EXPECT_EQ(p.name(9999), "?");
    // A module call by its two literals: the same id every time, "Module.stage".
    const char* mod = "Physics";
    const char* stage = "update";
    EXPECT_EQ(p.moduleZone(mod, stage), a);
    EXPECT_EQ(p.moduleZone(mod, stage), a);
}

TEST(Profiler, ACaptureHoldsNestedZonesInOrder) {
    Profiler p;
    const uint32_t outer = p.intern("outer"), inner = p.intern("inner");
    ASSERT_TRUE(p.startCapture({}));
    {
        Profiler::Scope o(outer, p);
        spin(0.3);
        {
            Profiler::Scope i(inner, p);
            spin(0.3);
        }
        spin(0.3);
    }
    ASSERT_TRUE(p.stopCapture());
    const auto zones = p.summary();
    ASSERT_EQ(zones.size(), 2u);
    EXPECT_EQ(zones[0].name, "outer"); // longest first
    EXPECT_EQ(zones[1].name, "inner");
    EXPECT_GE(zones[0].totalMs, zones[1].totalMs);
    EXPECT_GE(zones[1].totalMs, 0.25);
    EXPECT_EQ(zones[0].calls, 1u);

    // In the file the outer zone starts first and contains the inner one.
    const auto trace = parse(p.traceJson());
    double outerTs = -1, outerDur = 0, innerTs = -1, innerDur = 0;
    for (const auto& e : trace["traceEvents"]) {
        if (e.value("ph", "") != "X") continue;
        if (e["name"] == "outer") { outerTs = e["ts"]; outerDur = e["dur"]; }
        if (e["name"] == "inner") { innerTs = e["ts"]; innerDur = e["dur"]; }
    }
    ASSERT_GE(outerTs, 0.0);
    ASSERT_GE(innerTs, 0.0);
    EXPECT_LE(outerTs, innerTs);
    EXPECT_GE(outerTs + outerDur, innerTs + innerDur - 0.01);
}

TEST(Profiler, TheTraceIsValidChromeTraceJsonWithASummary) {
    Profiler p;
    const uint32_t z = p.intern("zone \"quoted\"\\path"), c = p.intern("GPU frame ms");
    ASSERT_TRUE(p.startCapture({}));
    { Profiler::Scope s(z, p); spin(0.1); }
    p.counter(c, 4.5);
    p.endFrame(16.0);
    ASSERT_TRUE(p.stopCapture());
    const auto trace = parse(p.traceJson()); // throws if it isn't JSON
    ASSERT_TRUE(trace.contains("traceEvents"));
    EXPECT_EQ(trace["displayTimeUnit"], "ms");
    bool sawName = false, sawCounter = false, sawFrame = false, sawProcess = false, sawThread = false;
    for (const auto& e : trace["traceEvents"]) {
        const std::string ph = e.value("ph", "");
        if (ph == "X" && e["name"] == "zone \"quoted\"\\path") sawName = true;
        if (ph == "C" && e["name"] == "GPU frame ms") { sawCounter = true; EXPECT_DOUBLE_EQ(e["args"]["GPU frame ms"].get<double>(), 4.5); }
        if (ph == "i" && e["name"] == "frame") { sawFrame = true; EXPECT_DOUBLE_EQ(e["args"]["ms"].get<double>(), 16.0); }
        if (ph == "M" && e["name"] == "process_name") sawProcess = true;
        if (ph == "M" && e["name"] == "thread_name") sawThread = true;
    }
    EXPECT_TRUE(sawName && sawCounter && sawFrame && sawProcess && sawThread);
    ASSERT_TRUE(trace["kkeSummary"].is_array());
    ASSERT_EQ(trace["kkeSummary"].size(), 1u);
    EXPECT_EQ(trace["kkeSummary"][0]["calls"], 1);
    EXPECT_EQ(trace["kkeCapture"]["frames"], 1);
}

TEST(Profiler, EveryThreadGetsItsOwnNamedTrack) {
    Profiler p;
    p.nameThisThread("main");
    const uint32_t id = p.intern("work");
    ASSERT_TRUE(p.startCapture({}));
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back([&, t] {
            p.nameThisThread("worker " + std::to_string(t));
            for (int i = 0; i < 50; ++i) {
                Profiler::Scope s(id, p);
                spin(0.01);
            }
        });
    { Profiler::Scope s(id, p); }
    for (auto& w : workers) w.join();
    ASSERT_TRUE(p.stopCapture());
    EXPECT_EQ(p.summary()[0].calls, 201u);
    const auto trace = parse(p.traceJson());
    std::set<int> tids;
    std::set<std::string> names;
    for (const auto& e : trace["traceEvents"]) {
        if (e.value("ph", "") == "X") tids.insert(e["tid"].get<int>());
        if (e.value("ph", "") == "M" && e["name"] == "thread_name") names.insert(e["args"]["name"].get<std::string>());
    }
    EXPECT_EQ(tids.size(), 5u);
    EXPECT_TRUE(names.count("main"));
    EXPECT_TRUE(names.count("worker 0") && names.count("worker 3"));
}

TEST(Profiler, ACaptureStopsAfterItsFramesAndWritesItsFile) {
    namespace fs = std::filesystem;
    Profiler p;
    const fs::path path = fs::temp_directory_path() / "kke_profiler_test.json";
    fs::remove(path);
    Profiler::CaptureOptions o;
    o.path = path.string();
    o.frames = 3;
    ASSERT_TRUE(p.startCapture(o));
    std::string error;
    EXPECT_FALSE(p.startCapture(o, &error)); // one at a time
    EXPECT_FALSE(error.empty());
    for (int f = 0; f < 5; ++f) p.endFrame(10.0);
    EXPECT_FALSE(p.capturing());
    EXPECT_EQ(p.lastCapturePath(), path.string());
    ASSERT_TRUE(fs::exists(path));
    EXPECT_EQ(parse(p.traceJson())["kkeCapture"]["frames"], 3);
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_EQ(text.str(), p.traceJson());
    fs::remove(path);
    EXPECT_FALSE(p.stopCapture(&error)); // none running
}

TEST(Profiler, ACaptureStopsAfterItsSeconds) {
    Profiler p;
    Profiler::CaptureOptions o;
    o.seconds = 0.02;
    ASSERT_TRUE(p.startCapture(o));
    spin(30.0);
    p.endFrame(30.0);
    EXPECT_FALSE(p.capturing());
}

TEST(Profiler, AnUnwritableFileIsAnErrorNotACrash) {
    Profiler p;
    Profiler::CaptureOptions o;
    o.path = "/nonexistent-folder/for/sure/trace.json";
    ASSERT_TRUE(p.startCapture(o));
    std::string error;
    EXPECT_FALSE(p.stopCapture(&error));
    EXPECT_NE(error.find("can't write"), std::string::npos);
    EXPECT_FALSE(p.capturing());
    EXPECT_NO_THROW(parse(p.traceJson())); // still kept in memory
}

TEST(Profiler, EventsPastTheLimitAreDroppedAndCounted) {
    Profiler p;
    const uint32_t id = p.intern("many");
    Profiler::CaptureOptions o;
    o.maxEvents = 100;
    ASSERT_TRUE(p.startCapture(o));
    for (int i = 0; i < 250; ++i) { Profiler::Scope s(id, p); }
    const auto state = p.captureState();
    EXPECT_TRUE(state.active);
    EXPECT_EQ(state.events, 100u);
    EXPECT_EQ(state.dropped, 150u);
    ASSERT_TRUE(p.stopCapture());
    EXPECT_EQ(p.summary()[0].calls, 100u);
    EXPECT_EQ(parse(p.traceJson())["kkeCapture"]["dropped"], 150);
}

TEST(Profiler, ASecondCaptureStartsClean) {
    Profiler p;
    const uint32_t id = p.intern("once");
    ASSERT_TRUE(p.startCapture({}));
    { Profiler::Scope s(id, p); }
    ASSERT_TRUE(p.stopCapture());
    ASSERT_TRUE(p.startCapture({}));
    ASSERT_TRUE(p.stopCapture());
    EXPECT_TRUE(p.summary().empty());
}

TEST(Profiler, ModuleTimesRollOverTheLastWindow) {
    Profiler p;
    const uint32_t slow = p.moduleZone("Physics", "update"), fast = p.moduleZone("Audio", "update");
    for (int f = 0; f < 10; ++f) {
        p.addModuleTime(slow, 4.0);
        p.addModuleTime(slow, 2.0); // two calls in a frame add up
        p.addModuleTime(fast, 0.5);
        p.endFrame(10.0);
    }
    p.addModuleTime(slow, 20.0); // a spike
    p.endFrame(30.0);
    const auto stats = p.moduleStats();
    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats[0].name, "Physics.update"); // slowest first
    EXPECT_NEAR(stats[0].maxMs, 20.0, 1e-9);
    EXPECT_NEAR(stats[0].lastMs, 20.0, 1e-9);
    EXPECT_NEAR(stats[0].avgMs, (6.0 * 10 + 20.0) / 11.0, 1e-9);
    EXPECT_NEAR(stats[1].avgMs, 0.5 * 10 / 11.0, 1e-9);
    EXPECT_NEAR(p.worstFrameMs(), 30.0, 1e-9);
    EXPECT_NEAR(p.averageFrameMs(), (10.0 * 10 + 30.0) / 11.0, 1e-9);
    // Older frames fall out of the window.
    for (int f = 0; f < Profiler::kWindow; ++f) p.endFrame(1.0);
    EXPECT_NEAR(p.moduleStats()[0].maxMs, 0.0, 1e-9);
    EXPECT_NEAR(p.worstFrameMs(), 1.0, 1e-9);
    EXPECT_EQ(p.moduleStats(1).size(), 1u);
}

TEST(Profiler, ModuleTimesBecomeZonesWhileCapturing) {
    Profiler p;
    const uint32_t id = p.moduleZone("Render", "render");
    ASSERT_TRUE(p.startCapture({}));
    spin(3.0); // a call that began before the capture is not part of it: this one began after
    p.addModuleTime(id, 2.5);
    p.endFrame(16.0);
    ASSERT_TRUE(p.stopCapture());
    const auto zones = p.summary();
    ASSERT_EQ(zones.size(), 1u);
    EXPECT_EQ(zones[0].name, "Render.render");
    EXPECT_NEAR(zones[0].totalMs, 2.5, 1e-6);
}

TEST(Profiler, TheCounterSourceRunsOncePerCapturedFrame) {
    Profiler p;
    const uint32_t gpu = p.intern("GPU frame ms");
    int calls = 0;
    p.setCounterSource([&](Profiler& pr) {
        ++calls;
        pr.counter(gpu, 7.0);
    });
    p.endFrame(10.0); // not capturing: not asked
    EXPECT_EQ(calls, 0);
    ASSERT_TRUE(p.startCapture({}));
    p.endFrame(10.0);
    p.endFrame(10.0);
    ASSERT_TRUE(p.stopCapture());
    EXPECT_EQ(calls, 2);
    int counters = 0;
    const auto trace = parse(p.traceJson());
    for (const auto& e : trace["traceEvents"])
        if (e.value("ph", "") == "C") ++counters;
    EXPECT_EQ(counters, 2);
}

TEST(Profiler, TheMacroInternsOnceAndRecords) {
    Profiler& g = Profiler::get();
    ASSERT_TRUE(g.startCapture({}));
    for (int i = 0; i < 3; ++i) {
        KKE_PROFILE_SCOPE("test.macro");
        spin(0.05);
    }
    ASSERT_TRUE(g.stopCapture());
    const auto zones = g.summary();
    ASSERT_EQ(zones.size(), 1u);
    EXPECT_EQ(zones[0].name, "test.macro");
    EXPECT_EQ(zones[0].calls, 3u);
}
