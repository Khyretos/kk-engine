#include "ImGuiShowcaseModule.h"

#include <imgui.h>

namespace kke_demo {

void ImGuiShowcaseModule::renderUi() {
    // A small orientation panel -- ShowDemoWindow() below is huge and
    // dense (it's ImGui's own comprehensive widget catalogue), and
    // without any context a viewer might not realize that big window
    // *is* the showcase, not something to dismiss.
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("ImGui Showcase");
    ImGui::TextWrapped(
        "The big window elsewhere on screen is Dear ImGui's own built-in "
        "demo (ImGui::ShowDemoWindow()) -- every widget type this UI "
        "library supports, in one place, maintained by ImGui itself.");
    ImGui::End();

    bool alwaysOpen = true;
    ImGui::ShowDemoWindow(&alwaysOpen);
}

} // namespace kke_demo
