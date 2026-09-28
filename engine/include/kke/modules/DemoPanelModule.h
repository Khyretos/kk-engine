#pragma once

#include "kke/Module.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace kke {

class InputModule;

// A settings panel for a demo or a tool, in RmlUi, that works the same
// with a controller, the keyboard and the mouse: sliders, choices,
// toggles, buttons and live text lines, in sections. It replaces the
// ImGui windows the demos used to have, so a player on a sofa with a pad
// can change everything a mouse user can.
//
//   auto& panel = app.addModule<kke::DemoPanelModule>("Sea");
//   // in a game module's init():
//   auto& s = app.getModule<kke::DemoPanelModule>()->section("Waves");
//   s.slider("Wind", &m_wind, 0.0f, 14.0f, "%.1f m/s", [this] { applyWind(); });
//   s.choice("Throw", &m_kind, { "Foam", "Wood", "Iron" });
//   s.toggle("Camera follows the boat", &m_follow);
//   s.button("Reset", [this] { reset(); });
//   s.text([this] { return "Boat speed " + fmt(speed) + " m/s"; });
//   s.text("{sea.throw} throw  ·  {sea.drive} drive"); // button prompts
//
// The values live in the game; the panel reads them every frame (so a
// key that changes one shows up here too) and writes them when the
// player changes a row, then calls the row's onChange.
//
// Three states, one button to move between them (panel.toggle: the
// View/Back button on a pad, F3 on the keyboard):
//   Open      shown, the game has the controls; the mouse can use it.
//   Active    shown with a highlighted row: up/down picks a row,
//             left/right changes it (hold to keep changing), A presses.
//             While active the game's "game" input context is off for
//             player 1, so the stick that picks rows doesn't also drive
//             the boat. B, Esc or the toggle again hands control back.
//   Collapsed just the title and how to open it (the last row, "Hide
//             panel", or a click on the title).
// Keyboard while active: arrows, Enter, Esc. Clicks work in every state.
//
// The engine's own ImGui windows (Performance, Camera, Physics...) are
// developer tools, not the game's: with a DemoPanelModule they start
// hidden and F1 shows them (setDeveloperPanelsKey(false) for a game that
// handles F1 itself).
//
// Needs InputModule (actions, prompts) and UiModule (drawing). Sections
// can be added before or after this module's init(); the document is
// rebuilt on the next frame. See docs/DEMO_PANEL.md.
class DemoPanelModule : public Module {
    struct Row;

public:
    enum class State { Collapsed, Open, Active };
    enum class Side { Left, Right };

    // A live value: the pointer to read and write this frame, or null to
    // hide the row (a setting that only exists in one scene).
    template <class T> using Ref = std::function<T*()>;

    class Section {
    public:
        // Text: fixed, or recomputed every frame. {action} and {name}
        // become button prompts (ButtonPrompts::format; "{{" is a brace).
        Section& text(std::string fixed);
        Section& text(std::function<std::string()> live);
        Section& note(std::string fixed); // smaller, muted: the "why"
        // Controls help that depends on what the player holds: the first
        // text while they use the keyboard and mouse, the second on a
        // controller (an action only bound to the other shows nothing).
        Section& hint(std::string keyboardMouse, std::string controller);
        Section& heading(std::string title);
        Section& separator();
        // printf format for the value ("%.1f m/s"); step 0 = 1/50 of the range.
        Section& slider(std::string label, float* value, float min, float max, std::string format = "%.2f",
                        std::function<void()> onChange = {}, float step = 0.0f);
        Section& slider(std::string label, Ref<float> value, float min, float max, std::string format = "%.2f",
                        std::function<void()> onChange = {}, float step = 0.0f);
        Section& slider(std::string label, int* value, int min, int max, std::function<void()> onChange = {});
        Section& slider(std::string label, Ref<int> value, int min, int max, std::function<void()> onChange = {});
        // Left/right (or a click) cycles; wraps around.
        Section& choice(std::string label, int* value, std::vector<std::string> options, std::function<void()> onChange = {});
        Section& choice(std::string label, Ref<int> value, std::vector<std::string> options, std::function<void()> onChange = {});
        // Options that change while the game runs (the bones of whichever
        // character is selected): asked for every frame the row shows.
        Section& choice(std::string label, Ref<int> value, std::function<std::vector<std::string>()> options,
                        std::function<void()> onChange = {});
        Section& toggle(std::string label, bool* value, std::function<void()> onChange = {});
        Section& toggle(std::string label, Ref<bool> value, std::function<void()> onChange = {});
        Section& button(std::string label, std::function<void()> onPress);
        // The row added last shows only while `visible` says so.
        Section& showIf(std::function<bool()> visible);
        // The row added last is named by `label` every frame instead of
        // its fixed label (a slot whose player changes: "Mute Sam").
        Section& labelLive(std::function<std::string()> label);
        // The whole section shows only while `visible` says so.
        Section& sectionIf(std::function<bool()> visible);

    private:
        friend class DemoPanelModule;
        explicit Section(DemoPanelModule& owner, std::string title) : m_owner(owner), m_title(std::move(title)) {}
        Section& add(Row row);
        DemoPanelModule& m_owner;
        std::string m_title;
        std::function<bool()> m_visible;
        std::vector<size_t> m_rows; // indices into the owner's rows
    };

    explicit DemoPanelModule(std::string title = "Settings", Side side = Side::Left);
    ~DemoPanelModule() override;
    const char* name() const override { return "DemoPanel"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void frameStart(const UpdateContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // A section by title: the same title gives the same section.
    Section& section(const std::string& title);

    State state() const { return m_state; }
    void setState(State s);
    // True while the panel has the controller and keyboard (Active).
    bool active() const { return m_state == State::Active; }
    // Hide everything (a cutscene, a screenshot); the state is kept.
    void setVisible(bool visible);
    void setTitle(std::string title);
    void setWidth(float dp) { m_width = dp; m_dirty = true; }
    // A small crosshair in the middle of the screen while the player uses
    // a controller: what "click" means without a mouse (the demo acts on
    // the middle of the screen).
    void setPadCrosshair(bool enabled) { m_crosshair = enabled; }
    // F1 shows and hides the ImGui developer panels (on by default; call
    // before init to keep them as they are).
    void setDeveloperPanelsKey(bool enabled) { m_devPanelsKey = enabled; }
    // Esc works like a pause menu (on by default): it opens the panel and
    // goes back out, instead of quitting the game (the window's
    // quit-on-Esc is turned off), and the panel gets a "Quit" row so
    // keyboard and controller players can still leave. Call before init;
    // off keeps Esc and the window as the game set them.
    void setEscapeMenu(bool enabled) { m_escapeMenu = enabled; }

    // Pure helpers, public for the tests.
    static float stepValue(float value, float min, float max, float step, int direction);
    static int cycle(int value, int count, int direction);
    static std::string formatValue(const std::string& format, float value);

private:
    struct Row {
        enum class Kind { Heading, Text, Note, Separator, SliderF, SliderI, Choice, Toggle, Button } kind = Kind::Text;
        std::string label;
        std::function<std::string()> live;
        Ref<float> f;
        Ref<int> i;
        Ref<bool> b;
        float min = 0.0f, max = 1.0f, step = 0.0f;
        std::string format;
        std::vector<std::string> options;
        std::function<std::vector<std::string>()> optionsFn;
        std::function<void()> onChange;
        std::function<bool()> visible;
        std::function<std::string()> liveLabel;
        // Built document: the row, its value text and a slider's fill.
        Rml::Element* el = nullptr;
        Rml::Element* value = nullptr;
        Rml::Element* fill = nullptr;
        Rml::Element* labelEl = nullptr;
        std::string shown, shownLabel;
        float shownFill = -1.0f;
        bool hidden = false;
        bool focusable() const { return kind >= Kind::SliderF; }
    };
    class Listener;
    friend class Section;

    void build();
    void input(float dt);
    void draw();
    void refresh();
    void applyState();
    void move(int direction);          // focus up/down among visible rows
    void change(int direction);        // left/right on the focused row
    void press();                      // accept on the focused row
    void change(size_t row, int direction);
    void press(size_t row);
    void setSliderFromMouse(size_t row, float mouseX);
    void onClick(Rml::Element* target, float mouseX, bool down);
    std::vector<size_t> order() const; // every row, section by section
    bool rowVisible(size_t row) const;
    std::string prompts(const std::string& text) const;

    Application* m_app = nullptr;
    InputModule* m_input = nullptr;
    std::string m_title;
    Side m_side;
    float m_width = 330.0f;
    std::deque<Section> m_sections;
    std::vector<Row> m_rows;
    size_t m_hideRow = 0;              // the "Hide panel" row, always last
    size_t m_quitRow = SIZE_MAX;       // "Quit" just above it (setEscapeMenu), or none
    State m_state = State::Open;
    bool m_visible = true;
    bool m_docShown = true;
    bool m_devPanelsKey = true;
    bool m_escapeMenu = true;
    bool m_crosshair = false, m_crosshairShown = false;
    bool m_dirty = true;
    size_t m_focus = SIZE_MAX; // none yet: the first time Active, the first row that does something
    Rml::ElementDocument* m_doc = nullptr;
    Rml::Element* m_hint = nullptr;
    Rml::Element* m_titleEl = nullptr;
    Rml::Element* m_rowsEl = nullptr;
    std::string m_hintShown;
    std::unique_ptr<Listener> m_listener;
    long long m_dragRow = -1;          // slider under a held mouse button
    float m_held[2] = {};              // left/right held time (pad), for repeats
    float m_repeatAt[2] = {};
    float m_vHeld[2] = {}, m_vRepeatAt[2] = {};
};

} // namespace kke
