#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Two-finger gestures from raw touches (SDL_EVENT_FINGER_*): drag, pinch
// and twist, accumulated between take() calls. One finger is not a
// gesture here: SDL turns it into the mouse (touch-to-mouse emulation),
// which is how ImGui and click/drag code already see it. Pure logic,
// tested in tests/test_touch_gestures.cpp; OrbitCameraModule uses it
// (two fingers turn the view, pinch zooms) and games can too.
//
// Positions are in pixels (or anything with the same scale on both
// axes, so pinch and twist aren't skewed by the screen's aspect).
class TouchGestures {
public:
    void fingerDown(uint64_t id, const glm::vec2& pos);
    void fingerMove(uint64_t id, const glm::vec2& pos);
    void fingerUp(uint64_t id);
    void clear();                     // e.g. the window lost focus

    int fingers() const { return static_cast<int>(m_fingers.size()); }
    // True from the moment a second finger lands until every finger is
    // up: what one finger started (a drag, a placement) should be dropped.
    bool multiTouch() const { return m_multi; }

    struct Frame {
        glm::vec2 pan{0.0f};  // movement of the two fingers' midpoint
        float pinch = 1.0f;   // distance between them now / before (> 1: spreading)
        float twist = 0.0f;   // radians, counter-clockwise on screen (y down)
        bool active = false;  // any two-finger movement happened
    };
    // What happened since the last take(); resets it.
    Frame take();

private:
    struct Finger { uint64_t id; glm::vec2 pos; };
    Finger* find(uint64_t id);

    std::vector<Finger> m_fingers; // in the order they landed; the first two make the gesture
    bool m_multi = false;
    Frame m_frame;
};

} // namespace kke
