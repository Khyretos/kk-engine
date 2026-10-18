#include "kke/MotionBlur.h"

#include <algorithm>

namespace kke {

glm::vec2 motionPrevious(const glm::vec2& uv, float depth, const glm::mat4& reproject) {
    const glm::vec4 prev = reproject * glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
    return glm::vec2(prev) / prev.w * 0.5f + 0.5f;
}

glm::vec2 motionSmear(const glm::vec2& velocityPixels, float shutter, float maxPixels) {
    const glm::vec2 v = velocityPixels * shutter;
    const float length = glm::length(v);
    return length > maxPixels ? v * (maxPixels / length) : v;
}

float motionViewDistance(float depth, float a, float b) { return b / (depth + a); }

float motionInFront(float nearer, float farther) {
    const float soft = 0.02f * std::max(farther, 1e-3f);
    return std::clamp((farther - nearer) / soft + 0.5f, 0.0f, 1.0f);
}

} // namespace kke
