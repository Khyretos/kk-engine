#include "kke/BenchRecorder.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using kke::BenchRecorder;

namespace {

// Feeds `frames` frames of `ms` each, starting at time t; returns the new t.
double feed(BenchRecorder& r, int frames, double ms, double t0, double gpuMs = 2.0) {
    for (int i = 0; i < frames; ++i) {
        const double t = t0 + i * ms / 1000.0;
        r.beginFrame(t, 1.7e12 + t * 1000.0);
        r.addStage(BenchRecorder::Update, ms * 0.25);
        r.addStage(BenchRecorder::Record, ms * 0.5);
        r.addStage(BenchRecorder::Present, ms * 0.25);
        r.addModule(0, "update", ms * 0.2);
        r.setFrameInfo(gpuMs, 1, 2);
        r.endFrame(ms, 100.0);
    }
    return t0 + frames * ms / 1000.0;
}

BenchRecorder make(double seconds, double warmup) {
    kke::BenchOptions o;
    o.seconds = seconds;
    o.warmup = warmup;
    BenchRecorder r(o);
    r.setModules({ "Game", "UI" }, { 12.0, 3.0 });
    return r;
}

} // namespace

TEST(BenchRecorder, WarmupIsNotMeasuredAndItEndsOnTime) {
    BenchRecorder r = make(2.0, 1.0);
    double t = feed(r, 100, 10.0, 0.0); // the 1 s warm-up
    EXPECT_FALSE(r.measuring() && r.measuredFrames() > 0);
    EXPECT_EQ(r.measuredFrames(), 0);
    t = feed(r, 150, 10.0, t);
    EXPECT_EQ(r.measuredFrames(), 150);
    EXPECT_FALSE(r.done());
    feed(r, 60, 10.0, t);
    EXPECT_TRUE(r.done());
    EXPECT_NEAR(r.measuredFrames(), 200, 1); // frames after the end aren't counted
    const nlohmann::json j = r.toJson({ { "cpu", "test" } }, { { "game", "t" } }, {});
    EXPECT_NEAR(j["summary"]["fps_avg"].get<double>(), 100.0, 1e-6);
    EXPECT_NEAR(j["summary"]["seconds"].get<double>(), 2.0, 0.011);
    EXPECT_EQ(j["summary"]["hitches"]["count"].get<int>(), 0);
    EXPECT_EQ(j["summary"]["verdict"], "smooth");
    EXPECT_EQ(j["system"]["cpu"], "test");
    EXPECT_GE(j["per_second"]["rows"].size(), 2u);
    EXPECT_EQ(j["modules"][0]["name"], "Game"); // the busiest first
    EXPECT_NEAR(j["modules"][0]["init_ms"].get<double>(), 12.0, 1e-9);
}

TEST(BenchRecorder, AHitchIsFoundWithItsCause) {
    BenchRecorder r = make(10.0, 0.0);
    double t = feed(r, 100, 10.0, 0.0);
    // One slow frame: the GPU wait grew, with an event in the same frame.
    r.beginFrame(t, 1.7e12);
    r.addStage(BenchRecorder::Update, 2.5);
    r.addStage(BenchRecorder::Record, 5.0);
    r.addStage(BenchRecorder::GpuWait, 70.0);
    r.addStage(BenchRecorder::Present, 2.5);
    r.addEvent("window lost focus");
    r.setFrameInfo(60.0, 1, 2);
    r.endFrame(80.0, 140.0);
    t += 0.08;
    feed(r, 100, 10.0, t);

    ASSERT_EQ(r.hitchCount(), 1);
    const BenchRecorder::Hitch& h = r.hitches().front();
    EXPECT_NEAR(h.frameMs, 80.0, 1e-9);
    EXPECT_NEAR(h.medianMs, 10.0, 1e-9);
    EXPECT_STREQ(h.severity(), "major");
    EXPECT_NE(h.cause.find("gpu_wait"), std::string::npos) << h.cause;
    EXPECT_NE(h.cause.find("window lost focus"), std::string::npos) << h.cause;
    EXPECT_NEAR(h.residentDeltaMb, 40.0, 1e-9);
    const nlohmann::json j = r.toJson({}, {}, { "Physics update(): boom" });
    EXPECT_EQ(j["summary"]["hitches"]["major"].get<int>(), 1);
    EXPECT_NEAR(j["summary"]["hitches"]["time_lost_ms"].get<double>(), 70.0, 1e-9);
    EXPECT_EQ(j["hitches"][0]["events"][0], "window lost focus");
    EXPECT_EQ(j["broken_modules"][0], "Physics update(): boom");
    EXPECT_EQ(j["events"].size(), 1u);
}

TEST(BenchRecorder, SteadySlowFramesAreNotHitchesButFallingBehindIsCounted) {
    BenchRecorder r = make(10.0, 0.0);
    double t = feed(r, 50, 40.0, 0.0); // 25 fps, every frame alike
    for (int i = 0; i < 30; ++i) {
        r.beginFrame(t, 0.0);
        r.setFrameInfo(-1.0, 2, 2); // the fixed-step cap: the simulation can't keep up
        r.endFrame(40.0, 0.0);
        t += 0.04;
    }
    EXPECT_EQ(r.hitchCount(), 0);
    const nlohmann::json j = r.toJson({}, {}, {});
    EXPECT_EQ(j["summary"]["sim_behind_frames"].get<int>(), 30);
    EXPECT_EQ(j["summary"]["verdict"], "struggles");
    EXPECT_NEAR(j["summary"]["memory_mb"]["peak"].get<double>(), 100.0, 1e-9); // 0 = keep the last sample
}

TEST(BenchRecorder, ModuleCostIsPerFrameAndItsWorstCallIsNamed) {
    BenchRecorder r = make(10.0, 0.0);
    double t = 0.0;
    for (int i = 0; i < 100; ++i) {
        r.beginFrame(t, 0.0);
        r.addModule(1, "renderUi", 1.0);
        r.addModule(1, "renderOverlay", i == 50 ? 9.0 : 1.0);
        r.endFrame(10.0, 0.0);
        t += 0.01;
    }
    const nlohmann::json j = r.toJson({}, {}, {});
    const nlohmann::json& ui = j["modules"][0];
    EXPECT_EQ(ui["name"], "UI");
    EXPECT_NEAR(ui["avg_ms_per_frame"].get<double>(), 2.08, 1e-9);
    EXPECT_NEAR(ui["worst_frame_ms"].get<double>(), 10.0, 1e-9);
    EXPECT_NEAR(ui["worst_call_ms"].get<double>(), 9.0, 1e-9);
    EXPECT_EQ(ui["worst_call"], "renderOverlay");
}

TEST(BenchRecorder, PicksTheMomentsWorthAScreenshot) {
    BenchRecorder r = make(10.0, 0.0);
    double t = feed(r, 300, 10.0, 0.0); // 3 s at 100 fps
    t = feed(r, 1, 120.0, t);          // the worst frame
    t = feed(r, 100, 20.0, t);         // 2 slow seconds at 50 fps
    t = feed(r, 500, 10.0, t);         // back to 100 fps to the end
    std::vector<BenchRecorder::ShotRequest> shots = r.takeShotRequests();
    auto last = [&](const std::string& kind) {
        const BenchRecorder::ShotRequest* found = nullptr;
        for (const auto& s : shots)
            if (s.kind == kind) found = &s;
        return found;
    };
    ASSERT_NE(last("worst-frame"), nullptr);
    EXPECT_NEAR(last("worst-frame")->value, 120.0, 1e-9);
    EXPECT_EQ(last("worst-frame")->label, "worst-frame_120ms_at-3.0s");
    ASSERT_NE(last("slowest-second"), nullptr);
    EXPECT_NEAR(last("slowest-second")->value, 45.0, 1.0); // the 120 ms frame and 44 of 20 ms
    ASSERT_NE(last("fastest-second"), nullptr);
    EXPECT_NEAR(last("fastest-second")->value, 100.0, 3.0);
    for (const char* view : { "view-1-of-3", "view-2-of-3", "view-3-of-3" }) EXPECT_NE(last(view), nullptr) << view;
    EXPECT_LE(static_cast<int>(shots.size()), BenchRecorder::kMaxShotRequests);
    EXPECT_TRUE(r.takeShotRequests().empty()); // handed over once

    r.shotSaved(*last("worst-frame"), "shots/x_worst-frame_120ms_at-3.0s.jpg");
    r.shotSaved(*last("view-1-of-3"), "shots/x_view-1-of-3_at-1.0s.jpg");
    r.shotSaved(*last("worst-frame"), "shots/x_worst-frame_120ms_at-3.0s.jpg"); // a kind is listed once
    const nlohmann::json j = r.toJson({}, {}, {});
    ASSERT_EQ(j["screenshots"].size(), 2u);
    EXPECT_EQ(j["screenshots"][0]["kind"], "worst-frame");
    EXPECT_EQ(j["screenshots"][0]["file"], "shots/x_worst-frame_120ms_at-3.0s.jpg");
}

TEST(BenchRecorder, AScreenshotFrameIsNotMeasured) {
    BenchRecorder r = make(10.0, 0.0);
    double t = feed(r, 200, 10.0, 0.0);
    r.beginFrame(t, 1.7e12 + t * 1000.0);
    r.excludeFrame(); // this one copied a screenshot
    r.endFrame(200.0, 100.0);
    t += 0.2;
    feed(r, 200, 10.0, t);
    EXPECT_EQ(r.measuredFrames(), 400);
    EXPECT_EQ(r.excludedFrames(), 1);
    EXPECT_EQ(r.hitchCount(), 0);
    const nlohmann::json j = r.toJson({}, {}, {});
    EXPECT_EQ(j["summary"]["screenshot_frames"].get<int>(), 1);
    EXPECT_NEAR(j["summary"]["frame_ms"]["max"].get<double>(), 10.0, 1e-9);
}
