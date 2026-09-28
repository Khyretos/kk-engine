#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

namespace kke {
class UiModule;
}

namespace kke_sandbox {

class FormPanelListener;

// A Build mode panel (Assets, Sandbox) in RmlUi, written like an ImGui
// window: every frame the sandbox says which rows it holds, between
// begin() and end(), and a row returns true in the frame the player
// changed or pressed it. Making things in the game is RmlUi; ImGui is
// kept for the developer panels behind F1 (docs/DEMO_PANEL.md).
//
// The document is rebuilt only when the rows themselves change (one
// added, a label renamed, a picture ready); values are updated in place,
// so a text field keeps the keyboard while you type.
//
// Everything is a click: the gamepad's cursor is the mouse (A clicks and
// holds), so a pad, a finger and a mouse use the panel the same way.
// Holding - or + keeps stepping; a bar can be dragged; the right stick
// scrolls the panel under the cursor (scroll()).
class FormPanel {
public:
    enum class Tone { Normal, Muted, Good, Warn };

    // `style` places the panel: CSS for its box ("left: 10dp; top: 10dp;
    // width: 330dp;").
    FormPanel(std::string name, std::string style);
    ~FormPanel();
    FormPanel(const FormPanel&) = delete;
    FormPanel& operator=(const FormPanel&) = delete;

    // Needs a UiModule's context; without one nothing is shown.
    void attach(Rml::Context* context, const kke::UiModule& ui);
    // Closes the document; before the UiModule shuts RmlUi down.
    void detach();

    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    // now: seconds (for + and - repeating); mouse: window points and the
    // left button, the gamepad's cursor included.
    void begin(double now, glm::vec2 mouse, bool mouseDown);
    void end();

    // A title that folds what follows: returns whether it is open.
    bool section(const std::string& title, bool openByDefault = true);
    void text(const std::string& text, Tone tone = Tone::Normal);
    // Buttons that follow each other sit side by side.
    bool button(const std::string& label, bool enabled = true);
    // A small square button, for an on-screen keyboard's keys; newline()
    // starts the next row.
    bool key(const std::string& label);
    void newline();
    bool toggle(const std::string& label, bool& value);
    // < value >: the arrows step, a click on the value steps forward.
    bool choice(const std::string& label, int& index, const std::vector<std::string>& options);
    // format: printf for the value ("%.0f deg"); step 0 = 1/50 of the range.
    // started: set in the frame an edit begins (a press, not a repeat or a
    // drag), for one undo step per edit.
    bool slider(const std::string& label, float& value, float min, float max, const std::string& format, float step = 0.0f,
                bool* started = nullptr);
    bool slider(const std::string& label, int& value, int min, int max);
    // - value +, no ends (a position).
    bool number(const std::string& label, float& value, float step, const std::string& format, bool* started = nullptr);
    // A swatch and three bars (red, green, blue), 0..1.
    bool colour(const std::string& label, glm::vec3& value);
    bool textField(const std::string& label, std::string& value);
    // A picture with its name under it; tiles that follow each other flow
    // in rows. image: a PNG on disk, or empty for `word` instead.
    bool tile(const std::string& label, const std::string& image, const std::string& word, bool on);

    // Whether a point (window points, like SDL's mouse) is on the panel.
    bool contains(glm::vec2 point) const;
    // Scrolls the panel by `points` (the right stick over it).
    void scroll(float points);
    // A text field has the keyboard: the sandbox's shortcuts wait.
    bool typing() const;
    // Takes the keyboard back from a text field (Enter, Esc).
    void blur();

private:
    friend class FormPanelListener;
    enum class Kind { Section, Text, Button, Key, Break, Toggle, Choice, Slider, Number, Colour, TextField, Tile };
    enum class Part { Press, Minus, Plus, Bar, Text };
    struct Event {
        size_t row = 0;
        Part part = Part::Press;
        int sub = 0;          // which bar (a colour has three)
        float fraction = 0.0f; // along the bar
        bool start = false;   // a press, not a repeat or a drag
        std::string text;
    };
    struct Row {
        Kind kind = Kind::Text;
        std::string sig;      // what it is: the same sig, the same row
        std::string builtSig; // what the document shows
        std::string label, value, image, word;
        Tone tone = Tone::Normal;
        float fill[3] = { 0.0f, 0.0f, 0.0f };
        glm::vec3 swatch{ 0.0f };
        bool on = false, enabled = true;
        // What the document shows, to touch it only on change.
        Rml::Element* valueEl = nullptr;
        Rml::Element* stateEl = nullptr;
        Rml::Element* fillEl[3] = { nullptr, nullptr, nullptr };
        std::string shownValue;
        float shownFill[3] = { -1.0f, -1.0f, -1.0f };
        int shownState = -1;
    };

    size_t add(Kind kind, std::string sig);
    std::vector<Event> take(size_t row);
    void build();
    void refresh();
    std::string rowRml(size_t i) const;
    float barFraction(size_t row, int sub, float pixelX) const;
    void pressed(size_t row, Part part, int sub, float pixelX);
    void typed(size_t row, std::string text);

    std::string m_name, m_style;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    std::unique_ptr<FormPanelListener> m_listener;
    const kke::UiModule* m_ui = nullptr; // window points <-> context pixels (toContext/toPoints)
    bool m_visible = false;

    std::vector<Row> m_rows;
    size_t m_count = 0; // rows declared this frame
    std::vector<Event> m_events;
    std::set<std::string> m_folded, m_seenSections;

    double m_now = 0.0;
    glm::vec2 m_mouse{ 0.0f };
    // A bar being dragged, or - / + held (repeats after 0.4 s).
    long m_dragRow = -1;
    int m_dragSub = 0;
    long m_holdRow = -1;
    Part m_holdPart = Part::Plus;
    double m_holdSince = 0.0, m_holdNext = 0.0;
};

} // namespace kke_sandbox
