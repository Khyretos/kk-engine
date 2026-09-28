#include "Stunts.h"

#include <algorithm>
#include <cmath>

namespace flying {

namespace {
constexpr float kChainWindow = 3.0f;
} // namespace

void StuntTracker::reset() { *this = StuntTracker{}; }

void StuntTracker::crashed() {
    m_score = std::max(0, m_score - 300);
    m_chain = 0;
    m_started = false;
    m_pitch = m_roll = m_pitchTime = m_rollTime = 0.0f;
    m_inverted = m_knife = m_low = 0.0f;
}

StuntTracker::Trick StuntTracker::award(const std::string& name, int points) {
    m_chain = m_sinceTrick <= kChainWindow ? m_chain + 1 : 1;
    m_sinceTrick = 0.0f;
    Trick t;
    t.name = name;
    t.chain = m_chain;
    t.points = static_cast<int>(std::lround(static_cast<float>(points) * (1.0f + 0.5f * static_cast<float>(m_chain - 1))));
    m_score += t.points;
    return t;
}

StuntTracker::Trick StuntTracker::update(const PlaneState& s, float clearance, float dt) {
    Trick none;
    m_sinceTrick += dt;
    if (s.onGround || s.crashed) {
        m_started = false;
        m_pitch = m_roll = m_pitchTime = m_rollTime = 0.0f;
        m_inverted = m_knife = m_low = 0.0f;
        return none;
    }
    if (!m_started) {
        m_last = s.rotation;
        m_started = true;
        return none;
    }
    // How it turned this step, in its own frame: x = pitch, z = roll.
    glm::quat d = glm::inverse(m_last) * s.rotation;
    m_last = s.rotation;
    if (d.w < 0.0f) d = -d;
    const float angle = 2.0f * std::acos(std::clamp(d.w, -1.0f, 1.0f));
    const float sinHalf = std::sqrt(std::max(0.0f, 1.0f - d.w * d.w));
    const glm::vec3 axis = sinHalf > 1e-6f ? glm::vec3(d.x, d.y, d.z) / sinHalf : glm::vec3(0.0f);
    const glm::vec3 turned = glm::degrees(axis * angle);
    const float pitchStep = turned.x, rollStep = -turned.z; // + = nose up, + = right wing down

    // Loops: the nose round a full circle one way, in 10 s.
    if ((pitchStep > 0.0f) != (m_pitch > 0.0f) && std::abs(pitchStep) > 0.2f) m_pitch = m_pitchTime = 0.0f;
    m_pitch += pitchStep;
    m_pitchTime += dt;
    if (m_pitchTime > 10.0f) m_pitch = m_pitchTime = 0.0f;
    Trick trick;
    if (m_pitch >= 330.0f) {
        trick = award("Loop", 500);
        m_pitch = m_pitchTime = 0.0f;
    } else if (m_pitch <= -330.0f) {
        trick = award("Outside loop", 800);
        m_pitch = m_pitchTime = 0.0f;
    }
    // Rolls: the wings round a full circle one way, in 4 s.
    if ((rollStep > 0.0f) != (m_roll > 0.0f) && std::abs(rollStep) > 0.5f) m_roll = m_rollTime = 0.0f;
    m_roll += rollStep;
    m_rollTime += dt;
    if (m_rollTime > 4.0f) m_roll = m_rollTime = 0.0f;
    if (std::abs(m_roll) >= 330.0f && trick.name.empty()) {
        trick = award("Roll", 200);
        m_roll = m_rollTime = 0.0f;
    }

    // Held tricks: named (and scored) when they end.
    const float bank = std::abs(bankOf(s.rotation));
    const bool inverted = s.up().y < -0.7f;
    const bool knife = bank > 70.0f && bank < 110.0f && std::abs(s.velocity.y) < 5.0f && s.airspeed > 30.0f;
    const bool low = clearance < 15.0f && s.airspeed > 40.0f;
    auto held = [&](float& time, bool on, float minimum, const char* name, int base, int perSecond) {
        if (on) {
            time += dt;
            return;
        }
        if (time >= minimum && trick.name.empty())
            trick = award(std::string(name) + " " + std::to_string(static_cast<int>(time)) + "s", base + static_cast<int>(time * static_cast<float>(perSecond)));
        time = 0.0f;
    };
    held(m_inverted, inverted, 2.0f, "Inverted", 0, 60);
    held(m_knife, knife, 2.0f, "Knife edge", 0, 80);
    held(m_low, low, 1.0f, "Low pass", 150, 100);
    return trick;
}

} // namespace flying
