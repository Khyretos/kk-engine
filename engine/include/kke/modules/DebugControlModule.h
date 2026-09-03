#pragma once

#include "kke/Module.h"

namespace kke {

// The ImGui front-end for Application's pause/step controls and its
// per-module fault isolation — see Application.h for the actual
// mechanics; this module just exposes them.
//
// Two panels:
//   - "Debug Control": pause/resume, single-step-one-frame.
//   - "Emergency Log": only appears once at least one module has
//     thrown and been disabled (see Application::brokenModules()) —
//     bright red, impossible to miss, shows which module, which
//     lifecycle stage, and the actual error message. Everything else
//     in the engine keeps running; this is where you find out what
//     broke and why, without the whole session crashing to tell you.
class DebugControlModule : public Module {
public:
    const char* name() const override { return "DebugControl"; }

    void init(Application& app) override;
    void renderUi() override;

private:
    Application* m_app = nullptr;
};

} // namespace kke
