#include "kke/FrameStats.h"

#include <gtest/gtest.h>

using kke::FrameStats;

TEST(FrameStats, SteadyFramesGiveTheSameAverageAndLow) {
    FrameStats s;
    s.beginPhase("walk");
    for (int i = 0; i < 200; ++i) s.add({ 10.0, 4.0, 1.0, 5 });
    FrameStats::Summary o = s.overall();
    EXPECT_EQ(o.frames, 200);
    EXPECT_NEAR(o.fpsAvg, 100.0, 1e-9);
    EXPECT_NEAR(o.low1Fps, 100.0, 1e-9);
    EXPECT_NEAR(o.seconds, 2.0, 1e-9);
    EXPECT_NEAR(o.gpuAvgMs, 4.0, 1e-9);
    EXPECT_EQ(o.bodiesMax, 5);
    EXPECT_STREQ(FrameStats::verdict(o), "smooth");
}

TEST(FrameStats, OneLowIsTheSlowestPercent) {
    FrameStats s;
    for (int i = 0; i < 99; ++i) s.add({ 10.0, -1.0, 0.0, 0 });
    s.add({ 100.0, -1.0, 0.0, 0 }); // one hitch in 100 frames
    FrameStats::Summary o = s.overall();
    EXPECT_NEAR(o.low1Fps, 10.0, 1e-9);
    EXPECT_NEAR(o.frameMaxMs, 100.0, 1e-9);
    EXPECT_LT(o.gpuAvgMs, 0.0); // never reported
    EXPECT_STREQ(FrameStats::verdict(o), "too slow");
    // The average barely notices the hitch; that's why the low exists.
    EXPECT_GT(o.fpsAvg, 90.0);
}

TEST(FrameStats, PhasesAndWindowsAreSeparate) {
    FrameStats s;
    s.beginPhase("a");
    for (int i = 0; i < 150; ++i) s.add({ 10.0, 1.0, 0.5, 1 }); // 1.5 s
    s.beginPhase("b");
    for (int i = 0; i < 40; ++i) s.add({ 25.0, 1.0, 2.0, 300 }); // 1.0 s
    auto p = s.phases();
    ASSERT_EQ(p.size(), 2u);
    EXPECT_NEAR(p[0].fpsAvg, 100.0, 1e-9);
    EXPECT_NEAR(p[1].fpsAvg, 40.0, 1e-9);
    EXPECT_STREQ(FrameStats::verdict(p[1]), "playable");
    EXPECT_EQ(p[1].bodiesMax, 300);
    auto w = s.windows();
    ASSERT_EQ(w.size(), 3u); // a: 1 s + 0.5 s, b: 1 s
    EXPECT_EQ(w[0][1], 0.0);
    EXPECT_EQ(w[2][1], 1.0);
    EXPECT_NEAR(w[2][0], 2.5, 1e-9);
    EXPECT_EQ(w[0].size(), FrameStats::windowColumns().size());

    kke::BenchmarkReport r;
    s.fill(r);
    bool found = false;
    for (const auto& [k, v] : r.results)
        if (k == "b.fps_avg") found = std::abs(v - 40.0) < 1e-9;
    EXPECT_TRUE(found);
    EXPECT_NE(r.toText().find("Verdict: "), std::string::npos);
}
