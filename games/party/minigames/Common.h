#pragma once

// Small helpers the minigames share: steering a CPU bean, hex tiles,
// glass, ring platforms.

#include "../Minigame.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace party::common {

constexpr float kPi = 3.14159265f;

// A CPU bean's stick: toward `to` (flat), full tilt at `speed` (0..1).
inline glm::vec2 steer(const glm::vec3& from, const glm::vec3& to, float speed = 1.0f) {
    glm::vec2 d(to.x - from.x, to.z - from.z);
    const float l = glm::length(d);
    if (l < 0.05f) return glm::vec2(0.0f);
    return d / l * speed * std::min(1.0f, l * 2.0f);
}

// How good a CPU bean is: 0.55 (Easy) .. 1 (Expert).
inline float skill(const Bean& b) { return 0.55f + 0.15f * static_cast<float>(b.difficulty); }

// A hex tile's corners (flat top), for its Jolt hull.
inline std::vector<glm::vec3> hexPoints(float radius, float halfHeight) {
    std::vector<glm::vec3> p;
    for (int k = 0; k < 6; ++k) {
        const float a = kPi / 3.0f * static_cast<float>(k);
        for (float y : { -halfHeight, halfHeight }) p.push_back({ std::cos(a) * radius, y, std::sin(a) * radius });
    }
    return p;
}

// Hex grid centres (flat-top hexes, pointy along Z) within `rings` of the middle.
inline std::vector<glm::vec2> hexGrid(int rings, float radius, float gap) {
    std::vector<glm::vec2> out;
    const float w = (radius + gap * 0.5f) * 1.5f, h = (radius + gap * 0.5f) * std::sqrt(3.0f);
    for (int q = -rings; q <= rings; ++q)
        for (int r = std::max(-rings, -q - rings); r <= std::min(rings, -q + rings); ++r)
            out.push_back({ w * static_cast<float>(q), h * (static_cast<float>(r) + static_cast<float>(q) * 0.5f) });
    return out;
}
// Which ring of the grid a centre is on (0 = the middle).
inline int hexRing(const glm::vec2& c, float radius, float gap) {
    const float w = (radius + gap * 0.5f) * 1.5f, h = (radius + gap * 0.5f) * std::sqrt(3.0f);
    const float q = c.x / w, r = c.y / h - q * 0.5f;
    const int qi = static_cast<int>(std::lround(q)), ri = static_cast<int>(std::lround(r));
    return std::max({ std::abs(qi), std::abs(ri), std::abs(-qi - ri) });
}

// Mark what was added since `from` as glass for drawTranslucent (uv.x
// density: how much it tints; uv.y milkiness).
inline void makeGlass(std::vector<kke::Vertex>& v, size_t from, float density, float milky) {
    for (size_t i = from; i < v.size(); ++i) v[i].uv = glm::vec2(density, milky);
}

} // namespace party::common
