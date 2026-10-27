#include "kke/CameraDirector.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float smooth(float w) {
    w = std::clamp(w, 0.0f, 1.0f);
    return w * w * (3.0f - 2.0f * w);
}

// fov 0 in a shot = keep the gameplay camera's.
float fovOf(const CameraDirector::Shot& s, float gameplay) { return s.fovDegrees > 0.0f ? s.fovDegrees : gameplay; }

Camera mix(const Camera& a, const Camera& b, float w) {
    Camera c = a;
    c.position = a.position + (b.position - a.position) * w;
    c.target = a.target + (b.target - a.target) * w;
    c.fovDegrees = a.fovDegrees + (b.fovDegrees - a.fovDegrees) * w;
    c.up = glm::vec3(0.0f, 1.0f, 0.0f);
    return c;
}

} // namespace

CameraDirector::Shot CameraDirector::sample(const std::vector<Shot>& shots, float time) {
    if (shots.empty()) return {};
    if (shots.size() == 1) return shots[0];
    const float t = std::clamp(time, shots.front().time, shots.back().time);
    size_t i = 0;
    while (i + 2 < shots.size() && shots[i + 1].time < t) ++i;
    const Shot& k1 = shots[i];
    const Shot& k2 = shots[i + 1];
    const Shot& k0 = i > 0 ? shots[i - 1] : k1;
    const Shot& k3 = i + 2 < shots.size() ? shots[i + 2] : k2;
    const float u = std::clamp((t - k1.time) / std::max(1e-6f, k2.time - k1.time), 0.0f, 1.0f);
    Shot s;
    s.position = catmullRom(k0.position, k1.position, k2.position, k3.position, u);
    s.target = catmullRom(k0.target, k1.target, k2.target, k3.target, u);
    // Field of view eases between its two shots; 0 (gameplay's) on either
    // side stays 0 so the gameplay value fills it in.
    s.fovDegrees = (k1.fovDegrees > 0.0f && k2.fovDegrees > 0.0f) ? k1.fovDegrees + (k2.fovDegrees - k1.fovDegrees) * smooth(u)
                                                                  : (u < 0.5f ? k1.fovDegrees : k2.fovDegrees);
    s.time = t;
    return s;
}

void CameraDirector::begin(Phase phase, float blendIn) {
    // Taking over from another shot: blend from what was on screen, not
    // from gameplay, so there is no jump.
    if (m_phase != Phase::Idle && m_hasLast) m_from = m_last;
    else m_from.reset();
    m_phase = phase;
    m_blendIn = std::max(0.0f, blendIn);
    m_weight = m_blendIn > 0.0f ? 0.0f : 1.0f;
    m_time = 0.0f;
}

void CameraDirector::play(std::vector<Shot> shots, const PathOptions& options) {
    if (shots.empty()) return;
    if (shots.size() == 1) {
        hold(shots[0], options.blendIn);
        return;
    }
    std::stable_sort(shots.begin(), shots.end(), [](const Shot& a, const Shot& b) { return a.time < b.time; });
    const float start = shots.front().time;
    for (Shot& s : shots) s.time -= start;
    m_shots = std::move(shots);
    m_options = options;
    m_blendOut = std::max(0.0f, options.blendOut);
    begin(Phase::Playing, options.blendIn);
}

void CameraDirector::hold(const Shot& shot, float blendIn) {
    m_held = shot;
    begin(Phase::Holding, blendIn);
}

void CameraDirector::release(float blendOut) {
    if (m_phase == Phase::Idle) return;
    m_blendOut = std::max(0.0f, blendOut);
    if (m_blendOut <= 0.0f || !m_hasLast) {
        m_phase = Phase::Idle;
        m_from.reset();
        return;
    }
    // From whatever is on screen now (even half way through a blend in)
    // back to gameplay.
    m_held = Shot{ m_last.position, m_last.target, m_last.fovDegrees, 0.0f };
    m_from.reset();
    m_phase = Phase::BlendingOut;
    m_weight = 1.0f;
}

void CameraDirector::stop() {
    m_phase = Phase::Idle;
    m_from.reset();
    m_shake = m_shakeLeft = 0.0f;
}

void CameraDirector::shake(float strength, float seconds) {
    if (strength <= 0.0f || seconds <= 0.0f) return;
    // What is left of the current shake, plus the new one, at most 1.
    const float now = m_shakeLeft > 0.0f ? m_shake * (m_shakeLeft / m_shakeTotal) : 0.0f;
    m_shake = std::min(1.0f, now + strength);
    m_shakeTotal = m_shakeLeft = std::max(seconds, m_shakeLeft);
}

CameraDirector::Result CameraDirector::update(float dt, Camera& camera) {
    Result result;
    dt = std::max(0.0f, dt);
    const Camera gameplay = camera;

    if (m_phase != Phase::Idle) {
        Shot pose;
        if (m_phase == Phase::Playing) {
            m_time += dt;
            const float end = duration();
            float t = m_time;
            if (m_options.loop && end > 0.0f) t = std::fmod(t, end);
            pose = sample(m_shots, t);
            if (!m_options.loop && m_time >= end) {
                result.finished = true;
                pose = m_shots.back();
            }
        } else {
            pose = m_held;
        }
        Camera shot = gameplay;
        shot.position = pose.position;
        shot.target = pose.target;
        shot.fovDegrees = fovOf(pose, gameplay.fovDegrees);
        shot.up = glm::vec3(0.0f, 1.0f, 0.0f);

        Camera shown;
        if (m_phase == Phase::BlendingOut) {
            m_weight -= m_blendOut > 0.0f ? dt / m_blendOut : 1.0f;
            shown = mix(gameplay, shot, smooth(m_weight));
            if (m_weight <= 0.0f) m_phase = Phase::Idle;
        } else {
            if (m_weight < 1.0f) m_weight = std::min(1.0f, m_weight + (m_blendIn > 0.0f ? dt / m_blendIn : 1.0f));
            shown = mix(m_from ? *m_from : gameplay, shot, smooth(m_weight));
            if (m_weight >= 1.0f) m_from.reset();
        }
        camera = shown;
        m_last = shown;
        m_hasLast = true;
        result.changed = true;

        if (result.finished) {
            if (m_options.hold) {
                m_held = m_shots.back();
                m_phase = Phase::Holding; // m_weight stays: a blend in that outlasted the path finishes
            } else {
                release(m_blendOut);
            }
        }
    }

    if (m_shakeLeft > 0.0f) {
        m_shakeTime += dt;
        m_shakeLeft = std::max(0.0f, m_shakeLeft - dt);
        // Trauma squared: strong at first, a soft tail. Sums of sines at
        // unrelated frequencies read as noise without a random generator.
        const float a = m_shake * (m_shakeLeft / m_shakeTotal);
        const float k = a * a;
        const float s = m_shakeTime;
        const glm::vec3 wobble(std::sin(s * 37.0f) + 0.5f * std::sin(s * 71.3f), std::sin(s * 43.7f + 1.3f) + 0.5f * std::sin(s * 89.1f),
                               std::sin(s * 29.3f + 2.1f) + 0.5f * std::sin(s * 61.7f));
        const glm::vec3 turn(std::sin(s * 31.1f + 0.7f), std::sin(s * 47.9f + 2.9f), std::sin(s * 53.3f + 1.7f));
        const float distance = glm::length(camera.target - camera.position);
        camera.position += wobble * (0.08f * k);
        camera.target += wobble * (0.08f * k) + turn * (0.04f * k * std::max(1.0f, distance)); // about 2 degrees at full strength
        result.changed = true;
    }
    return result;
}

} // namespace kke
