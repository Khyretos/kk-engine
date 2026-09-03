#include "kke/modules/StatsModule.h"
#include "kke/Application.h"

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

    ImGui::End();
}

} // namespace kke
