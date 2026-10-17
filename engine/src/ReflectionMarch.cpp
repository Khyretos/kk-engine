#include "kke/ReflectionMarch.h"

#include <algorithm>

namespace kke {

ReflectionMarchResult reflectionMarch(const ReflectionMarchView& view, const glm::vec3& origin, const glm::vec3& direction, int steps,
                                      float maxDistance, float thickness) {
    ReflectionMarchResult out;
    const glm::vec2 size(static_cast<float>(view.width), static_cast<float>(view.height));
    float reach = maxDistance;
    if (direction.z > 0.0f) reach = std::min(reach, (-0.05f - origin.z) / direction.z); // stop short of the camera plane
    const glm::vec3 end = origin + direction * std::max(reach, 0.0f);
    const glm::vec4 h0 = view.proj * glm::vec4(origin, 1.0f), h1 = view.proj * glm::vec4(end, 1.0f);
    const glm::vec2 s0 = (glm::vec2(h0) / h0.w * 0.5f + 0.5f) * size, s1 = (glm::vec2(h1) / h1.w * 0.5f + 0.5f) * size;
    const float k0 = 1.0f / h0.w, k1 = 1.0f / h1.w; // 1 / view depth: linear on screen
    auto zAt = [&](const glm::vec2& px) { return view.viewZ(static_cast<int>(px.x), static_cast<int>(px.y)); };
    float prev = 0.0f;
    for (int k = 1; k <= steps; ++k) {
        const float f = static_cast<float>(k) / static_cast<float>(steps);
        const glm::vec2 px = glm::mix(s0, s1, f);
        if (px.x < 0.0f || px.y < 0.0f || px.x >= size.x || px.y >= size.y) {
            out.leftScreen = true;
            return out;
        }
        const float rayZ = -1.0f / glm::mix(k0, k1, f);
        const float sz = zAt(px);
        if (sz == 0.0f) {
            prev = f;
            continue; // sky there: keep going
        }
        const float behind = sz - rayZ; // > 0: the ray is behind what the screen shows
        if (behind > 0.0f && behind < thickness) {
            float lo = prev, hi = f;
            for (int b = 0; b < 4; ++b) {
                const float mid = 0.5f * (lo + hi);
                const float mz = zAt(glm::mix(s0, s1, mid));
                if (mz != 0.0f && mz - (-1.0f / glm::mix(k0, k1, mid)) > 0.0f) hi = mid;
                else lo = mid;
            }
            out.hit = true;
            out.pixel = glm::mix(s0, s1, hi);
            return out;
        }
        prev = f;
    }
    return out;
}

} // namespace kke
