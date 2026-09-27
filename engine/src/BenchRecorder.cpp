#include "kke/BenchRecorder.h"
#include "kke/BenchmarkReport.h"
#include "kke/FrameStats.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>

namespace kke {

namespace {

// SDL_getenv, not std::getenv: UTF-8 on every OS, so a results folder
// under a user name like "José" survives the trip from the launcher.
double envNumber(const char* name, double fallback) {
    const char* v = SDL_getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    return end != v && std::isfinite(d) ? d : fallback;
}

// Which Stage a module lifecycle call belongs to (Application::run()).
BenchRecorder::Stage stageOfCall(const char* call) {
    auto is = [call](const char* s) { return std::strcmp(call, s) == 0; };
    if (is("onEvent")) return BenchRecorder::Events;
    if (is("frameStart") || is("fixedUpdate")) return BenchRecorder::Simulate;
    if (is("update")) return BenchRecorder::Update;
    if (is("frameEnd")) return BenchRecorder::Idle;
    return BenchRecorder::Record; // renderUi, compute, renderShadow, prepass, render, renderOverlay
}

// "2026-09-27 08:30:12.345" in local time: the log's own time stamp format.
std::string localTime(double epochMs) {
    const std::time_t secs = static_cast<std::time_t>(epochMs / 1000.0);
    const int ms = static_cast<int>(std::fmod(epochMs, 1000.0));
    char buf[40] = "unknown";
    if (const std::tm* tm = std::localtime(&secs)) {
        char date[32];
        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", tm);
        std::snprintf(buf, sizeof(buf), "%s.%03d", date, ms);
    }
    return buf;
}

double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

nlohmann::json stats(std::vector<double> v) {
    if (v.empty()) return nullptr;
    double sum = 0.0;
    for (double x : v) sum += x;
    return { { "avg", round3(sum / v.size()) }, { "p50", round3(percentile(v, 50)) }, { "p95", round3(percentile(v, 95)) },
             { "p99", round3(percentile(v, 99)) }, { "max", round3(*std::max_element(v.begin(), v.end())) } };
}

// Frames per second over the slowest `fraction` of frames (their mean time).
double lowFps(std::vector<double> ms, double fraction) {
    if (ms.empty()) return 0.0;
    std::sort(ms.begin(), ms.end(), std::greater<double>());
    const size_t n = std::max<size_t>(1, static_cast<size_t>(ms.size() * fraction));
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) total += ms[i];
    return total > 0.0 ? 1000.0 * n / total : 0.0;
}

} // namespace

const char* BenchRecorder::stageName(int stage) {
    static const char* const kNames[kStageCount] = { "events", "simulate", "update", "gpu_wait", "record", "present", "idle" };
    return stage >= 0 && stage < kStageCount ? kNames[stage] : "?";
}

std::optional<BenchRecorder::Options> BenchRecorder::fromEnvironment() {
    const char* v = SDL_getenv("KKE_BENCHMARK");
    if (!v || !*v || std::strcmp(v, "0") == 0) return std::nullopt;
    Options o;
    const double seconds = envNumber("KKE_BENCHMARK", o.seconds);
    if (seconds > 1.0) o.seconds = seconds; // "1" = on, default length
    o.warmup = std::max(0.0, envNumber("KKE_BENCH_WARMUP", o.warmup));
    if (const char* d = SDL_getenv("KKE_BENCH_DIR"); d && *d) o.dir = d;
    if (const char* n = SDL_getenv("KKE_BENCH_NAME"); n && *n) o.name = n;
    if (const char* s = SDL_getenv("KKE_BENCH_VSYNC"); s && *s && std::strcmp(s, "0") != 0) o.uncapped = false;
    if (const char* s = SDL_getenv("KKE_BENCH_SHOTS"); s && std::strcmp(s, "0") == 0) o.screenshots = false;
    return o;
}

BenchRecorder::BenchRecorder(Options options) : m_options(std::move(options)) { m_recent.reserve(61); }

void BenchRecorder::setModules(std::vector<std::string> names, std::vector<double> initMs) {
    m_moduleNames = std::move(names);
    m_moduleInitMs = std::move(initMs);
    m_moduleInitMs.resize(m_moduleNames.size(), 0.0);
    m_moduleTotals.assign(m_moduleNames.size(), {});
    m_moduleFrameMs.assign(m_moduleNames.size(), 0.0);
    m_moduleFramePeak.assign(m_moduleNames.size(), 0.0);
}

void BenchRecorder::beginFrame(double tSeconds, double epochMs) {
    m_frame.t = tSeconds;
    m_frame.epochMs = epochMs;
    m_frame.stageMs.fill(0.0);
    m_frame.modules.clear();
    m_frame.events.clear();
    m_frame.gpuMs = -1.0;
    m_frame.fixedSteps = m_frame.maxFixedSteps = 0;
    if (m_measureStart < 0.0 && tSeconds >= m_options.warmup) m_measureStart = tSeconds;
}

void BenchRecorder::addModule(int module, const char* stage, double ms) {
    if (module < 0 || module >= static_cast<int>(m_moduleNames.size())) return;
    m_frame.modules.push_back({ module, stage, ms });
}

void BenchRecorder::addEvent(const std::string& what) {
    m_frame.events.push_back(what);
    ++m_eventCount;
    if (m_events.size() < kMaxEventsKept) m_events.push_back({ m_measureStart >= 0.0 ? m_frame.t - m_measureStart : m_frame.t - m_options.warmup, what });
}

void BenchRecorder::setFrameInfo(double gpuMs, int fixedSteps, int maxFixedSteps) {
    m_frame.gpuMs = gpuMs;
    m_frame.fixedSteps = fixedSteps;
    m_frame.maxFixedSteps = maxFixedSteps;
}

double BenchRecorder::rollingMedian() const {
    if (m_recent.empty()) return 0.0;
    std::vector<double> v = m_recent;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

void BenchRecorder::endFrame(double wallMs, double residentMb) {
    if (residentMb <= 0.0) residentMb = m_lastResidentMb;
    const double median = rollingMedian();
    const bool excluded = m_excludeFrame;
    m_excludeFrame = false;
    const bool measured = m_measureStart >= 0.0 && !done() && !excluded;
    if (excluded && m_measureStart >= 0.0 && !done()) ++m_excludedFrames;
    m_lastT = m_frame.t + wallMs / 1000.0;

    if (measured) {
        const size_t n = m_frameMs.size();
        const bool hitch = m_recent.size() >= 10 && wallMs > median * m_options.hitchFactor && wallMs > median + m_options.hitchMinMs;

        m_frameMs.push_back(wallMs);
        m_frameT.push_back(m_frame.t - m_measureStart);
        m_gpuMs.push_back(m_frame.gpuMs);
        std::array<float, kStageCount> st{};
        for (int s = 0; s < kStageCount; ++s) st[s] = static_cast<float>(m_frame.stageMs[s]);
        m_stageMs.push_back(st);
        m_residentMb.push_back(static_cast<float>(residentMb));
        m_isHitch.push_back(hitch ? 1 : 0);
        if (m_frame.maxFixedSteps > 0 && m_frame.fixedSteps >= m_frame.maxFixedSteps) ++m_simBehindFrames;
        if (m_residentStartMb < 0.0) m_residentStartMb = residentMb;
        m_residentPeakMb = std::max(m_residentPeakMb, residentMb);
        m_residentEndMb = residentMb;

        std::fill(m_moduleFrameMs.begin(), m_moduleFrameMs.end(), 0.0);
        for (const ModuleTime& m : m_frame.modules) {
            ModuleTotal& t = m_moduleTotals[m.module];
            t.totalMs += m.ms;
            if (m.ms > t.maxMs) {
                t.maxMs = m.ms;
                t.maxStage = m.stage;
            }
            m_moduleFrameMs[m.module] += m.ms;
        }
        for (size_t i = 0; i < m_moduleFrameMs.size(); ++i) m_moduleFramePeak[i] = std::max(m_moduleFramePeak[i], m_moduleFrameMs[i]);

        if (hitch) {
            ++m_hitchCount;
            m_hitchExtraMs += wallMs - median;
            Hitch h;
            h.tSeconds = m_frame.t - m_measureStart;
            h.epochMs = m_frame.epochMs;
            h.frameMs = wallMs;
            h.medianMs = median;
            h.stageMs = m_frame.stageMs;
            h.gpuMs = m_frame.gpuMs;
            h.fixedSteps = m_frame.fixedSteps;
            h.residentMb = residentMb;
            h.residentDeltaMb = residentMb - m_lastResidentMb;
            h.events = m_frame.events;
            ++m_hitchBySeverity[wallMs < 50.0 ? 0 : wallMs < 250.0 ? 1 : 2];

            std::vector<ModuleTime> calls = m_frame.modules;
            std::sort(calls.begin(), calls.end(), [](const ModuleTime& a, const ModuleTime& b) { return a.ms > b.ms; });
            if (calls.size() > 3) calls.resize(3);
            h.topModules = calls;

            // The stage that grew most over its usual time.
            int worst = -1;
            double worstExtra = 0.0, accounted = 0.0;
            for (int s = 0; s < kStageCount; ++s) {
                accounted += m_frame.stageMs[s];
                const double usual = n ? m_stageTotal[s] / n : 0.0;
                const double extra = m_frame.stageMs[s] - usual;
                if (extra > worstExtra) {
                    worstExtra = extra;
                    worst = s;
                }
            }
            char buf[256];
            const double unaccounted = wallMs - accounted;
            if (worst < 0 || unaccounted > worstExtra) {
                std::snprintf(buf, sizeof(buf), "%.1f ms outside the engine's own work (the OS paused the game, or the window was busy)",
                              unaccounted);
                h.cause = buf;
            } else {
                const double usual = n ? m_stageTotal[worst] / n : 0.0;
                std::snprintf(buf, sizeof(buf), "%s took %.1f ms (usually %.1f ms)", stageName(worst), m_frame.stageMs[worst], usual);
                h.cause = buf;
                if (worst == GpuWait) h.cause += ": the GPU was behind";
                // The module call that dominated that stage, if one did.
                const ModuleTime* top = nullptr;
                for (const ModuleTime& m : m_frame.modules)
                    if (stageOfCall(m.stage) == worst && (!top || m.ms > top->ms)) top = &m;
                if (top && top->ms > 0.5 * m_frame.stageMs[worst]) {
                    std::snprintf(buf, sizeof(buf), ", mostly %s %s() %.1f ms", m_moduleNames[top->module].c_str(), top->stage, top->ms);
                    h.cause += buf;
                }
            }
            if (!h.events.empty()) h.cause += "; also this frame: " + h.events.front();
            if (m_hitches.size() < kMaxHitchesKept) m_hitches.push_back(std::move(h));
        }
        for (int s = 0; s < kStageCount; ++s) m_stageTotal[s] += m_frame.stageMs[s];
        if (m_options.screenshots) pickShots(m_frame.t - m_measureStart, wallMs);
    }
    if (excluded) {
        m_lastResidentMb = residentMb;
        return; // a screenshot frame says nothing about the game's usual pace
    }

    // Every frame (warm-up too) primes the median, hitches included: a
    // lasting slow-down becomes the new normal after a second or so.
    constexpr size_t kWindow = 61;
    if (m_recent.size() < kWindow) m_recent.push_back(wallMs);
    else m_recent[m_recentNext] = wallMs;
    m_recentNext = (m_recentNext + 1) % kWindow;
    m_lastResidentMb = residentMb;
}

// Called for each measured frame. tMeasured: when it began, in seconds
// since measuring started.
void BenchRecorder::pickShots(double tMeasured, double wallMs) {
    auto request = [&](const std::string& kind, const char* valueFmt, double value, double at) {
        if (m_shotRequestCount >= kMaxShotRequests) return;
        ++m_shotRequestCount;
        char buf[96];
        std::string label = kind;
        if (valueFmt) {
            std::snprintf(buf, sizeof(buf), valueFmt, value);
            label += "_";
            label += buf;
        }
        std::snprintf(buf, sizeof(buf), "_at-%.1fs", at);
        label += buf;
        m_shotRequests.push_back({ kind, label, at, value });
    };
    // The slowest frame: each new record at least 10% worse than the last
    // picture, at most one a second (the first second sets records often).
    if (wallMs > m_shotWorstMs * 1.1 && tMeasured - m_shotWorstAt >= 1.0) {
        m_shotWorstMs = wallMs;
        m_shotWorstAt = tMeasured;
        request("worst-frame", "%.0fms", wallMs, tMeasured);
    }
    // Whole seconds: the slowest and the fastest so far (3% apart from the
    // last picture of that kind, so a steady game isn't pictured twice a second).
    const int second = static_cast<int>(tMeasured);
    if (second != m_second) {
        const double span = tMeasured - m_secondStart;
        if (m_secondFrames > 0 && span > 0.5) {
            const double fps = m_secondFrames / span;
            const double at = tMeasured; // the picture is of the frame right after that second
            if ((m_shotSlowFps <= 0.0 || fps < m_shotSlowFps * 0.97) && tMeasured - m_shotSlowAt >= 1.0) {
                m_shotSlowFps = fps;
                m_shotSlowAt = tMeasured;
                request("slowest-second", "%.0ffps", fps, at);
            }
            if (fps > m_shotFastFps * 1.03 && tMeasured - m_shotFastAt >= 1.0) {
                m_shotFastFps = fps;
                m_shotFastAt = tMeasured;
                request("fastest-second", "%.0ffps", fps, at);
            }
        }
        m_second = second;
        m_secondStart = tMeasured;
        m_secondFrames = 0;
    }
    ++m_secondFrames;
    // Three views spread over the run: what the demo looks like.
    static constexpr double kViewAt[3] = { 0.1, 0.5, 0.9 };
    if (m_viewsTaken < 3 && tMeasured >= kViewAt[m_viewsTaken] * m_options.seconds) {
        ++m_viewsTaken;
        request("view-" + std::to_string(m_viewsTaken) + "-of-3", nullptr, 0.0, tMeasured);
    }
}

std::vector<BenchRecorder::ShotRequest> BenchRecorder::takeShotRequests() {
    std::vector<ShotRequest> out;
    out.swap(m_shotRequests);
    return out;
}

void BenchRecorder::shotSaved(const ShotRequest& shot, const std::string& file) {
    for (auto& [s, f] : m_shots) {
        if (s.kind == shot.kind) {
            s = shot;
            f = file;
            return;
        }
    }
    m_shots.emplace_back(shot, file);
}

const char* BenchRecorder::Hitch::severity() const {
    if (frameMs < 50.0) return "minor";
    if (frameMs < 250.0) return "major";
    return "freeze";
}

nlohmann::json BenchRecorder::toJson(const std::vector<std::pair<std::string, std::string>>& system,
                                     const std::vector<std::pair<std::string, std::string>>& config,
                                     const std::vector<std::string>& brokenModules) const {
    using nlohmann::json;
    json j;
    j["format"] = "kke-benchmark-game/1";
    j["system"] = json::object();
    for (const auto& [k, v] : system) j["system"][k] = v;
    j["config"] = json::object();
    for (const auto& [k, v] : config) j["config"][k] = v;
    j["config"]["measure_s"] = m_options.seconds;
    j["config"]["warmup_s"] = m_options.warmup;
    j["config"]["uncapped"] = m_options.uncapped;
    j["config"]["hitch_rule"] = "frame > " + std::to_string(m_options.hitchFactor).substr(0, 4) + " x median of the last 61 frames and > median + " +
                                std::to_string(static_cast<int>(m_options.hitchMinMs)) + " ms";
    j["broken_modules"] = brokenModules;
    j["load_s"] = m_loadSeconds >= 0.0 ? json(round3(m_loadSeconds)) : json(nullptr);

    // --- Summary -----------------------------------------------------------
    json s;
    double total = 0.0;
    for (double ms : m_frameMs) total += ms;
    s["frames"] = m_frameMs.size();
    s["screenshot_frames"] = m_excludedFrames; // not measured: they copied or read back a screenshot
    s["seconds"] = round3(total / 1000.0);
    s["fps_avg"] = round3(total > 0.0 ? 1000.0 * m_frameMs.size() / total : 0.0);
    s["fps_1pct_low"] = round3(lowFps(m_frameMs, 0.01));
    s["fps_0_1pct_low"] = round3(lowFps(m_frameMs, 0.001));
    s["frame_ms"] = stats(m_frameMs);
    std::vector<double> gpu;
    for (double g : m_gpuMs)
        if (g >= 0.0) gpu.push_back(g);
    s["gpu_ms"] = stats(gpu);
    FrameStats::Summary fs;
    fs.frames = static_cast<int>(m_frameMs.size());
    fs.low1Fps = s["fps_1pct_low"].get<double>();
    s["verdict"] = FrameStats::verdict(fs);
    s["sim_behind_frames"] = m_simBehindFrames;
    s["memory_mb"] = { { "start", round3(std::max(0.0, m_residentStartMb)) }, { "peak", round3(m_residentPeakMb) }, { "end", round3(m_residentEndMb) } };
    const double minutes = total / 60000.0;
    s["hitches"] = { { "count", m_hitchCount },
                     { "minor", m_hitchBySeverity[0] },
                     { "major", m_hitchBySeverity[1] },
                     { "freeze", m_hitchBySeverity[2] },
                     { "per_minute", round3(minutes > 0.0 ? m_hitchCount / minutes : 0.0) },
                     { "time_lost_ms", round3(m_hitchExtraMs) } };
    j["summary"] = s;

    // --- Where the time goes -------------------------------------------------
    json stages = json::object();
    for (int st = 0; st < kStageCount; ++st) {
        std::vector<double> v;
        v.reserve(m_stageMs.size());
        for (const auto& f : m_stageMs) v.push_back(f[st]);
        stages[stageName(st)] = stats(v);
    }
    j["stages_ms"] = stages;

    std::vector<size_t> order(m_moduleNames.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return m_moduleTotals[a].totalMs > m_moduleTotals[b].totalMs; });
    json modules = json::array();
    const double frames = static_cast<double>(std::max<size_t>(1, m_frameMs.size()));
    for (size_t i : order) {
        const ModuleTotal& t = m_moduleTotals[i];
        modules.push_back({ { "name", m_moduleNames[i] },
                            { "avg_ms_per_frame", round3(t.totalMs / frames) },
                            { "worst_frame_ms", round3(m_moduleFramePeak[i]) },
                            { "worst_call_ms", round3(t.maxMs) },
                            { "worst_call", t.maxStage },
                            { "init_ms", round3(m_moduleInitMs[i]) } });
    }
    j["modules"] = modules;

    // --- Per second ------------------------------------------------------------
    json cols = { "t_s", "fps", "frame_avg_ms", "frame_max_ms", "gpu_avg_ms" };
    for (int st = 0; st < kStageCount; ++st) cols.push_back(std::string(stageName(st)) + "_avg_ms");
    cols.push_back("rss_mb");
    cols.push_back("hitches");
    json rows = json::array();
    size_t i = 0;
    while (i < m_frameMs.size()) {
        const double second = std::floor(m_frameT[i]);
        double sum = 0.0, max = 0.0, gpuSum = 0.0;
        int gpuN = 0, hitches = 0;
        std::array<double, kStageCount> stSum{};
        size_t j0 = i;
        for (; i < m_frameMs.size() && std::floor(m_frameT[i]) == second; ++i) {
            sum += m_frameMs[i];
            max = std::max(max, m_frameMs[i]);
            if (m_gpuMs[i] >= 0.0) {
                gpuSum += m_gpuMs[i];
                ++gpuN;
            }
            for (int st = 0; st < kStageCount; ++st) stSum[st] += m_stageMs[i][st];
            hitches += m_isHitch[i];
        }
        const double n = static_cast<double>(i - j0);
        json row = { second, round3(sum > 0.0 ? 1000.0 * n / sum : 0.0), round3(sum / n), round3(max), gpuN ? json(round3(gpuSum / gpuN)) : json(nullptr) };
        for (int st = 0; st < kStageCount; ++st) row.push_back(round3(stSum[st] / n));
        row.push_back(round3(m_residentMb[i - 1]));
        row.push_back(hitches);
        rows.push_back(row);
    }
    j["per_second"] = { { "columns", cols }, { "rows", rows } };

    // --- Hitches and events --------------------------------------------------
    json hitches = json::array();
    for (const Hitch& h : m_hitches) {
        json st = json::object();
        for (int k = 0; k < kStageCount; ++k) st[stageName(k)] = round3(h.stageMs[k]);
        json top = json::array();
        for (const ModuleTime& m : h.topModules) top.push_back({ { "module", m_moduleNames[m.module] }, { "call", m.stage }, { "ms", round3(m.ms) } });
        hitches.push_back({ { "t_s", round3(h.tSeconds) },
                            { "time", localTime(h.epochMs) },
                            { "frame_ms", round3(h.frameMs) },
                            { "median_ms", round3(h.medianMs) },
                            { "severity", h.severity() },
                            { "cause", h.cause },
                            { "stages_ms", st },
                            { "top_calls", top },
                            { "gpu_ms", h.gpuMs >= 0.0 ? json(round3(h.gpuMs)) : json(nullptr) },
                            { "fixed_steps", h.fixedSteps },
                            { "rss_mb", round3(h.residentMb) },
                            { "rss_delta_mb", round3(h.residentDeltaMb) },
                            { "events", h.events } });
    }
    j["hitches"] = hitches;
    j["hitches_not_listed"] = m_hitchCount - static_cast<int>(m_hitches.size());
    json events = json::array();
    for (const Event& e : m_events) events.push_back({ { "t_s", round3(e.t) }, { "what", e.what } });
    j["events"] = events;
    json shots = json::array();
    for (const auto& [shot, file] : m_shots) shots.push_back({ { "kind", shot.kind }, { "file", file }, { "t_s", std::round(shot.tSeconds * 10.0) / 10.0 }, { "value", std::round(shot.value * 10.0) / 10.0 } });
    j["screenshots"] = shots;
    j["events_not_listed"] = m_eventCount - static_cast<int>(m_events.size());
    return j;
}

} // namespace kke
