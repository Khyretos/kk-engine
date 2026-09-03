#include "kke/modules/StatsModule.h"
#include "kke/Application.h"
#include "kke/Log.h"

#include <imgui.h>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

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
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance");

    float fps = m_lastDt > 0.0f ? 1.0f / m_lastDt : 0.0f;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Separator();

    plotWithStats("CPU frame time", m_cpuFrameTimesMs, "ms");
    ImGui::Spacing();
    plotWithStats("GPU frame time", m_gpuFrameTimesMs, "ms");

    // Real per-draw-call GPU profiling, when VulkanProfiler is actually
    // active — see kke::VulkanDevice::queryGpuProfilerFrameSummary() and
    // README "GPU profiler (VulkanProfiler) integration".
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
    // version fixes it — see README for the full incident writeup.
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
