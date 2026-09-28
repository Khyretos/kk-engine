#include "Shatter.h"

#include "Show.h"

#include <algorithm>
#include <cmath>

namespace party {

Polygon2 clipHalfPlane(const Polygon2& poly, const glm::vec2& n, float d) {
    Polygon2 out;
    const size_t count = poly.size();
    for (size_t i = 0; i < count; ++i) {
        const glm::vec2 a = poly[i], b = poly[(i + 1) % count];
        const float da = glm::dot(a, n) - d, db = glm::dot(b, n) - d;
        if (da <= 0.0f) out.push_back(a);
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) {
            const float t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

float polygonArea(const Polygon2& poly) {
    float a = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        const glm::vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::abs(a) * 0.5f;
}

glm::vec2 polygonCentroid(const Polygon2& poly) {
    glm::vec2 c(0.0f);
    for (const glm::vec2& p : poly) c += p;
    return poly.empty() ? c : c / static_cast<float>(poly.size());
}

std::vector<Polygon2> shatterPane(const ShardDesc& desc) {
    const glm::vec2 h = desc.halfSize;
    // Counter-clockwise seen from above (+y): x right, z towards the viewer.
    const Polygon2 pane = { { -h.x, -h.y }, { -h.x, h.y }, { h.x, h.y }, { h.x, -h.y } };
    Rng rng(desc.seed);
    std::vector<glm::vec2> seeds;
    const int count = std::max(1, desc.pieces);
    const float reach = std::max(h.x, h.y) * 2.0f;
    for (int i = 0; i < count; ++i) {
        // Around the impact, denser near it: a random direction, a radius
        // skewed towards zero (squared), clamped into the pane.
        const float a = rng.range(0.0f, 6.2831853f);
        const float r = reach * rng.unit() * rng.unit();
        glm::vec2 p = desc.impact + glm::vec2(std::cos(a), std::sin(a)) * r;
        p = glm::clamp(p, -h * 0.98f, h * 0.98f);
        bool near = false;
        for (const glm::vec2& s : seeds) near = near || glm::length(s - p) < 0.02f;
        if (!near) seeds.push_back(p);
    }
    std::vector<Polygon2> cells;
    for (size_t i = 0; i < seeds.size(); ++i) {
        Polygon2 cell = pane;
        for (size_t j = 0; j < seeds.size() && !cell.empty(); ++j) {
            if (i == j) continue;
            // Nearer to seed i than to seed j: dot(p, n) <= d on the bisector.
            const glm::vec2 n = seeds[j] - seeds[i];
            const float d = glm::dot(n, (seeds[i] + seeds[j]) * 0.5f);
            cell = clipHalfPlane(cell, n, d);
        }
        if (cell.size() >= 3 && polygonArea(cell) > 1e-4f) cells.push_back(std::move(cell));
    }
    return cells;
}

} // namespace party
