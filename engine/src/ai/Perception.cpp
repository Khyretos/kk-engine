#include "kke/ai/Perception.h"

#include <algorithm>
#include <cmath>

namespace kke::ai {

namespace {
glm::vec3 flat(const glm::vec3& v) { return { v.x, 0.0f, v.z }; }
float smoothFade(float t) { // 1 at 0, 0 at 1, smooth
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
} // namespace

float sightStrength(const glm::vec3& eye, const glm::vec3& forward, const Senses& s, const glm::vec3& target, const Conspicuity& c) {
    const float range = s.sightRange * std::max(c.size, 0.0f) * std::max(c.visibility, 0.0f);
    if (range <= 0.0f) return 0.0f;
    const glm::vec3 to = flat(target - eye);
    const float dist = std::sqrt(glm::dot(to, to));
    if (dist >= range) return 0.0f;
    if (dist < 1e-4f) return 1.0f;
    const glm::vec3 fwd = flat(forward);
    const float fwdLen = std::sqrt(glm::dot(fwd, fwd));
    float cone = 1.0f;
    if (fwdLen > 1e-4f && s.fovDegrees < 360.0f) {
        const float cosAngle = glm::dot(to / dist, fwd / fwdLen);
        const float angle = std::acos(std::clamp(cosAngle, -1.0f, 1.0f));
        const float half = glm::radians(s.fovDegrees * 0.5f);
        if (angle > half) return 0.0f;
        // Full in the middle two thirds of the cone, fading to half at the edge
        // (peripheral vision notices, but less).
        const float edge = half * 0.66f;
        if (angle > edge) cone = 1.0f - 0.5f * (angle - edge) / std::max(half - edge, 1e-4f);
    }
    // Sharp up close, fading over the far half of the range.
    const float distance = dist < range * 0.5f ? 1.0f : smoothFade((dist - range * 0.5f) / (range * 0.5f));
    return std::clamp(distance * cone, 0.0f, 1.0f);
}

float touchStrength(const glm::vec3& self, const Senses& s, const glm::vec3& target) {
    if (s.touchRange <= 0.0f) return 0.0f;
    const glm::vec3 d = flat(target - self);
    const float dist = std::sqrt(glm::dot(d, d));
    if (dist >= s.touchRange) return 0.0f;
    return dist <= s.touchRange * 0.5f ? 1.0f : smoothFade((dist - s.touchRange * 0.5f) / (s.touchRange * 0.5f));
}

float hearingStrength(const glm::vec3& ear, const Senses& s, const Noise& n) {
    const float range = n.loudness * s.hearing;
    if (range <= 0.0f) return 0.0f;
    const glm::vec3 d = n.position - ear;
    const float dist = std::sqrt(glm::dot(d, d));
    if (dist >= range) return 0.0f;
    // Loud and clear over the first third, then fading.
    return dist < range / 3.0f ? 1.0f : smoothFade((dist - range / 3.0f) / (range * 2.0f / 3.0f));
}

void ScentField::emit(uint32_t source, const glm::vec3& position, float strength) {
    for (Last& l : m_last) {
        if (l.source != source) continue;
        const glm::vec3 d = flat(position - l.position);
        if (glm::dot(d, d) < minSpacing * minSpacing) return;
        l.position = position;
        m_marks.push_back({ position, std::clamp(strength, 0.0f, 1.0f), source });
        return;
    }
    m_last.push_back({ source, position });
    m_marks.push_back({ position, std::clamp(strength, 0.0f, 1.0f), source });
}

void ScentField::update(float dt) {
    const float fade = fadePerSecond * dt;
    for (Mark& m : m_marks) {
        m.strength -= fade;
        m.position += flat(wind) * dt;
    }
    m_marks.erase(std::remove_if(m_marks.begin(), m_marks.end(), [](const Mark& m) { return m.strength <= 0.0f; }), m_marks.end());
}

void ScentField::forget(uint32_t source) {
    m_marks.erase(std::remove_if(m_marks.begin(), m_marks.end(), [source](const Mark& m) { return m.source == source; }), m_marks.end());
    m_last.erase(std::remove_if(m_last.begin(), m_last.end(), [source](const Last& l) { return l.source == source; }), m_last.end());
}

void ScentField::smell(const glm::vec3& nose, const Senses& s, std::vector<Smelled>& out) const {
    if (s.smell <= 0.0f) return;
    const glm::vec3 windFlat = flat(wind);
    const float windSpeed = std::sqrt(glm::dot(windFlat, windFlat));
    const size_t first = out.size();
    for (const Mark& m : m_marks) {
        glm::vec3 d = flat(nose - m.position);
        const float dist = std::sqrt(glm::dot(d, d));
        float range = baseRange * s.smell * m.strength;
        // Downwind of the mark, the scent carries further (up to 3x in a
        // strong wind); upwind, much less.
        if (windSpeed > 0.01f && dist > 1e-4f) {
            const float along = glm::dot(d / dist, windFlat / windSpeed);
            range *= 1.0f + along * std::min(windSpeed, 4.0f) * 0.5f;
        }
        if (range <= 0.0f || dist >= range) continue;
        const float strength = m.strength * smoothFade(dist / range);
        bool merged = false;
        for (size_t i = first; i < out.size(); ++i) {
            if (out[i].source != m.source) continue;
            if (strength > out[i].strength) out[i] = { m.source, m.position, strength };
            merged = true;
            break;
        }
        if (!merged) out.push_back({ m.source, m.position, strength });
    }
}

int updateAwareness(Awareness& a, float stimulus, float dt, float now, const Senses& s, const AwarenessThresholds& t) {
    const bool wasSpotted = a.spotted;
    if (stimulus > 0.0f) {
        // Stronger stimuli fill it faster; a very strong one (a bark next to
        // you) is near instant.
        a.level = std::min(1.0f, a.level + s.awarenessGain * stimulus * stimulus * dt * (1.0f + 4.0f * stimulus));
        a.lastSensedAt = now;
    } else {
        a.level = std::max(0.0f, a.level - s.awarenessDecay * dt);
    }
    if (!a.spotted && a.level >= t.spotted) a.spotted = true;
    if (a.spotted && (a.level < t.lost || now - a.lastSensedAt > s.memorySeconds)) a.spotted = false;
    if (a.spotted && !wasSpotted) return 1;
    if (!a.spotted && wasSpotted) return -1;
    return 0;
}

} // namespace kke::ai
