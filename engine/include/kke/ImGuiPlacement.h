#pragma once

// Default place and size for a developer (F1) ImGui window, written in
// the 13 px font's pixels like the rest of the engine's panels: scaled
// with the font (a phone's ImGui is 2-3x larger) and kept on the screen,
// so a tall phone screen gets a window as wide as it can take instead of
// a narrow one off its edge. First use only; a window the player moved
// stays where they put it.
//
// Only for files that already use ImGui (DebugUi.h keeps ImGui out of
// everything else).

#include <imgui.h>

#include <algorithm>

namespace kke {

// size.x 0 = ImGui's own width (fits the contents); size.y 0 = fits too.
//
// A small screen (a phone: not two 350 px panels side by side, or under
// 500 px tall) stacks the panels collapsed down the left edge in the
// order they're first drawn, one title bar each, instead of piling them
// on each other; a tap on a title opens it.
inline void placeNextDebugWindow(ImVec2 pos, ImVec2 size = ImVec2(0.0f, 0.0f)) {
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 view = ImGui::GetMainViewport()->WorkSize;
    const float margin = 8.0f * s;
    ImVec2 sz(size.x * s, size.y * s);
    sz.x = std::max(1.0f, std::min(sz.x, view.x - 2.0f * margin));
    if (sz.y > 0.0f) sz.y = std::max(1.0f, std::min(sz.y, view.y - 2.0f * margin));
    if (size.x > 0.0f) ImGui::SetNextWindowSize(sz, ImGuiCond_FirstUseEver);

    if (view.x < 700.0f * s || view.y < 500.0f * s) {
        static int frame = -1;
        static float nextY = 0.0f;
        if (ImGui::GetFrameCount() != frame) { frame = ImGui::GetFrameCount(); nextY = margin; }
        ImGui::SetNextWindowPos(ImVec2(margin, nextY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
        nextY += ImGui::GetFrameHeight() + 4.0f * s;
        return;
    }
    ImVec2 p(pos.x * s, pos.y * s);
    const float w = size.x > 0.0f ? sz.x : view.x * 0.5f;
    p.x = std::clamp(p.x, margin, std::max(margin, view.x - w - margin));
    p.y = std::clamp(p.y, margin, std::max(margin, view.y * 0.75f));
    ImGui::SetNextWindowPos(p, ImGuiCond_FirstUseEver);
}

} // namespace kke
