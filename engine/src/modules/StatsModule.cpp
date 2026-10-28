#include "kke/modules/StatsModule.h"
#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Profiler.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>
#include "kke/ImGuiPlacement.h"

namespace kke {

void StatsModule::init(Application& app) {
    m_app = &app;
}

void StatsModule::update(const UpdateContext& ctx) {
    m_lastDt = ctx.dt;

    m_cpuFrameTimesMs.push_back(ctx.dt * 1000.0f);
    if (m_cpuFrameTimesMs.size() > kHistorySize) m_cpuFrameTimesMs.pop_front();

    float gpuMs = m_app->renderer().lastGpuFrameTimeMs();
    if (gpuMs >= 0.0f) {
        m_gpuFrameTimesMs.push_back(gpuMs);
        if (m_gpuFrameTimesMs.size() > kHistorySize) m_gpuFrameTimesMs.pop_front();
    }
}

namespace {
void plotWithStats(const char* label, const std::deque<float>& samples, const char* unit) {
    if (samples.empty()) {
        ImGui::Text("%s: (no data yet)", label);
        return;
    }

    std::vector<float> flat(samples.begin(), samples.end());
    float avg = std::accumulate(flat.begin(), flat.end(), 0.0f) / flat.size();
    float maxV = *std::max_element(flat.begin(), flat.end());

    ImGui::Text("%s: %.2f %s (avg)  %.2f %s (max)", label, avg, unit, maxV, unit);
    ImGui::PlotLines(("##" + std::string(label)).c_str(), flat.data(), static_cast<int>(flat.size()),
                      0, nullptr, 0.0f, maxV * 1.2f, ImVec2(280, 50));
}
} // namespace

void StatsModule::renderUi() {
    placeNextDebugWindow(ImVec2(10, 10), ImVec2(320, 0));
    ImGui::Begin("Performance");

    float fps = m_lastDt > 0.0f ? 1.0f / m_lastDt : 0.0f;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Separator();

    plotWithStats("CPU frame time", m_cpuFrameTimesMs, "ms");
    ImGui::Spacing();
    plotWithStats("GPU frame time", m_gpuFrameTimesMs, "ms");

    // Where the CPU time goes: every module's calls, timed by Application
    // every frame (kke/Profiler.h), and a capture of them for Perfetto.
    Profiler& profiler = Profiler::get();
    ImGui::Separator();
    ImGui::Text("Modules, CPU (avg / worst of %d frames)", Profiler::kWindow);
    const double frameMs = std::max(0.001, profiler.averageFrameMs());
    for (const Profiler::ModuleStat& s : profiler.moduleStats(8)) {
        if (s.avgMs < 0.005) break;
        char label[64];
        std::snprintf(label, sizeof(label), "%.2f / %.2f ms", s.avgMs, s.maxMs);
        ImGui::ProgressBar(static_cast<float>(std::min(1.0, s.avgMs / frameMs)), ImVec2(120, 0), label);
        ImGui::SameLine();
        ImGui::TextUnformatted(s.name.c_str());
    }
    const Profiler::CaptureState capture = profiler.captureState();
    if (capture.active) {
        ImGui::Text("Capturing: %.1f s, %d frames, %zu events", capture.elapsedSeconds, capture.frames, capture.events);
        if (ImGui::Button("Stop and save")) {
            std::string error;
            if (profiler.stopCapture(&error)) log::get("Profiler")->info("capture saved to {}\n{}", profiler.lastCapturePath(), profiler.lastCaptureSummary());
            else log::get("Profiler")->warn("capture not saved: {}", error);
        }
    } else {
        if (ImGui::Button("Capture 5 s")) {
            Profiler::CaptureOptions options;
            options.seconds = 5.0;
            options.path = Profiler::newCapturePath();
            std::string error;
            if (!profiler.startCapture(options, &error)) log::get("Profiler")->warn("{}", error);
        }
        if (const std::string last = profiler.lastCapturePath(); !last.empty())
            ImGui::TextWrapped("Saved: %s (open it at ui.perfetto.dev)", last.c_str());
    }

    // Real per-draw-call GPU profiling, when VulkanProfiler is actually
    // active — see kke::VulkanDevice::queryGpuProfilerFrameSummary() and
    // docs/HISTORY.md "GPU profiler (VulkanProfiler) integration".
    //
    // DISABLED BY DEFAULT, on purpose: calling this reproducibly
    // segfaults *inside the layer's own compiled code*
    // (vkGetProfilerFrameDataEXT itself, confirmed via gdb backtrace —
    // not in this engine's code, and not a timing issue: it crashes on
    // the very first call regardless of how many frames have already
    // been presented, and regardless of sampling_mode or threading
    // config). The layer's own overlay reads equivalent data and works
    // correctly, proving the layer itself is fine and active — this is
    // specifically about calling this one function from application
    // code in this environment. Root-causing further needs the layer's
    // own debug symbols/source stepping, which is out of scope for a
    // guess-and-check fix. Flip KKE_QUERY_GPU_PROFILER_DATA on locally
    // to re-attempt this once that's understood, or if a newer layer
    // version fixes it — see docs/HISTORY.md for the full incident writeup.
#ifdef KKE_QUERY_GPU_PROFILER_DATA
    ++m_frameCount;
    if (m_frameCount > 3) {
        VulkanDevice::GpuProfilerFrameSummary profilerSummary = m_app->device().queryGpuProfilerFrameSummary();
        if (profilerSummary.valid) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "VulkanProfiler (VK_LAYER_PROFILER_unified)");
            ImGui::Text("Frame GPU time: %.2f ms", profilerSummary.frameDurationMs);
            ImGui::Text("Draw/dispatch/copy commands this frame: %u", profilerSummary.commandCount);

            ++m_framesSinceProfilerLog;
            if (m_framesSinceProfilerLog >= kProfilerLogIntervalFrames) {
                m_framesSinceProfilerLog = 0;
                log::get("GpuProfiler")->info("frame GPU time: {:.2f} ms, {} draw/dispatch/copy commands",
                                               profilerSummary.frameDurationMs, profilerSummary.commandCount);
            }
        }
    }
#endif

    ImGui::End();
}

} // namespace kke
