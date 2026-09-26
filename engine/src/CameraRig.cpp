#include "kke/CameraRig.h"

#include <algorithm>
#include <cmath>

namespace kke {

void CameraRig::addLook(float yawDegrees, float pitchDegrees) {
    yaw += yawDegrees;
    yaw = std::fmod(yaw, 360.0f);
    pitch = std::clamp(pitch + pitchDegrees, settings.pitchMin, settings.pitchMax);
}

glm::vec3 CameraRig::viewDirection() const {
    const float y = glm::radians(yaw), p = glm::radians(pitch);
    return glm::vec3(std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p));
}

glm::vec3 CameraRig::forward() const {
    const float y = glm::radians(yaw);
    return glm::vec3(std::sin(y), 0.0f, -std::cos(y));
}

glm::vec3 CameraRig::right() const {
    const float y = glm::radians(yaw);
    return glm::vec3(std::cos(y), 0.0f, std::sin(y));
}

void CameraRig::setCinematic(std::vector<Keyframe> keys, bool loop) {
    std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    m_keys = std::move(keys);
    m_loop = loop;
    m_cinematicTime = 0.0f;
}

namespace {
glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
} // namespace

void CameraRig::update(float dt, const glm::vec3& focus, const RayFn& ray, Camera& out) {
    out.fovDegrees = settings.fovDegrees;
    // Lagged focus: the camera trails the character slightly (1st-order
    // exponential smoothing, frame-rate independent).
    if (!m_hasLagged || settings.positionLag <= 0.0f) {
        m_lagged = focus;
        m_hasLagged = true;
    } else {
        m_lagged += (focus - m_lagged) * (1.0f - std::exp(-settings.positionLag * dt));
    }
    const glm::vec3 dir = viewDirection();
    switch (mode) {
    case Mode::FirstPerson: {
        // No lag in first person: the view is the body.
        glm::vec3 eye = focus + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
        out.position = eye;
        out.target = eye + dir;
        break;
    }
    case Mode::ThirdPerson: {
        const glm::vec3 pivot = m_lagged + glm::vec3(0.0f, settings.pivotHeight, 0.0f) + right() * settings.shoulderOffset;
        const glm::vec3 back = -dir;
        float wanted = settings.armLength;
        if (ray) {
            float hit = ray(pivot, back, settings.armLength + settings.probeRadius);
            wanted = std::max(0.1f, std::min(settings.armLength, hit - settings.probeRadius));
        }
        // In instantly, out gently (UE SpringArm's behaviour).
        if (m_arm < 0.0f || wanted < m_arm) m_arm = wanted;
        else m_arm = std::min(wanted, m_arm + settings.armReturnSpeed * dt);
        out.position = pivot + back * m_arm;
        out.target = pivot + dir;
        break;
    }
    case Mode::Orbit:
        out.position = focus - dir * settings.orbitDistance;
        out.target = focus;
        break;
    case Mode::Cinematic: {
        if (m_keys.size() < 2) {
            if (!m_keys.empty()) { out.position = m_keys[0].position; out.target = m_keys[0].target; }
            break;
        }
        m_cinematicTime += dt;
        const float end = m_keys.back().time, start = m_keys.front().time;
        float t = m_cinematicTime + start;
        if (m_loop && end > start) t = start + std::fmod(t - start, end - start);
        t = std::clamp(t, start, end);
        size_t i = 0;
        while (i + 2 < m_keys.size() && m_keys[i + 1].time < t) ++i;
        const Keyframe& k1 = m_keys[i];
        const Keyframe& k2 = m_keys[i + 1];
        const Keyframe& k0 = i > 0 ? m_keys[i - 1] : k1;
        const Keyframe& k3 = i + 2 < m_keys.size() ? m_keys[i + 2] : k2;
        const float u = (t - k1.time) / std::max(1e-6f, k2.time - k1.time);
        out.position = catmullRom(k0.position, k1.position, k2.position, k3.position, u);
        out.target = catmullRom(k0.target, k1.target, k2.target, k3.target, u);
        break;
    }
    }
    out.up = glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace kke
