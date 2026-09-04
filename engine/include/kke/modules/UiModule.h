#pragma once

#include "kke/Module.h"
#include "kke/RmlVulkanRenderInterface.h"

#include <RmlUi/Core/SystemInterface.h>
#include <memory>

namespace Rml {
class Context;
class ElementDocument;
}

namespace kke {

// SLICE 2 of the RmlUi integration (slice 1 was init/shutdown lifecycle
// only — see README/git history). This slice makes RmlUi fully visible:
// real geometry AND real textures through RmlVulkanRenderInterface, a
// bundled Noto Sans fallback font, and a small in-memory test document
// with actual text proving the whole pipeline — layout, font shaping,
// glyph atlas generation, texture upload, Vulkan draw calls — works end
// to end.
//
// Deliberately NOT in this slice (see README "Roadmap"):
//   - Real image-file decoding (<img>, background-image: url(...)) —
//     RmlVulkanRenderInterface::LoadTexture is still a stub; this needs
//     stb_image, which is a separate, independent piece of work.
//   - Loading documents from actual .rml/.rcss files on disk (the in-
//     memory test document is a deliberately small stand-in).
//
// Resizing IS handled now: update() compares the current swapchain
// extent against the context's own dimensions every frame and calls
// SetDimensions() when they differ. Checked once per frame rather than
// reacting to the SDL resize event directly, specifically to sidestep
// an event-ordering question that wasn't worth resolving cleverly: by
// the time update() runs, the swapchain has already been recreated for
// this frame (if needed), so its extent is always trustworthy here —
// whereas reacting to the resize event itself would require knowing
// whether that recreation has already happened yet in the same frame.
//
// Input (mouse/keyboard) IS wired now, via onEvent() — see its
// implementation for the SDL-to-RmlUi key mapping, which covers common
// keys (letters, digits, arrows, enter/escape/backspace/tab/delete,
// F1-F12, shift/ctrl) but isn't exhaustive (no numpad-specific keys, no
// non-US-layout punctuation beyond period/comma). Extend the mapping
// table as real need for a specific key comes up rather than trying to
// cover SDL's entire keycode space speculatively.
class UiModule : public Module {
public:
    const char* name() const override { return "UI"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // Lets other modules (e.g. MarketplaceUiModule) add their own
    // Rml::ElementDocument into the same context/render pipeline, without
    // each needing to own a separate Rml::Context (RmlUi supports many
    // documents in one context; a second context is a heavier and
    // unnecessary tool for "another panel").
    Rml::Context* context() { return m_context; }

private:
    class EngineSystemInterface : public Rml::SystemInterface {
    public:
        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    };

    EngineSystemInterface m_systemInterface;
    std::unique_ptr<RmlVulkanRenderInterface> m_renderInterface;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_testDocument = nullptr;
    bool m_initialised = false;
    Application* m_app = nullptr; // needed each frame in update() to detect window resize
};

} // namespace kke
