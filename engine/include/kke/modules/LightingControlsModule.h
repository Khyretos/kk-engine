#pragma once

#include "kke/Module.h"

#include <memory>

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml

namespace kke {

// A real, working settings panel for this engine's actual multi-light
// system (see Application.h's Light/Lighting structs and
// LightingBuffer) -- built specifically to replace UiModule's old
// hardcoded "three test boxes" (see UiModule.h's own updated class
// comment for why those existed and why they were removed) with
// something genuinely purposeful: real sliders and buttons that
// change real engine state, verified by watching lit geometry
// actually change in response.
//
// Deliberately a real engine module (games/ demos can add it, not
// forced to), not baked into UiModule itself -- UiModule stays
// content-agnostic; this is real, optional content any demo with lit
// geometry can opt into the same way MarketplaceUiModule already
// works. Requires kke::UiModule to already be added and initialised
// first (same requirement, same reasoning, as MarketplaceUiModule and
// RmlUiShowcaseModule).
//
// Sliders are read by polling their current value once per frame in
// update() rather than a registered Rml::EventListener for RmlUi's
// own Change event -- deliberately simple for the small number of
// values involved here, and sidesteps needing to reason about
// interactions between event listeners and the click-position
// workaround UiModule's own mousedown handling already applies to
// every input.range element (see UiModule.cpp's own comment on that
// fix for the real, diagnosed RmlUi quirk it works around). Preset
// buttons use a real Rml::EventListener instead, since button clicks
// already dispatch correctly through RmlUi's normal event path with
// no equivalent quirk to work around.
class LightingControlsModule : public Module {
public:
    const char* name() const override { return "LightingControls"; }
    ~LightingControlsModule();
    // left/top let more than one RmlUi content module share a screen
    // without hardcoded overlap — a real, hit problem once a second
    // module (kke::MaterialGridModule) needed the exact same default
    // position this one already used. Defaults match this module's
    // original, already-verified position, so every existing call site
    // that doesn't pass these keeps rendering exactly where it always
    // did.
    explicit LightingControlsModule(float left = 40.0f, float top = 500.0f);
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void shutdown() override;

private:
    class PresetButtonListener;

    Application* m_app = nullptr;
    float m_left;
    float m_top;
    Rml::ElementDocument* m_document = nullptr;
    float m_lastAmbient = -1.0f;
    float m_lastKeyIntensity = -1.0f;
    float m_lastFillIntensity = -1.0f;
    // Raw pointer, not unique_ptr<PresetButtonListener> -- that
    // combination (a unique_ptr member of an only-forward-declared
    // nested type) hit a real, confirmed compile error even with an
    // out-of-line destructor defined where the type is complete
    // (standard "Pimpl" pattern): std::make_unique<LightingControlsModule>(),
    // instantiated from games/kke_demo_game/main.cpp where only this
    // header (not the .cpp) is visible, still required
    // PresetButtonListener to be complete. A raw pointer sidesteps
    // this entirely -- managed manually in init()/shutdown(), which
    // this class already needs regardless for m_document's own
    // Close() call.
    PresetButtonListener* m_listener = nullptr;
};

} // namespace kke
