#include "kke/modules/DebugControlModule.h"
#include "kke/Application.h"

#include <imgui.h>

namespace kke {

void DebugControlModule::init(Application& app) {
    m_app = &app;
}

void DebugControlModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(340, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug Control");

    bool paused = m_app->isPaused();
    if (ImGui::Checkbox("Paused (freeze simulation)", &paused)) {
        m_app->setPaused(paused);
    }

    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("Step one frame")) {
        m_app->stepOneFrame();
    }
    ImGui::EndDisabled();

    ImGui::TextWrapped(
        "Rendering keeps presenting every frame even while paused -- "
        "fixedUpdate/update/compute (including GPU particle simulation) "
        "freeze instead. Useful for holding a frame steady for an "
        "external capture tool (RenderDoc, a Vulkan profiling layer) or "
        "for stepping through simulation ticks one at a time.");
    ImGui::End();

    const auto& broken = m_app->brokenModules();
    if (!broken.empty()) {
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(340, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("/!\\ Emergency Log");
        ImGui::TextWrapped(
            "The module(s) below threw an error and have been disabled "
            "for the rest of this session. Everything else keeps running.");
        ImGui::Separator();
        for (size_t i = 0; i < broken.size(); ++i) {
            const auto& info = broken[i];

            // Source badge: frames the error differently depending on
            // who's likely at fault. A script author needs to know
            // "this is your script" vs. "this is worth reporting to the
            // engine" — very different next actions.
            const char* sourceLabel = "UNKNOWN SOURCE";
            ImVec4 sourceColor(0.7f, 0.7f, 0.7f, 1.0f);
            if (info.source == ErrorSource::Script) {
                sourceLabel = "SCRIPT ERROR";
                sourceColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            } else if (info.source == ErrorSource::Engine) {
                sourceLabel = "ENGINE ERROR";
                sourceColor = ImVec4(1.0f, 0.55f, 0.55f, 1.0f);
            }

            ImGui::TextColored(sourceColor, "[%s]", sourceLabel);
            ImGui::SameLine();
            ImGui::Text("%s", info.moduleName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(during %s)", info.stage.c_str());

            if (!info.file.empty()) {
                ImGui::TextDisabled("at %s:%d", info.file.c_str(), info.line);
            }

            // The friendly message is the headline — this is the part a
            // non-programmer actually needs. Technical details are one
            // click away, not hidden, but not forced on someone who
            // doesn't want them either.
            ImGui::TextWrapped("%s", info.friendlyMessage.c_str());

            if (info.technicalMessage != info.friendlyMessage) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode("Technical details")) {
                    ImGui::TextWrapped("%s", info.technicalMessage.c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Separator();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
    }
}

} // namespace kke
