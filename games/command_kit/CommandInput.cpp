#include "CommandInput.h"

#include "kke/Application.h"
#include "kke/modules/InputModule.h"

#include <algorithm>
#include <cmath>

namespace command_kit {

void CommandInput::defineActions(kke::InputMap& m) {
    using IM = kke::InputModule;
    auto def = [&](const char* id, const char* label, kke::ActionType t = kke::ActionType::Button) {
        m.defineAction({ id, label, "Orders", "game", t });
    };
    def("cmd.context", "Order (whatever is pointed at)");
    def("cmd.wheel", "Order wheel (hold)");
    def("cmd.wheel.pick", "Order wheel: pick (stick)", kke::ActionType::Axis2D);
    def("cmd.cancel", "Order wheel: close");
    def("cmd.force", "Force: focus fire / hold there");
    def("cmd.queue", "Queue after the current order");
    m.addBinding(IM::bind("cmd.context", IM::mouse(SDL_BUTTON_RIGHT)));
    m.addBinding(IM::bind("cmd.context", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
    m.addBinding(IM::bind("cmd.wheel", IM::key(SDL_SCANCODE_TAB), kke::Trigger::Continuous));
    m.addBinding(IM::bind("cmd.wheel", IM::mouse(SDL_BUTTON_MIDDLE), kke::Trigger::Continuous));
    m.addBinding(IM::bind("cmd.wheel", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), kke::Trigger::Continuous));
    // SDL's stick Y is down-positive, which is what the wheel wants: no invert.
    kke::Binding rs = IM::bind("cmd.wheel.pick", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTX), kke::Trigger::Continuous);
    rs.sourceY = IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTY);
    m.addBinding(rs);
    m.addBinding(IM::bind("cmd.cancel", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
    m.addBinding(IM::bind("cmd.force", IM::key(SDL_SCANCODE_LCTRL), kke::Trigger::Continuous));
    kke::Binding lt = IM::bind("cmd.force", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous);
    lt.threshold = 0.4f;
    m.addBinding(lt);
    m.addBinding(IM::bind("cmd.queue", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
}

void CommandInput::onEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        m_pad = true;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // Half a push, like InputModule's button prompts, so the reticle
        // and the prompts switch to the controller together.
        if (std::abs(e.gaxis.value) >= 16384) m_pad = true;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        m_mouse = { e.motion.x, e.motion.y };
        if (m_wheelOpen) m_wheelMouse += glm::vec2(e.motion.xrel, e.motion.yrel);
        if (std::abs(e.motion.xrel) + std::abs(e.motion.yrel) > 2.0f) m_pad = false;
        if (m_press.down) {
            m_press.pos = m_mouse;
            if (glm::length(m_press.pos - m_press.start) > dragPixels) m_press.dragging = true;
        }
        if (m_right.down && !m_right.opened) {
            if (!m_right.moved && glm::length(m_mouse - m_right.start) > dragPixels) m_right.moved = true;
            if (m_right.moved) m_orbitDelta += glm::vec2(e.motion.xrel, e.motion.yrel);
        }
        if (m_middleDown) m_panDelta += glm::vec2(e.motion.xrel, e.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        m_mouse = { e.button.x, e.button.y };
        m_pad = false;
        if (e.button.button == SDL_BUTTON_LEFT) {
            m_press = Press{};
            m_press.down = true;
            m_press.touch = e.button.which == SDL_TOUCH_MOUSEID;
            m_press.start = m_press.pos = m_mouse;
            m_press.blocked = overUi && overUi(m_mouse);
        }
        if (rightButtonGestures && e.button.button == SDL_BUTTON_RIGHT) {
            m_right = RightPress{};
            m_right.down = true;
            m_right.start = m_mouse;
        }
        if (rightButtonGestures && e.button.button == SDL_BUTTON_MIDDLE) m_middleDown = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        m_mouse = { e.button.x, e.button.y };
        if (e.button.button == SDL_BUTTON_LEFT && m_press.down) {
            m_press.down = false;
            m_press.released = true;
            m_press.pos = m_mouse;
        }
        if (e.button.button == SDL_BUTTON_RIGHT && m_right.down) {
            m_right.down = false;
            m_right.released = true;
        }
        if (e.button.button == SDL_BUTTON_MIDDLE) m_middleDown = false;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_press = Press{};
        m_right = RightPress{};
        m_middleDown = false;
        break;
    default: break;
    }
}

void CommandInput::openWheel(const glm::vec2& center, const glm::vec2& target, int source) {
    m_wheelOpen = true;
    m_wheelSource = source;
    m_wheelCenter = center;
    m_wheelTarget = target;
    m_wheelMouse = glm::vec2(0.0f);
    m_wheel.reset();
}

const CommandInput::Frame& CommandInput::update(kke::InputMap& in, kke::Application& app, bool captured, float dt) {
    Frame f;
    int w = 1, h = 1;
    SDL_GetWindowSize(app.window().handle(), &w, &h);
    const glm::vec2 middle(float(w) * 0.5f, float(h) * 0.5f);
    f.reticle = captured || m_pad;
    f.pointer = f.reticle ? middle : m_mouse;
    f.force = in.held("cmd.force");
    f.queue = in.held("cmd.queue");

    // The wheel button: open on press, give on release.
    if (in.pressed("cmd.wheel") && !m_wheelOpen) openWheel(f.reticle ? middle : f.pointer, f.pointer, 1);
    if (m_wheelOpen && m_wheelSource == 1) {
        const glm::vec2 stick = in.axis2("cmd.wheel.pick");
        if (glm::length(stick) > 0.3f) m_wheel.updateStick(stick);
        else if (glm::length(m_wheelMouse) > 0.0f && !m_pad)
            m_wheel.updatePointer(captured ? m_wheelMouse : m_mouse - m_wheelCenter);
        if (in.released("cmd.wheel")) {
            f.wheelGiven = m_wheel.picked();
            m_wheelOpen = false;
        }
    }
    if (m_wheelOpen && in.pressed("cmd.cancel")) {
        m_wheelOpen = false;
        m_wheel.reset();
    }

    // The left button / a finger: click, drag-box, or held for the wheel.
    if (m_press.down && !m_press.blocked) {
        m_press.held += dt;
        if (!m_press.dragging && !m_wheelOpen && m_press.held >= longPressSeconds) openWheel(m_press.start, m_press.start, 2);
    }
    if (m_wheelOpen && m_wheelSource == 2) {
        m_wheel.updatePointer(m_press.pos - m_wheelCenter);
        if (!m_press.down) {
            f.wheelGiven = m_wheel.picked();
            m_wheelOpen = false;
            m_press = Press{};
        }
    }
    if (m_press.down && m_press.dragging && !m_press.blocked && !m_wheelOpen) {
        f.dragging = true;
        f.boxA = m_press.start;
        f.boxB = m_press.pos;
    }
    if (m_press.released) {
        if (!m_press.blocked) {
            if (m_press.dragging) {
                f.boxDone = true;
                f.boxA = m_press.start;
                f.boxB = m_press.pos;
            } else {
                f.click = true;
                f.touch = m_press.touch;
                // A click is where the mouse is, even with a controller plugged in.
                f.pointer = m_press.pos;
                f.reticle = false;
            }
        }
        m_press = Press{};
    }

    // The right button with gestures: a drag turns the view, held still it
    // opens the wheel, a click is the order (the bound right button's own
    // press is that click, so it isn't counted twice).
    const bool rightGesture = rightButtonGestures && (m_right.down || m_right.released);
    if (rightButtonGestures) {
        if (m_right.down && !m_right.moved && !m_right.opened && !m_wheelOpen) {
            m_right.held += dt;
            if (m_right.held >= longPressSeconds) {
                m_right.opened = true;
                openWheel(m_right.start, m_right.start, 3);
            }
        }
        if (m_wheelOpen && m_wheelSource == 3) {
            m_wheel.updatePointer(m_mouse - m_wheelCenter);
            if (!m_right.down) {
                f.wheelGiven = m_wheel.picked();
                m_wheelOpen = false;
            }
        }
        if (m_right.released) {
            if (!m_right.moved && !m_right.opened && !m_wheelOpen) {
                f.pointer = m_right.start;
                f.reticle = false;
                f.context = !(overUi && overUi(f.pointer));
            }
            m_right = RightPress{};
        }
        f.orbit = m_orbitDelta;
        f.pan = m_panDelta;
        m_orbitDelta = m_panDelta = glm::vec2(0.0f);
    }
    if (!m_wheelOpen && !rightGesture && in.pressed("cmd.context")) f.context = !(overUi && !f.reticle && overUi(f.pointer));

    f.wheelOpen = m_wheelOpen;
    f.wheelCenter = m_wheelCenter;
    f.wheelTarget = m_wheelOpen || f.wheelGiven >= 0 ? m_wheelTarget : f.pointer;
    f.wheelPicked = m_wheelOpen ? m_wheel.picked() : -1;
    m_frame = f;
    return m_frame;
}

} // namespace command_kit
