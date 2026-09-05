#pragma once

#include "kke/Module.h"

namespace kke_demo {

// Wraps Dear ImGui's own built-in ImGui::ShowDemoWindow() — genuinely
// the canonical "showcase everything this UI library can do": every
// widget type (buttons, sliders, color pickers, drag/drop, tables,
// trees, tabs, menus, popups, text editing, plotting), all in one
// place, maintained by ImGui itself rather than hand-curated here.
// Confirmed available before writing this: imgui_demo.cpp is already
// compiled into this engine's imgui target (see root CMakeLists.txt),
// and nothing defines IMGUI_DISABLE_DEMO_WINDOWS.
//
// Deliberately lives in games/imgui_demo/, not engine/kke/modules/ —
// unlike OrbitCameraModule or GridModule, this isn't a building block
// a real game would want; it's explicitly a "look what the UI library
// can do" tech demo, matching where CubeModule/DestructionModule/
// NetworkModule live for the same reason.
class ImGuiShowcaseModule : public kke::Module {
public:
    const char* name() const override { return "ImGuiShowcase"; }
    void renderUi() override;
};

} // namespace kke_demo
