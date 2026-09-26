#include "kke/Ocean.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
constexpr float kG = 9.81f;
constexpr float kTwoPi = 6.2831853f;

struct WaveTerms { float k, omega, q; };
WaveTerms terms(const GerstnerWave& w, size_t count) {
    float k = kTwoPi / std::max(0.01f, w.wavelength);
    float omega = std::sqrt(kG * k); // deep-water dispersion
    // Q per wave so the summed steepness never loops the surface over itself.
    float q = w.amplitude > 0.0f ? w.steepness / (k * w.amplitude * static_cast<float>(count)) : 0.0f;
    return { k, omega, q };
}
} // namespace

OceanWaves::OceanWaves() { setWind(6.0f, 0.3f); }

void OceanWaves::setWind(float speed, float dir, float choppiness) {
    m_waves.clear();
    // Dominant wavelength grows with the square of wind speed (fully
    // developed sea); amplitude ~ wavelength / 30, then a spread of
    // shorter, smaller waves around it.
    const float lambda0 = std::max(2.0f, 0.5f * speed * speed);
    const float spread[kMaxWaves] = { 0.0f, 0.55f, -0.45f, 0.25f, -0.7f, 0.9f };
    const float lengthScale[kMaxWaves] = { 1.0f, 0.62f, 0.45f, 0.31f, 0.22f, 0.14f };
    for (int i = 0; i < kMaxWaves; ++i) {
        GerstnerWave w;
        float a = dir + spread[i] * 0.7f;
        w.direction = glm::vec2(std::cos(a), std::sin(a));
        w.wavelength = lambda0 * lengthScale[i];
        w.amplitude = w.wavelength / 30.0f * (i == 0 ? 1.0f : 0.8f);
        w.steepness = choppiness;
        w.phase = i * 1.7f;
        m_waves.push_back(w);
    }
}

glm::vec3 OceanWaves::displacement(const glm::vec2& xz, float t) const {
    glm::vec3 d(0.0f, seaLevel, 0.0f);
    for (const GerstnerWave& w : m_waves) {
        WaveTerms tw = terms(w, m_waves.size());
        float theta = tw.k * glm::dot(w.direction, xz) - tw.omega * t + w.phase;
        float c = std::cos(theta), s = std::sin(theta);
        d.x += tw.q * w.amplitude * w.direction.x * c;
        d.z += tw.q * w.amplitude * w.direction.y * c;
        d.y += w.amplitude * s;
    }
    return d;
}

float OceanWaves::height(const glm::vec2& xz, float t) const {
    // Find the undisturbed point p whose displaced position lands on xz.
    glm::vec2 p = xz;
    for (int i = 0; i < 4; ++i) {
        glm::vec3 d = displacement(p, t);
        p -= (glm::vec2(p.x + d.x, p.y + d.z) - xz);
    }
    return displacement(p, t).y;
}

glm::vec3 OceanWaves::normal(const glm::vec2& xz, float t) const {
    const float e = 0.1f;
    float hx = height(xz + glm::vec2(e, 0.0f), t) - height(xz - glm::vec2(e, 0.0f), t);
    float hz = height(xz + glm::vec2(0.0f, e), t) - height(xz - glm::vec2(0.0f, e), t);
    return glm::normalize(glm::vec3(-hx, 2.0f * e, -hz));
}

glm::vec3 OceanWaves::velocity(const glm::vec2& xz, float t) const {
    const float dt = 0.02f;
    glm::vec3 a = displacement(xz, t), b = displacement(xz, t + dt);
    return (b - a) / dt;
}

void OceanWaves::toGpu(std::vector<glm::vec4>& out) const {
    out.clear();
    for (int i = 0; i < kMaxWaves; ++i) {
        if (i < static_cast<int>(m_waves.size())) {
            const GerstnerWave& w = m_waves[i];
            WaveTerms tw = terms(w, m_waves.size());
            out.push_back(glm::vec4(w.direction, tw.k, w.amplitude));
            out.push_back(glm::vec4(tw.q, tw.omega, w.phase, 0.0f));
        } else {
            out.push_back(glm::vec4(0.0f));
            out.push_back(glm::vec4(0.0f));
        }
    }
}

} // namespace kke
