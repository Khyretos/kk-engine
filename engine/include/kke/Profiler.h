#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kke {

// A runtime CPU/GPU/memory profiler with capture files (docs/PROFILING.md).
//
// Always on, almost free: Application times every module call (update,
// fixedUpdate, render, ...) and keeps a rolling average and worst case per
// module for the Performance panel. A *capture* records more, only while it
// runs: every KKE_PROFILE_SCOPE zone from every thread, and once a frame the
// frame time, GPU time and memory. It ends in one file that Perfetto
// (ui.perfetto.dev) and chrome://tracing open, with a summary table inside.
// Outside a capture a zone costs one relaxed atomic load.
//
// Pure data and timing: unit-tested in tests/test_profiler.cpp. Application
// feeds it from safeInvoke() and its frame loop.
class Profiler {
public:
    // The process-wide instance KKE_PROFILE_SCOPE and Application use.
    // Tests make their own.
    static Profiler& get();

    Profiler();
    ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // ---- names: a zone or counter is a number, made once per name
    uint32_t intern(std::string_view name);
    // A module's call, "Physics.update": the pointers are the literals
    // Module::name() and the stage argument return, so after the first call
    // this is one lookup and no string building.
    uint32_t moduleZone(const char* module, const char* stage);
    std::string name(uint32_t id) const;

    // ---- zones
    bool capturing() const { return m_capturing.load(std::memory_order_relaxed); }
    double nowUs() const;
    // Records a finished zone on the calling thread's track (a capture only).
    void zone(uint32_t id, double startUs, double durUs);
    // A value over time (a capture only): GPU ms, memory, a count.
    void counter(uint32_t id, double value);
    // Names the calling thread's track ("main", "physics worker 2").
    void nameThisThread(std::string_view name);

    // RAII zone. Prefer the macro, which interns the name once.
    class Scope {
    public:
        explicit Scope(uint32_t id, Profiler& p = Profiler::get()) : m_p(p), m_id(id) {
            if (m_p.capturing()) m_start = m_p.nowUs();
            else m_start = -1.0;
        }
        ~Scope() {
            if (m_start >= 0.0) m_p.zone(m_id, m_start, m_p.nowUs() - m_start);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Profiler& m_p;
        uint32_t m_id;
        double m_start;
    };

    // ---- the always-on module timings (main thread only)
    // Adds `ms` to the module call's total for this frame. Also a zone when
    // capturing, ending now.
    void addModuleTime(uint32_t id, double ms);
    // Ends the frame: folds this frame's module times into the rolling
    // stats, samples counters and counts a captured frame (stopping the
    // capture when its frames are done). `frameMs` is the frame's wall time.
    void endFrame(double frameMs);

    struct ModuleStat {
        std::string name;
        double avgMs = 0.0;  // over the last kWindow frames
        double maxMs = 0.0;  // worst frame in that window
        double lastMs = 0.0; // the latest frame
    };
    static constexpr int kWindow = 120;
    // Slowest first (by average), at most `limit`.
    std::vector<ModuleStat> moduleStats(size_t limit = 12) const;
    double averageFrameMs() const;
    double worstFrameMs() const;

    // ---- captures
    struct CaptureOptions {
        std::string path;         // the trace file (.json); "" = keep in memory (tests)
        int frames = 0;           // stop after this many frames; 0 = until stopCapture()
        double seconds = 0.0;     // or after this long (whichever comes first); 0 = no limit
        size_t maxEvents = 2000000; // across all threads; later ones are dropped and counted
    };
    // False (with `error`) when one is already running.
    bool startCapture(const CaptureOptions& options, std::string* error = nullptr);
    // Ends the capture and writes its file. False (with `error`) when none
    // runs or the file can't be written.
    bool stopCapture(std::string* error = nullptr);
    // Samples run once a frame while capturing (the GPU time, memory).
    void setCounterSource(std::function<void(Profiler&)> source);

    struct CaptureState {
        bool active = false;
        std::string path;
        double elapsedSeconds = 0.0;
        int frames = 0;
        size_t events = 0;
        size_t dropped = 0;
    };
    CaptureState captureState() const;
    // Where the last finished capture went ("" before the first).
    std::string lastCapturePath() const;
    std::string lastCaptureSummary() const;

    // ---- what a capture holds
    struct ZoneSummary {
        std::string name;
        uint64_t calls = 0;
        double totalMs = 0.0, avgMs = 0.0, p95Ms = 0.0, maxMs = 0.0;
    };
    // A fresh file name for a capture: profiles/profile-YYYYMMDD-HHMMSS.json
    // in the game's user data folder (the working folder when there is none).
    static std::string newCapturePath();

    // The trace as Chrome Trace Event JSON (what stopCapture writes), with
    // the per-zone summary under "kkeSummary". Empty before a capture.
    std::string traceJson() const;
    std::vector<ZoneSummary> summary() const; // by total time, longest first

private:
    struct Event {
        enum Kind : uint8_t { Zone, Counter, Frame };
        Kind kind;
        uint32_t id;
        uint32_t thread;
        double startUs;
        double durUs; // Zone: duration. Counter: the value. Frame: the frame's ms.
    };
    struct ThreadBuffer {
        std::mutex mutex;
        std::vector<Event> events;
        uint32_t thread = 0;
        std::string name;
    };
    ThreadBuffer& threadBuffer();
    void push(const Event& e);
    std::vector<Event> collect() const;
    std::string buildJson(const std::vector<Event>& events, double endUs) const;

    std::chrono::steady_clock::time_point m_epoch;
    uint64_t m_serial; // tells this profiler's thread caches from another's (tests make several)
    std::atomic<bool> m_capturing{ false };

    mutable std::mutex m_namesMutex;
    std::unordered_map<std::string, uint32_t> m_ids;
    std::vector<std::string> m_names;
    struct PairHash {
        size_t operator()(const std::pair<const void*, const void*>& p) const {
            return std::hash<const void*>()(p.first) * 31u + std::hash<const void*>()(p.second);
        }
    };
    std::unordered_map<std::pair<const void*, const void*>, uint32_t, PairHash> m_moduleIds;

    mutable std::mutex m_buffersMutex;
    std::vector<std::shared_ptr<ThreadBuffer>> m_buffers;
    std::unordered_map<std::thread::id, std::shared_ptr<ThreadBuffer>> m_byThread;
    std::atomic<uint32_t> m_nextThread{ 1 };

    // Rolling module stats (main thread).
    struct Rolling {
        double frameTotal = 0.0;
        double ring[kWindow] = {};
        int filled = 0, next = 0;
        double last = 0.0;
    };
    std::unordered_map<uint32_t, Rolling> m_rolling;
    double m_frameRing[kWindow] = {};
    int m_frameFilled = 0, m_frameNext = 0;

    // The capture.
    mutable std::mutex m_captureMutex;
    CaptureOptions m_options;
    std::atomic<double> m_captureStartUs{ 0.0 };
    std::atomic<size_t> m_maxEvents{ 2000000 };
    int m_capturedFrames = 0;
    std::atomic<size_t> m_eventCount{ 0 };
    std::atomic<size_t> m_dropped{ 0 };
    std::function<void(Profiler&)> m_counterSource;
    std::string m_lastPath;
    std::string m_lastSummary;
    std::string m_lastJson;
    std::vector<ZoneSummary> m_lastZones;
};

} // namespace kke

#define KKE_PROFILE_CONCAT_(a, b) a##b
#define KKE_PROFILE_CONCAT(a, b) KKE_PROFILE_CONCAT_(a, b)
// A zone round the rest of the enclosing block:
//   void Thing::update() { KKE_PROFILE_SCOPE("Thing.update"); ... }
// `name` must be a string literal (it is interned once, on first pass).
#define KKE_PROFILE_SCOPE(name)                                                                                        \
    static const uint32_t KKE_PROFILE_CONCAT(kkeProfileId_, __LINE__) = ::kke::Profiler::get().intern(name);           \
    ::kke::Profiler::Scope KKE_PROFILE_CONCAT(kkeProfileScope_, __LINE__)(KKE_PROFILE_CONCAT(kkeProfileId_, __LINE__))
