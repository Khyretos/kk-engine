#include "kke/Refraction.h"

#include <algorithm>

namespace kke {

RefractionSample refractedSample(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& worldPos, const glm::vec3& normal,
                                 float ior, const std::function<float(const glm::vec2&)>& behindDistance, float maxDepth) {
    auto uvOf = [&](const glm::vec3& p) {
        const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
        return glm::vec2(c) / c.w * 0.5f + 0.5f;
    };
    RefractionSample out;
    out.uv = uvOf(worldPos);
    const glm::vec3 toSurface = worldPos - cameraPos;
    const float glass = glm::length(toSurface);
    const glm::vec3 r = glm::refract(toSurface / glass, glm::normalize(normal), 1.0f / std::max(ior, 1.0f));
    if (glm::dot(r, r) < 1e-6f) return out; // total internal reflection
    // As far as what the straight-through pixel shows behind the glass (the sky: maxDepth).
    const float behind = behindDistance(out.uv);
    const float depth = behind > 0.0f ? std::clamp(behind - glass, 0.0f, maxDepth) : maxDepth;
    const glm::vec2 uv = uvOf(worldPos + r * depth);
    if (uv.x < 0.0f || uv.y < 0.0f || uv.x > 1.0f || uv.y > 1.0f) return out;
    const float there = behindDistance(uv);
    if (there > 0.0f && there < glass) return out; // in front of the glass: not seen through it
    out.uv = uv;
    out.bent = true;
    return out;
}

} // namespace kke
