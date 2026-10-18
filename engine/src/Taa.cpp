#include "kke/Taa.h"

#include <algorithm>
#include <cmath>

namespace kke {

float taaHalton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    for (uint32_t i = index; i > 0; i /= base) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
    }
    return r;
}

glm::vec2 taaJitter(uint32_t frame, const TaaSettings& settings) {
    const uint32_t i = frame % std::max(settings.jitterCycle, 1u) + 1; // index 0 is (0, 0): skip it
    return glm::vec2(taaHalton(i, 2), taaHalton(i, 3)) - 0.5f;
}

glm::mat4 taaJitteredProjection(const glm::mat4& proj, const glm::vec2& jitterPixels, const glm::vec2& size) {
    glm::mat4 p = proj;
    // NDC moves by 2 / size per pixel; the third column meets view z, and w = -z (looking down -z).
    p[2][0] -= 2.0f * jitterPixels.x / size.x;
    p[2][1] -= 2.0f * jitterPixels.y / size.y;
    return p;
}

glm::vec2 taaReproject(const glm::vec2& uv, float depth, const glm::mat4& invViewProj, const glm::mat4& prevViewProj) {
    const glm::vec4 world = invViewProj * glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
    const glm::vec4 prev = prevViewProj * (world / world.w);
    return glm::vec2(prev) / prev.w * 0.5f + 0.5f;
}

glm::vec3 taaToYCoCg(const glm::vec3& c) {
    return glm::vec3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b, 0.5f * c.r - 0.5f * c.b, -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

glm::vec3 taaFromYCoCg(const glm::vec3& c) { return glm::vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

glm::vec3 taaClip(const glm::vec3& history, const glm::vec3& mean, const glm::vec3& deviation, float spread) {
    const glm::vec3 half = glm::max(deviation * spread, glm::vec3(1e-4f));
    const glm::vec3 d = history - mean;
    const glm::vec3 units = glm::abs(d / half);
    const float most = std::max(units.x, std::max(units.y, units.z));
    return most > 1.0f ? mean + d / most : history;
}

} // namespace kke
