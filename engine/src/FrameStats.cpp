#include "kke/FrameStats.h"

#include <algorithm>
#include <functional>

namespace kke {

void FrameStats::beginPhase(const std::string& name) {
    m_names.push_back(name);
    m_frames.emplace_back();
}

void FrameStats::add(const Frame& f) {
    if (m_frames.empty()) beginPhase("run");
    m_frames.back().push_back(f);
}

void FrameStats::clear() {
    m_names.clear();
    m_frames.clear();
}

FrameStats::Summary FrameStats::summarize(const std::string& name, const std::vector<Frame>& frames) {
    Summary s;
    s.name = name;
    s.frames = static_cast<int>(frames.size());
    if (frames.empty()) return s;
    std::vector<double> ms;
    ms.reserve(frames.size());
    double gpuTotal = 0.0;
    int gpuCount = 0;
    for (const Frame& f : frames) {
        ms.push_back(f.frameMs);
        s.seconds += f.frameMs / 1000.0;
        if (f.gpuMs >= 0.0) {
            gpuTotal += f.gpuMs;
            ++gpuCount;
        }
        s.physicsAvgMs += f.physicsMs;
        s.physicsMaxMs = std::max(s.physicsMaxMs, f.physicsMs);
        s.bodiesMax = std::max(s.bodiesMax, f.bodies);
    }
    s.physicsAvgMs /= frames.size();
    if (gpuCount) s.gpuAvgMs = gpuTotal / gpuCount;
    s.frameAvgMs = s.seconds * 1000.0 / frames.size();
    s.fpsAvg = s.seconds > 0.0 ? frames.size() / s.seconds : 0.0;
    s.frameP50Ms = percentile(ms, 50);
    s.frameP95Ms = percentile(ms, 95);
    s.frameP99Ms = percentile(ms, 99);
    std::sort(ms.begin(), ms.end(), std::greater<double>());
    s.frameMaxMs = ms.front();
    const size_t worst = std::max<size_t>(1, ms.size() / 100);
    double worstTotal = 0.0;
    for (size_t i = 0; i < worst; ++i) worstTotal += ms[i];
    s.low1Fps = worstTotal > 0.0 ? 1000.0 * worst / worstTotal : 0.0;
    return s;
}

std::vector<FrameStats::Summary> FrameStats::phases() const {
    std::vector<Summary> out;
    for (size_t i = 0; i < m_frames.size(); ++i) out.push_back(summarize(m_names[i], m_frames[i]));
    return out;
}

FrameStats::Summary FrameStats::overall() const {
    std::vector<Frame> all;
    for (const auto& p : m_frames) all.insert(all.end(), p.begin(), p.end());
    return summarize("overall", all);
}

std::vector<std::string> FrameStats::windowColumns() {
    return { "t_s", "phase", "fps", "frame_avg_ms", "frame_max_ms", "gpu_avg_ms", "physics_avg_ms", "bodies" };
}

std::vector<std::vector<double>> FrameStats::windows() const {
    std::vector<std::vector<double>> rows;
    double t = 0.0;
    for (size_t p = 0; p < m_frames.size(); ++p) {
        std::vector<Frame> win;
        double winMs = 0.0;
        auto flush = [&] {
            if (win.empty()) return;
            Summary s = summarize("", win);
            t += winMs / 1000.0;
            rows.push_back({ t, static_cast<double>(p), s.fpsAvg, s.frameAvgMs, s.frameMaxMs, s.gpuAvgMs, s.physicsAvgMs,
                             static_cast<double>(s.bodiesMax) });
            win.clear();
            winMs = 0.0;
        };
        for (const Frame& f : m_frames[p]) {
            win.push_back(f);
            winMs += f.frameMs;
            if (winMs >= 1000.0) flush();
        }
        flush(); // a phase's last partial second is its own row
    }
    return rows;
}

const char* FrameStats::verdict(const Summary& s) {
    if (s.frames == 0) return "no data";
    if (s.low1Fps >= 55.0) return "smooth";
    if (s.low1Fps >= 30.0) return "playable";
    if (s.low1Fps >= 15.0) return "struggles";
    return "too slow";
}

void FrameStats::fill(BenchmarkReport& r) const {
    auto put = [&r](const std::string& prefix, const Summary& s) {
        r.results.emplace_back(prefix + "fps_avg", s.fpsAvg);
        r.results.emplace_back(prefix + "fps_1pct_low", s.low1Fps);
        r.results.emplace_back(prefix + "frame_avg_ms", s.frameAvgMs);
        r.results.emplace_back(prefix + "frame_p50_ms", s.frameP50Ms);
        r.results.emplace_back(prefix + "frame_p95_ms", s.frameP95Ms);
        r.results.emplace_back(prefix + "frame_p99_ms", s.frameP99Ms);
        r.results.emplace_back(prefix + "frame_max_ms", s.frameMaxMs);
        r.results.emplace_back(prefix + "gpu_avg_ms", s.gpuAvgMs);
        r.results.emplace_back(prefix + "physics_avg_ms", s.physicsAvgMs);
        r.results.emplace_back(prefix + "physics_max_ms", s.physicsMaxMs);
        r.results.emplace_back(prefix + "bodies_max", s.bodiesMax);
        r.results.emplace_back(prefix + "frames", s.frames);
        r.results.emplace_back(prefix + "seconds", s.seconds);
    };
    const Summary all = overall();
    put("", all);
    std::string perPhase;
    for (const Summary& s : phases()) {
        put(s.name + ".", s);
        perPhase += " " + s.name + ": " + verdict(s) + ",";
    }
    if (!perPhase.empty()) perPhase.pop_back();
    r.sampleColumns = windowColumns();
    r.samples = windows();
    r.notes.push_back(std::string("Verdict: ") + verdict(all) + " (by the 1% low fps). Per phase:" + perPhase + ".");
    r.notes.push_back("Verdicts: smooth = 1% low >= 55 fps, playable >= 30, struggles >= 15, too slow below.");
    r.notes.push_back("fps_1pct_low = frames per second over the slowest 1% of frames (the stutter you feel); "
                      "gpu_avg_ms = GPU time of the frame's passes (-1 = the driver doesn't report it).");
    std::string names;
    for (size_t i = 0; i < m_names.size(); ++i) names += (i ? ", " : "") + std::to_string(i) + " = " + m_names[i];
    r.notes.push_back("Sample column 'phase': " + names + ".");
}

} // namespace kke
