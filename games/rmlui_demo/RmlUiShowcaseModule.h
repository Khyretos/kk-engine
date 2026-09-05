#pragma once

#include "kke/Module.h"

namespace Rml { class ElementDocument; }

namespace kke_demo {

// Loads a genuinely rich RML/RCSS document into kke::UiModule's shared
// Rml::Context (see UiModule::context() -- this is exactly what that
// accessor exists for: letting another module add its own document
// into the same context/render pipeline, rather than each needing its
// own separate Rml::Context). Confirmed before writing this: RmlUi
// 6.3's Core library alone (already linked -- see root CMakeLists.txt)
// includes <input>, <select>, <textarea>, <tabset>, and <progress>
// directly -- these were merged into Core from the old separate
// "Controls" plugin in a prior RmlUi version, so no additional linking
// was needed to showcase them.
//
// Requires kke::UiModule to already be added AND initialised first --
// see games/rmlui_demo/main.cpp for the add order this depends on.
// Deliberately lives here, not engine/kke/modules/, for the same
// reason ImGuiShowcaseModule does: this is a "look what the library
// can do" tech demo, not a building block a real game would want.
class RmlUiShowcaseModule : public kke::Module {
public:
    const char* name() const override { return "RmlUiShowcase"; }
    void init(kke::Application& app) override;
    void shutdown() override;

private:
    Rml::ElementDocument* m_document = nullptr;
};

} // namespace kke_demo
