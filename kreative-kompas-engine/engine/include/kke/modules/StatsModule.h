#pragma once

#include "kke/Module.h"

#include <deque>

namespace kke {

// The "how performant is this actually" tool: a always-on-top ImGui panel
// showing FPS, CPU frame time, and GPU frame time (via the timestamp
// queries Renderer records every frame), each with a rolling history graph
// so spikes are visible, not just the instantaneous number.
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

    Application* m_app = nullptr;
    std::deque<float> m_cpuFrameTimesMs;
    std::deque<float> m_gpuFrameTimesMs;
    float m_lastDt = 0.0f;
};

} // namespace kke
