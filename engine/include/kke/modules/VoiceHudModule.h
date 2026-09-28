#pragma once

// Proximity voice made visible (docs/NETWORKING.md "Voice"): where each
// voice comes from, and who to mute.
//
//   Markers:  a ring over the head of everyone talking near you, pulsing
//             with their voice, with their name; someone out of view gets
//             an arrow at the edge of the screen pointing their way (top =
//             ahead, bottom = behind, like the sound ring).
//   List:     the players near you who talked lately, nearest first, one
//             numbered slot each: an arrow for their direction, the
//             distance, a level bar, "muted". A slot keeps its number
//             while they talk (holdSeconds after they stop), and nothing
//             moves while the menu is open, so the slot you pick is the
//             player you meant.
//   Mute:     addToPanel() puts one row per slot in a pause menu
//             (kke::DemoPanelModule): "1  Mute Sam". Muting is only for
//             you and lasts the session; you still see a muted player's
//             slot (greyed) so you can unmute them.
//
// Reads kke::VoiceModule::talkers(); draws with RmlUi (needs a UiModule),
// in frameStart, so it shows in shipping builds with the developer panels
// hidden. Nothing shows offline or when nobody talks.

#include "kke/Module.h"
#include "kke/modules/VoiceModule.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace kke {

struct Camera;
class DemoPanelModule;

class VoiceHudModule : public Module {
public:
    static constexpr int kMaxSlots = 8;
    enum class Corner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
    struct Settings {
        bool markers = true;       // rings over heads, arrows at the edge
        bool list = true;          // the nearby talkers list
        int slots = 6;             // list length, up to kMaxSlots
        float holdSeconds = 6.0f;  // a slot stays this long after they stop talking
        float markerScale = 1.0f;
        float edgeInset = 0.07f;   // edge arrows this far in (fraction of the smaller screen side)
        Corner corner = Corner::BottomLeft;
        // The pause menu section init() adds when there's a DemoPanelModule
        // ("" = none; the game calls addToPanel itself).
        std::string panelSection = "Voices nearby";
    };
    struct Slot {
        bool used = false;
        VoiceModule::Talker talker;
    };

    VoiceHudModule() = default;
    explicit VoiceHudModule(const Settings& s) : settings(s) {}
    const char* name() const override { return "VoiceHud"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void frameStart(const UpdateContext& ctx) override;
    void shutdown() override;

    Settings settings;
    bool visible = true; // the game hides it (a menu over everything, a cutscene)

    // The list as shown (slot i is number i + 1).
    const std::array<Slot, kMaxSlots>& slots() const { return m_slots; }
    // Pause menu rows (shown while online): a mute per slot, how to talk,
    // markers on/off, headphones.
    void addToPanel(DemoPanelModule& panel, const std::string& section = "Voices nearby");

    // Pure helpers, public for the tests.
    // Keeps each talker in the slot it had, fills free slots nearest
    // first, frees a slot when its talker left the list (not while frozen).
    static void assignSlots(std::array<Slot, kMaxSlots>& slots, const std::vector<VoiceModule::Talker>& talkers, int count, bool frozen);
    // A world point on screen: false when it's behind the camera.
    // `ndc` is -1..1 left to right and top to bottom.
    static bool project(const Camera& camera, float aspect, const glm::vec3& point, glm::vec2& ndc);
    // Where an arrow for `azimuth` (0 = up, +pi/2 = right) sits on a
    // rectangle `inset` pixels inside a `size` screen.
    static glm::vec2 edgePoint(float azimuth, glm::vec2 size, float inset);
    // "ahead", "left", "behind right"...
    static const char* directionWord(float azimuth);

private:
    bool makeDocument();
    void draw();
    Rml::Element* pooled(std::vector<Rml::Element*>& pool, size_t index, const char* cls);

    Application* m_app = nullptr;
    VoiceModule* m_voice = nullptr;
    DemoPanelModule* m_panel = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    Rml::Element* m_marksEl = nullptr;
    Rml::Element* m_listEl = nullptr;
    Rml::Element* m_selfEl = nullptr;
    std::vector<Rml::Element*> m_marks, m_arrows;
    size_t m_marksShown = 0, m_arrowsShown = 0;
    struct SlotEls {
        Rml::Element* row = nullptr;
        Rml::Element* chev = nullptr;
        Rml::Element* name = nullptr;
        Rml::Element* info = nullptr;
        Rml::Element* fill = nullptr;
        std::string shownName, shownInfo;
    };
    std::array<SlotEls, kMaxSlots> m_slotEls{};
    std::array<Slot, kMaxSlots> m_slots{};
    std::array<bool, kMaxSlots> m_muteShown{};
    bool m_headphones = false;
    int m_talkMode = 0;
    Corner m_builtCorner = Corner::BottomLeft;
    bool m_noUiLogged = false;
};

} // namespace kke
