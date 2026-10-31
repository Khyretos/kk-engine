#pragma once

#include "kke/Capabilities.h"
#include "kke/Localization.h"
#include "kke/Module.h"
#include "kke/Subtitles.h"

#include <string>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
}

namespace kke {

class UiModule;

// Dialogue subtitles on screen (ENG-F85, docs/ACCESSIBILITY.md): lines for
// speech and story, with a speaker's name, at the bottom of the screen on a
// dark band, in the size the player chose (`accessibility.subtitleScale`),
// or none (`accessibility.subtitles`). Lines are queued by kke::SubtitleQueue;
// a text that is a language key (kke::LocalizationModule) is shown in the
// player's language.
//
// Lua:
//   subtitle.say("intro.line1", 3.0, "npc.guard")   -- key or plain text, seconds (0 = reading time), speaker
//   subtitle.say("Welcome, {name}.", 0, "Mira", {name = "Kees"})
//   subtitle.clear()
//   subtitle.active() -> how many are on screen or waiting
class SubtitleModule : public Module, public ISettingsListener {
public:
    const char* name() const override { return "Subtitles"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void onSettingsChanged(const EngineSettings& settings) override;
    void shutdown() override;

    // For C++ games. The text may be a language key.
    uint32_t say(const std::string& textOrKey, float seconds = 0.0f, const std::string& speakerOrKey = {}, const LocArgs& args = {});
    void clear() { m_queue.clear(); }
    SubtitleQueue& queue() { return m_queue; }

private:
    bool makeDocument();
    void registerLua();
    std::string translated(const std::string& textOrKey, const LocArgs& args) const;

    Application* m_app = nullptr;
    UiModule* m_ui = nullptr;
    ILocalizer* m_localizer = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    Rml::Element* m_box = nullptr;
    SubtitleQueue m_queue;
    bool m_enabled = true;
    float m_scale = 1.0f;
    std::string m_shown; // the RML on screen now: rewritten only when it changes
    bool m_noUiLogged = false;
};

} // namespace kke
