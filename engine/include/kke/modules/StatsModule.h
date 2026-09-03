#pragma once

#include "kke/Module.h"

#include <deque>

namespace kke {

// The "how performant is this actually" tool: a always-on-top ImGui panel
// showing FPS, CPU frame time, and GPU frame time (via the timestamp
// queries Renderer records every frame), each with a rolling history graph
// so spikes are visible, not just the instantaneous number.
//
// Also shows real per-draw-call GPU profiling data when
// KKE_ENABLE_GPU_PROFILER is on and VulkanProfiler is actually installed
// (see README "GPU profiler (VulkanProfiler) integration") — pulled via
// VulkanDevice::queryGpuProfilerFrameSummary(), which calls the layer's
// own vkGetProfilerFrameDataEXT. Silently omitted (not an error) when the
// profiler isn't active, exactly like the rest of this engine treats an
// unavailable optional layer.
//
// This is the module to extend first if you want more profiling: per-module
// CPU time, draw-call counts, GPU memory usage from VMA, etc. all fit the
// same "keep a small history buffer, plot it" shape.
class StatsModule : public Module {
public:
    const char* name() const override { return "Stats"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;

private:
    static constexpr size_t kHistorySize = 240;
    static constexpr int kProfilerLogIntervalFrames = 120; // ~once every 2s at 60fps — see renderUi()

    Application* m_app = nullptr;
    std::deque<float> m_cpuFrameTimesMs;
    std::deque<float> m_gpuFrameTimesMs;
    float m_lastDt = 0.0f;

    int m_framesSinceProfilerLog = 0;
    int m_frameCount = 0; // see renderUi() — the profiler must not be queried before at least one full frame has been presented
};

} // namespace kke
