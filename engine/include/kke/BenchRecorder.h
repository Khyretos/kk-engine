#pragma once

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// How a benchmark run is set up (BenchRecorder::fromEnvironment()).
struct BenchOptions {
    double seconds = 20.0;  // measured time, after the warm-up
    double warmup = 3.0;    // first seconds not measured (pipelines, first-use uploads)
    std::string dir;        // where the report goes ("" = the default benchmark folder)
    std::string name;       // report file name without extension ("" = <game>_<stamp>)
    bool uncapped = true;   // vsync off, no frame cap: measure what the machine can do
    // A hitch is a frame slower than both hitchFactor x the median of
    // the last second's frames and that median + hitchMinMs.
    double hitchFactor = 2.0;
    double hitchMinMs = 8.0;
    // Pictures of the best and worst moments (KKE_BENCH_SHOTS=0: none).
    bool screenshots = true;
};

// The per-game half of the benchmark (docs/BENCHMARKS.md "Benchmark for
// everyone"): any game started with KKE_BENCHMARK=<seconds> records every
// frame, split into the stages of Application::run() and into what each
// module cost, finds the hitches and what was going on in each one, then
// writes one JSON report and quits. tools/kke_benchmark runs every demo
// this way and joins the reports into the one file people send back.
//
// This class is pure data and maths (unit-tested in
// tests/test_bench_recorder.cpp); Application feeds it timings.
class BenchRecorder {
public:
    // Where a frame's wall time went, in the order Application::run() does it.
    enum Stage : int {
        Events,    // pollEvents + every module's onEvent
        Simulate,  // frameStart + the fixedUpdate ticks
        Update,    // every module's update()
        GpuWait,   // Renderer::beginFrame: waiting for a free frame (the GPU is behind) + acquiring the image
        Record,    // building the frame: renderUi, compute, shadows, prepass, render, overlay, UI
        Present,   // Renderer::endFrame: submit + present (blocks with vsync)
        Idle,      // frameEnd + the frame-rate limiter's sleep
        kStageCount
    };
    static const char* stageName(int stage);

    using Options = BenchOptions;
    // KKE_BENCHMARK=<seconds> (1 = the default 20), KKE_BENCH_WARMUP,
    // KKE_BENCH_DIR, KKE_BENCH_NAME, KKE_BENCH_VSYNC=1 (keep the game's
    // vsync and cap). Nothing set: nullopt. Read with SDL_getenv (UTF-8),
    // not dev::env: a player can run the benchmark in a shipping build.
    static std::optional<Options> fromEnvironment();

    struct ModuleTime {
        int module = -1;
        const char* stage = ""; // the lifecycle call ("update", "render", ...)
        double ms = 0.0;
    };

    explicit BenchRecorder(Options options);

    const Options& options() const { return m_options; }

    // Called once before the main loop: which modules run, in init order,
    // and how long each one's init() took.
    void setModules(std::vector<std::string> names, std::vector<double> initMs);
    // Seconds from the process starting (Application's constructor) to
    // the first frame: the loading time a player waits through.
    void setLoadSeconds(double s) { m_loadSeconds = s; }

    // --- One frame ---------------------------------------------------------
    // A frame runs from one pass of the main loop to the next. tSeconds:
    // time since the main loop started. epochMs: wall-clock time now, in
    // ms since 1970 (to line hitches up with the log's time stamps).
    void beginFrame(double tSeconds, double epochMs);
    void addStage(Stage s, double ms) { m_frame.stageMs[s] += ms; }
    // One module lifecycle call ("update", "render", ...; a string literal).
    void addModule(int module, const char* stage, double ms);
    // Something that happened this frame, kept with a hitch if there is
    // one ("window lost focus", "module Physics disabled", ...), and in
    // the report's event list either way.
    void addEvent(const std::string& what);
    // gpuMs < 0 = unknown. fixedSteps: fixedUpdate ticks this frame
    // (hitting maxFixedSteps means the simulation can't keep up).
    void setFrameInfo(double gpuMs, int fixedSteps, int maxFixedSteps);
    // wallMs: the frame's whole time, loop pass to loop pass.
    // residentMb: platform::residentMemoryMb() (sampled; 0 = not sampled
    // this frame, keep the last value).
    void endFrame(double wallMs, double residentMb);

    // --- Screenshots --------------------------------------------------------
    // The recorder picks the moments worth a picture while it measures:
    // the slowest frame, the slowest and the fastest whole second, and
    // three views at 10%, 50% and 90% of the measured time. After
    // endFrame(), takeShotRequests() hands over the new ones; the game
    // captures its next frame for each (the scene a frame after the
    // moment) and, once written, reports the file with shotSaved(). A
    // later request of the same kind replaces an earlier one.
    struct ShotRequest {
        std::string kind;  // "worst-frame", "slowest-second", "fastest-second", "view-2-of-3"
        std::string label; // for the file name: "worst-frame_352ms_at-43.4s"
        double tSeconds = 0.0; // since measuring began
        double value = 0.0;    // ms (worst-frame), fps (the seconds), 0 (views)
    };
    std::vector<ShotRequest> takeShotRequests();
    // `file` relative to the report's folder ("shots/duel_view-1-of-3_at-2.0s.jpg").
    void shotSaved(const ShotRequest& shot, const std::string& file);
    // The frame now running copies or reads back a screenshot: it isn't
    // measured (nor counted in stats, hitches or the moments above).
    void excludeFrame() { m_excludeFrame = true; }
    int excludedFrames() const { return m_excludedFrames; }
    static constexpr int kMaxShotRequests = 40;

    // True while measuring (after the warm-up), and once the measured
    // time is over (the game should quit then).
    bool measuring() const { return m_measureStart >= 0.0 && !done(); }
    bool done() const { return m_measureStart >= 0.0 && m_lastT - m_measureStart >= m_options.seconds; }

    // --- Results -----------------------------------------------------------
    struct Hitch {
        double tSeconds = 0.0; // since measuring began
        double epochMs = 0.0;
        double frameMs = 0.0, medianMs = 0.0;
        std::array<double, kStageCount> stageMs{};
        std::vector<ModuleTime> topModules; // the frame's three most expensive calls
        double gpuMs = -1.0;
        int fixedSteps = 0;
        double residentMb = 0.0, residentDeltaMb = 0.0;
        std::vector<std::string> events;
        // What took the extra time, in words: the stage that grew most
        // over its usual time, and the module call that dominated it.
        std::string cause;
        const char* severity() const; // "minor" < 50 ms <= "major" < 250 ms <= "freeze"
    };
    const std::vector<Hitch>& hitches() const { return m_hitches; }
    int hitchCount() const { return m_hitchCount; }
    int measuredFrames() const { return static_cast<int>(m_frameMs.size()); }

    // The whole report (see docs/BENCHMARKS.md for the keys). `system` and
    // `config` are copied in as given (collectSystemInfo(), game name...).
    nlohmann::json toJson(const std::vector<std::pair<std::string, std::string>>& system,
                          const std::vector<std::pair<std::string, std::string>>& config,
                          const std::vector<std::string>& brokenModules) const;

    static constexpr size_t kMaxHitchesKept = 200;
    static constexpr size_t kMaxEventsKept = 200;

private:
    struct Frame {
        double t = 0.0, epochMs = 0.0;
        std::array<double, kStageCount> stageMs{};
        std::vector<ModuleTime> modules;
        std::vector<std::string> events;
        double gpuMs = -1.0;
        int fixedSteps = 0, maxFixedSteps = 0;
    };
    double rollingMedian() const;

    Options m_options;
    std::vector<std::string> m_moduleNames;
    std::vector<double> m_moduleInitMs;
    double m_loadSeconds = -1.0;

    Frame m_frame;
    double m_lastT = 0.0;
    double m_measureStart = -1.0;
    double m_lastResidentMb = 0.0;
    std::vector<double> m_recent; // last frames' wall ms, for the rolling median (ring)
    size_t m_recentNext = 0;

    // Measured frames only.
    std::vector<double> m_frameMs, m_gpuMs, m_frameT;
    std::vector<std::array<float, kStageCount>> m_stageMs;
    std::vector<float> m_residentMb;
    std::vector<uint8_t> m_isHitch;
    std::array<double, kStageCount> m_stageTotal{}; // over measured frames, for "usual" stage times
    int m_simBehindFrames = 0;
    double m_residentStartMb = -1.0, m_residentPeakMb = 0.0, m_residentEndMb = 0.0;
    struct ModuleTotal {
        double totalMs = 0.0, maxMs = 0.0;
        const char* maxStage = "";
    };
    std::vector<ModuleTotal> m_moduleTotals;
    std::vector<double> m_moduleFrameMs; // this frame's per-module sum (scratch)
    std::vector<double> m_moduleFramePeak; // worst per-frame sum per module
    std::vector<Hitch> m_hitches;
    int m_hitchCount = 0;
    std::array<int, 3> m_hitchBySeverity{};
    double m_hitchExtraMs = 0.0; // time spent above the median in hitches
    struct Event {
        double t;
        std::string what;
    };
    std::vector<Event> m_events;
    int m_eventCount = 0;

    // Screenshots.
    void pickShots(double tMeasured, double wallMs);
    bool m_excludeFrame = false;
    int m_excludedFrames = 0;
    std::vector<ShotRequest> m_shotRequests;
    int m_shotRequestCount = 0;
    double m_shotWorstMs = 0.0, m_shotWorstAt = -1e9;
    double m_shotSlowFps = 0.0, m_shotFastFps = 0.0, m_shotSlowAt = -1e9, m_shotFastAt = -1e9;
    int m_second = 0, m_secondFrames = 0; // the whole second being counted
    double m_secondStart = 0.0;
    int m_viewsTaken = 0;
    std::vector<std::pair<ShotRequest, std::string>> m_shots; // saved, one per kind
};

} // namespace kke
