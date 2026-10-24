#include "kke/ScreenGi.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace kke {

uint32_t screenGiSectors(float t0, float t1) {
    if (t1 <= t0) return 0u; // a sample seen exactly edge-on covers nothing
    const int i0 = std::clamp(static_cast<int>(std::floor(t0 * 32.0f)), 0, 32);
    const int i1 = std::clamp(static_cast<int>(std::ceil(t1 * 32.0f)), 0, 32);
    if (i1 <= i0) return 0u;
    const uint32_t run = i1 - i0 == 32 ? 0xffffffffu : (1u << (i1 - i0)) - 1u;
    return run << i0;
}

float screenGiSector(float a, float n) {
    // Equal steps of sin(a - n) hold equal cosine-weighted solid angle in the slice.
    const float h = std::clamp(a - n, -1.5707963f, 1.5707963f);
    return 0.5f + 0.5f * std::sin(h);
}

glm::vec3 screenGiViewPos(const ScreenGiView& view, float x, float y, float depth) {
    const float ndcX = (x + 0.5f) / static_cast<float>(view.width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (y + 0.5f) / static_cast<float>(view.height) * 2.0f;
    const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
    return glm::vec3(ndcX * aspect * view.tanHalfFovY * depth, ndcY * view.tanHalfFovY * depth, -depth);
}

ScreenGiResult screenGi(const ScreenGiView& view, int x, int y, const glm::vec3& normal, const ScreenGiSettings& s) {
    ScreenGiResult out;
    const float depth = view.depth(x, y);
    if (depth <= 0.0f || s.slices <= 0 || s.steps <= 0) return out;
    // Lifted a hair off its surface, so the surface itself never occludes.
    const glm::vec3 p = screenGiViewPos(view, static_cast<float>(x), static_cast<float>(y), depth) + normal * (0.002f * depth);
    const glm::vec3 v = glm::normalize(-p);
    const float focal = 0.5f * static_cast<float>(view.height) / view.tanHalfFovY;
    const float radiusPx = s.radius * focal / depth;
    glm::vec3 bounce(0.0f);
    float visible = 0.0f, weight = 0.0f;
    for (int i = 0; i < s.slices; ++i) {
        // Fixed directions, the same for every pixel and frame.
        const float phi = (static_cast<float>(i) + 0.5f) * 3.14159265f / static_cast<float>(s.slices);
        const glm::vec3 dir(std::cos(phi), std::sin(phi), 0.0f);
        const glm::vec3 side = dir - v * glm::dot(dir, v);
        if (glm::length(side) < 1e-5f) continue;
        const glm::vec3 t = glm::normalize(side);
        const glm::vec3 axis = glm::cross(v, t);
        const glm::vec3 np = normal - axis * glm::dot(normal, axis);
        const float len = glm::length(np);
        if (len < 1e-4f) continue;
        const float n = std::atan2(glm::dot(np, t), glm::dot(np, v));
        uint32_t occluded = 0u;
        glm::vec3 light(0.0f);
        for (int sideSign : { 1, -1 }) {
            for (int k = 0; k < s.steps; ++k) {
                const float f = static_cast<float>(k + 1) / static_cast<float>(s.steps);
                const float dist = std::max(static_cast<float>(k + 1), radiusPx * f * f);
                const int sx = static_cast<int>(std::lround(static_cast<float>(x) + static_cast<float>(sideSign) * dir.x * dist));
                const int sy = static_cast<int>(std::lround(static_cast<float>(y) - static_cast<float>(sideSign) * dir.y * dist));
                if (sx < 0 || sy < 0 || sx >= view.width || sy >= view.height) break;
                const float sd = view.depth(sx, sy);
                if (sd <= 0.0f) continue; // sky: occludes nothing
                const glm::vec3 sp = screenGiViewPos(view, static_cast<float>(sx), static_cast<float>(sy), sd);
                const glm::vec3 front = sp - p;
                if (glm::length(front) > s.radius) continue;
                const glm::vec3 back = sp + glm::normalize(sp) * s.thickness - p;
                const float tf = screenGiSector(std::atan2(glm::dot(front, t), glm::dot(front, v)), n);
                float t0, t1;
                if (s.horizonOnly) {
                    // Infinitely thick: everything from the sample to the slice's end on its side.
                    t0 = sideSign > 0 ? tf : 0.0f;
                    t1 = sideSign > 0 ? 1.0f : tf;
                } else {
                    const float tb = screenGiSector(std::atan2(glm::dot(back, t), glm::dot(back, v)), n);
                    t0 = std::min(tf, tb);
                    t1 = std::max(tf, tb);
                }
                const uint32_t bits = screenGiSectors(t0, t1);
                const uint32_t fresh = bits & ~occluded;
                if (fresh != 0u) light += view.light(sx, sy) * (static_cast<float>(std::popcount(fresh)) / 32.0f);
                occluded |= bits;
            }
        }
        // Each slice counts by how much of the normal lies in its plane.
        bounce += light * len;
        visible += (1.0f - static_cast<float>(std::popcount(occluded)) / 32.0f) * len;
        weight += len;
    }
    if (weight <= 0.0f) return out;
    out.bounce = bounce / weight;
    out.visibility = visible / weight;
    return out;
}

} // namespace kke
