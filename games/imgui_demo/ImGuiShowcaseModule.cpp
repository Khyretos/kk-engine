#include "ImGuiShowcaseModule.h"

#include <imgui.h>

#include <algorithm>
#include "kke/ImGuiPlacement.h"

namespace kke_demo {

void ImGuiShowcaseModule::renderUi() {
    // A small orientation panel -- ShowDemoWindow() below is huge and
    // dense (it's ImGui's own comprehensive widget catalogue), and
    // without any context a viewer might not realize that big window
    // *is* the showcase, not something to dismiss.
    kke::placeNextDebugWindow(ImVec2(10, 200), ImVec2(320, 100)); // under Performance
    ImGui::Begin("ImGui Showcase");
    ImGui::TextWrapped(
        "The big window elsewhere on screen is Dear ImGui's own built-in "
        "demo (ImGui::ShowDemoWindow()) -- every widget type this UI "
        "library supports, in one place, maintained by ImGui itself.");
    ImGui::End();

    bool alwaysOpen = true;
    ImGui::ShowDemoWindow(&alwaysOpen);
    // ShowDemoWindow() asks for 550x680 unscaled pixels at x 650: narrow
    // and half off a phone's screen. Where that doesn't fit, place it once
    // beside or under the other panels.
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 origin = ImGui::GetMainViewport()->WorkPos;
    const ImVec2 view = ImGui::GetMainViewport()->WorkSize;
    // (ImGuiCond_Once doesn't do: the demo's own FirstUseEver call uses
    // up the window's once.)
    if (!m_demoPlaced && (view.x < 1200.0f * s || view.y < 700.0f * s)) {
        m_demoPlaced = true;
        const bool tall = view.y >= view.x;
        const ImVec2 size(std::min(550.0f * s, tall ? view.x - 16.0f * s : view.x * 0.6f),
                          std::min(680.0f * s, tall ? view.y * 0.6f : view.y - 16.0f * s));
        // Right of (landscape) or under (portrait) the stacked panels.
        const ImVec2 pos = !tall ? ImVec2(origin.x + view.x - size.x - 8.0f * s, origin.y + 8.0f * s)
                                 : ImVec2(origin.x + 8.0f * s, origin.y + view.y * 0.2f);
        ImGui::SetWindowSize("Dear ImGui Demo", size);
        ImGui::SetWindowPos("Dear ImGui Demo", pos);
    }
}

} // namespace kke_demo
