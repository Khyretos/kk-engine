#pragma once

#include "kke/TouchControls.h"

#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
class EventListener;
} // namespace Rml

namespace kke {

class InputModule;

// Draws the touch controls (kke/TouchControls.h) as an RmlUi document
// over the game's HUD, and their edit bar: Done, Reset, size, what a
// button does, hold or toggle, add and remove, fade and look speed. The
// controls take no pointer events (InputModule claims the fingers on
// them); only the bar does. Owned by UiModule.
class TouchOverlay {
public:
    TouchOverlay(Rml::Context* context, InputModule* input);
    ~TouchOverlay();
    TouchOverlay(const TouchOverlay&) = delete;
    TouchOverlay& operator=(const TouchOverlay&) = delete;

    // Before the context's Update(); `origin` is the safe area's corner
    // in frame pixels (the context's 0,0).
    void update(float originX, float originY);
    void command(const std::string& what);

private:
    class Listener;
    void rebuild();
    std::string labelOf(const TouchControl& c, bool keepColon = false) const;
    std::vector<std::string> labelsOf(const std::vector<TouchControl>& layout) const;

    Rml::Context* m_context = nullptr;
    InputModule* m_input = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    Rml::Element* m_root = nullptr;
    Rml::Element* m_controls = nullptr;
    Rml::Element* m_bar = nullptr;
    Rml::Element* m_info = nullptr;
    Rml::Element* m_lookHint = nullptr;
    std::unique_ptr<Listener> m_listener;
    std::vector<TouchControl> m_built; // the layout the elements show
    std::vector<std::string> m_builtLabels;
    std::vector<Rml::Element*> m_elements, m_knobs;
    std::string m_infoShown;
    bool m_shown = false, m_barShown = false;
};

} // namespace kke
