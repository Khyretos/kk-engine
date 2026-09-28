#pragma once

#include "CommandInput.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <functional>
#include <string>
#include <vector>

namespace kke {
class Application;
}
namespace Rml {
class ElementDocument;
}

namespace command_kit {

// The HUD every command demo shares (ui/command_hud.rml): a status panel,
// big picture buttons along the bottom (Simple mode: one tap, one order),
// the radial wheel, the controller reticle, the drag-box, a toast for
// what just happened and a line of controls.
class CommandHud {
public:
    struct Line { std::string text, color = "#e8ecf4"; };
    // On a touch screen the buttons show no key (they're tapped), and the
    // document's body gets the class "touch" (with kke-phone and
    // kke-portrait/kke-landscape from UiModule) for its phone layout.
    // `key` and the hint are prompt text: "{pet.come}" shows the button
    // for that action on the device in use (kke/ButtonPrompts.h), "{touch:tap}"
    // a gesture; the rest is plain text.
    struct Button { std::string icon, label, key; bool on = false; };
    struct WheelItem { std::string icon, label; };

    explicit CommandHud(kke::Application& app);
    ~CommandHud();
    bool build(const std::string& title);

    std::function<void(int button)> onButton;
    void setButtons(std::vector<Button> buttons);
    void setWheel(std::vector<WheelItem> items);
    void setLines(std::vector<Line> lines);
    void setHint(const std::string& hint);
    void toast(const std::string& text, float seconds = 2.0f);
    void update(const CommandInput::Frame& in, float dt);
    // Whether a point (window points) is on one of the buttons.
    bool overButtons(const glm::vec2& points);

    struct Item { std::string icon, label, left = "0px", top = "0px"; bool picked = false; };
    struct ButtonView { std::string icon, label, key; bool on = false; };
    struct LineView { std::string text, color = "#e8ecf4"; };

private:
    std::string prompts(const std::string& text) const; // prompt text -> RML
    bool touch() const; // prompts are for a touch screen

    kke::Application& m_app;
    Rml::DataModelHandle m_model;
    Rml::ElementDocument* m_doc = nullptr;
    std::string m_title, m_hint, m_toast;
    // Never empty: an empty data-style value is an RmlUi parse warning.
    std::string m_boxLeft = "0px", m_boxTop = "0px", m_boxWidth = "0px", m_boxHeight = "0px", m_wheelLeft = "0px", m_wheelTop = "0px";
    std::vector<ButtonView> m_buttons;
    std::vector<LineView> m_lines;
    std::vector<Item> m_items;
    std::vector<WheelItem> m_wheel;
    bool m_reticle = false, m_box = false, m_wheelOpen = false;
    float m_toastLeft = 0.0f;
};

} // namespace command_kit
