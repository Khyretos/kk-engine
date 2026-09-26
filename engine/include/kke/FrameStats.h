#pragma once

#include "kke/BenchmarkReport.h"

#include <string>
#include <vector>

namespace kke {

// Frame timing for the end-user stress test (ACTION_PLAN.md 1.7): every
// frame's time, split into named phases ("walk", "crates", "breaking"),
// summarized the way players compare machines: average fps, 1% low,
// percentiles, worst frame, and a one-word verdict. Pure data, unit-
// tested in tests/test_frame_stats.cpp; the scenario lives in the game.
class FrameStats {
public:
    struct Frame {
        double frameMs = 0.0;   // wall time since the previous frame
        double gpuMs = -1.0;    // Renderer::lastGpuFrameTimeMs (-1 = unknown)
        double physicsMs = 0.0; // physics step time behind this frame
        int bodies = 0;         // simulated objects alive
    };
    struct Summary {
        std::string name;
        int frames = 0;
        double seconds = 0.0;
        double fpsAvg = 0.0;
        double low1Fps = 0.0; // fps of the slowest 1% of frames (their mean time)
        double frameAvgMs = 0.0, frameP50Ms = 0.0, frameP95Ms = 0.0, frameP99Ms = 0.0, frameMaxMs = 0.0;
        double gpuAvgMs = -1.0;
        double physicsAvgMs = 0.0, physicsMaxMs = 0.0;
        int bodiesMax = 0;
    };

    // Frames added after this belong to `name`.
    void beginPhase(const std::string& name);
    void add(const Frame& f);
    void clear();

    std::vector<Summary> phases() const;
    Summary overall() const;
    // One row per second of frames: t_s, phase, fps, frame_avg_ms,
    // frame_max_ms, gpu_avg_ms, physics_avg_ms, bodies.
    std::vector<std::vector<double>> windows() const;
    static std::vector<std::string> windowColumns();

    // "smooth" (1% low >= 55 fps), "playable" (>= 30), "struggles" (>= 15),
    // "too slow" below that.
    static const char* verdict(const Summary& s);

    // Results (overall and per phase), per-second samples and notes into
    // a report; the caller adds system info and config.
    void fill(BenchmarkReport& report) const;

private:
    static Summary summarize(const std::string& name, const std::vector<Frame>& frames);
    std::vector<std::string> m_names;
    std::vector<std::vector<Frame>> m_frames; // per phase
};

} // namespace kke
