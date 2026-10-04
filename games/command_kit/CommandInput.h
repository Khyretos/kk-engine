#pragma once

#include "kke/InputMap.h"
#include "kke/Orders.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <functional>

namespace kke {
class Application;
}

namespace command_kit {

// Mouse, keyboard, controller and touch turned into the few things an
// order is made of (docs/COMMANDS.md, "Controls"): a pointer (the mouse
// cursor, a finger, or the reticle in the middle of the view for a
// controller or a captured mouse), a click on it, a context order on it,
// a drag-box, and a pick from the radial wheel. Which order each becomes
// is the game's call.
//
// Actions (rebindable, "Orders" in the bindings screen):
//   cmd.context  right mouse, RB            the obvious order for what's under the pointer
//   cmd.wheel    hold Tab / middle mouse, LB  the radial wheel (right stick / mouse picks)
//   cmd.cancel   B                          closes the wheel without an order
//   cmd.force    Ctrl, LT                   focus fire / hold there instead
//   cmd.queue    Shift                      after the current order
// Touch: a tap is a click, a held finger opens the wheel under it.
class CommandInput {
public:
    static void defineActions(kke::InputMap& m);

    // True where the pointer is over the HUD's own buttons (they take the click).
    std::function<bool(const glm::vec2& points)> overUi;

    void onEvent(const SDL_Event& e);

    struct Frame {
        glm::vec2 pointer{0.0f};   // window points
        bool reticle = false;      // the pointer is the middle of the view (controller, captured mouse)
        bool click = false;        // left click / tap at `pointer` (not a drag)
        bool touch = false;        // ... made by a finger
        bool context = false;      // give the context order at `pointer`
        bool boxDone = false;      // a drag-box was let go: select what's inside
        bool dragging = false;     // a drag-box is being drawn
        glm::vec2 boxA{0.0f}, boxB{0.0f};
        bool wheelOpen = false;
        glm::vec2 wheelCenter{0.0f}; // window points
        glm::vec2 wheelTarget{0.0f}; // what the order will be about (the pointer when the wheel opened)
        int wheelPicked = -1;
        int wheelGiven = -1;       // an item was picked and given this frame
        bool force = false, queue = false;
        glm::vec2 orbit{0.0f};     // rightButtonGestures: right-drag this frame (window points)
        glm::vec2 pan{0.0f};       // rightButtonGestures: middle-drag this frame (window points)
    };
    // `captured`: the mouse turns the camera (the pointer is the reticle).
    const Frame& update(kke::InputMap& in, kke::Application& app, bool captured, float dt);
    const Frame& frame() const { return m_frame; }

    kke::RadialMenu& wheel() { return m_wheel; }
    bool wheelOpen() const { return m_wheelOpen; }
    bool padActive() const { return m_pad; } // the last thing touched was a controller
    float longPressSeconds = 0.5f;
    float dragPixels = 12.0f;                 // window points a press may move and still be a click
    // The right mouse button as a camera and an order at once (Platoon):
    // a click is the context order where it was pressed, held still it
    // opens the wheel there, dragged it turns the camera (Frame::orbit).
    // The middle button then drags the view (Frame::pan), so the game
    // takes it off cmd.wheel. Off: the right button is just cmd.context.
    bool rightButtonGestures = false;

private:
    void openWheel(const glm::vec2& center, const glm::vec2& target, int source);
    kke::RadialMenu m_wheel{ 8 };
    Frame m_frame;
    bool m_pad = false;
    // The left button / one finger: where it went down and what it has become.
    struct Press {
        bool down = false, touch = false, dragging = false, released = false, blocked = false;
        glm::vec2 start{0.0f}, pos{0.0f};
        float held = 0.0f;
    } m_press;
    // The right button with rightButtonGestures: a click, a hold or a drag.
    struct RightPress {
        bool down = false, moved = false, released = false, opened = false;
        glm::vec2 start{0.0f};
        float held = 0.0f;
    } m_right;
    bool m_middleDown = false;
    glm::vec2 m_orbitDelta{0.0f}, m_panDelta{0.0f};
    bool m_wheelOpen = false;
    int m_wheelSource = 0; // 1 = the wheel button (stick or mouse), 2 = a held press, 3 = a held right button
    glm::vec2 m_wheelCenter{0.0f}, m_wheelTarget{0.0f}, m_wheelMouse{0.0f};
    glm::vec2 m_mouse{0.0f};
};

} // namespace command_kit
