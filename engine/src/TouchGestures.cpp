#include "kke/TouchGestures.h"

#include <algorithm>
#include <cmath>

namespace kke {

TouchGestures::Finger* TouchGestures::find(uint64_t id) {
    for (Finger& f : m_fingers)
        if (f.id == id) return &f;
    return nullptr;
}

void TouchGestures::fingerDown(uint64_t id, const glm::vec2& pos) {
    if (Finger* f = find(id)) {
        f->pos = pos; // a repeated down (lost up event): just move it
        return;
    }
    m_fingers.push_back({ id, pos });
    if (m_fingers.size() >= 2) m_multi = true;
}

void TouchGestures::fingerMove(uint64_t id, const glm::vec2& pos) {
    Finger* f = find(id);
    if (!f) return;
    const bool inGesture = m_fingers.size() >= 2 && (f == &m_fingers[0] || f == &m_fingers[1]);
    if (!inGesture) {
        f->pos = pos;
        return;
    }
    // One finger of the pair moved: compare the pair before and after.
    const glm::vec2 a0 = m_fingers[0].pos, b0 = m_fingers[1].pos;
    f->pos = pos;
    const glm::vec2 a1 = m_fingers[0].pos, b1 = m_fingers[1].pos;
    const glm::vec2 d0 = b0 - a0, d1 = b1 - a1;
    const float len0 = glm::length(d0), len1 = glm::length(d1);
    m_frame.pan += (a1 + b1) * 0.5f - (a0 + b0) * 0.5f;
    if (len0 > 1e-3f && len1 > 1e-3f) {
        m_frame.pinch *= len1 / len0;
        // Screen y points down, so negate to make counter-clockwise positive.
        float twist = -(std::atan2(d1.y, d1.x) - std::atan2(d0.y, d0.x));
        if (twist > 3.14159265f) twist -= 6.28318531f;
        if (twist < -3.14159265f) twist += 6.28318531f;
        m_frame.twist += twist;
    }
    m_frame.active = true;
}

void TouchGestures::fingerUp(uint64_t id) {
    m_fingers.erase(std::remove_if(m_fingers.begin(), m_fingers.end(), [&](const Finger& f) { return f.id == id; }), m_fingers.end());
    if (m_fingers.empty()) m_multi = false;
}

void TouchGestures::clear() {
    m_fingers.clear();
    m_multi = false;
    m_frame = Frame{};
}

TouchGestures::Frame TouchGestures::take() {
    Frame f = m_frame;
    m_frame = Frame{};
    return f;
}

} // namespace kke
