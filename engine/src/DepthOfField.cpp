#include "kke/DepthOfField.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {
constexpr float kSensorHeight = 24.0f; // mm, a full-frame sensor
}

float dofFocalLength(float fovY) { return 0.5f * kSensorHeight / std::tan(0.5f * fovY); }

float dofCircle(float distance, float focus, float focalLengthMm, float fStop, float imageHeight, float maxRadius) {
    // Thin lens: the blur diameter on the sensor is f^2 / N * (d - s) / (d * (s - f)), all in mm.
    const float f = focalLengthMm, d = std::max(distance, 1e-3f) * 1000.0f, s = std::max(focus * 1000.0f, f * 1.001f);
    const float diameter = f * f / fStop * (d - s) / (d * (s - f));
    return std::clamp(0.5f * diameter / kSensorHeight * imageHeight, -maxRadius, maxRadius);
}

float dofFollow(float current, float target, float speed, float dt) {
    if (current <= 0.0f) return target;
    const float t = 1.0f - std::exp(-speed * dt);
    return std::exp(std::log(current) + (std::log(target) - std::log(current)) * t);
}

glm::vec2 dofTap(int index, int count) {
    const float r = std::sqrt((static_cast<float>(index) + 0.5f) / static_cast<float>(count));
    const float a = static_cast<float>(index) * 2.39996323f; // the golden angle
    return glm::vec2(std::cos(a), std::sin(a)) * r;
}

} // namespace kke
