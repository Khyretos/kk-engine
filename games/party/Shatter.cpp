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
    return shatterPolygon(pane, desc.impact, desc.pieces, desc.seed);
}

std::vector<Polygon2> shatterPolygon(const Polygon2& outline, const glm::vec2& impact, int pieces, uint32_t seed) {
    if (outline.size() < 3) return {};
    glm::vec2 mn(1e30f), mx(-1e30f);
    for (const glm::vec2& p : outline) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    const glm::vec2 mid = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
    Rng rng(seed);
    std::vector<glm::vec2> seeds;
    const int count = std::max(1, pieces);
    const float reach = std::max(h.x, h.y) * 2.0f;
    for (int i = 0; i < count; ++i) {
        // Around the impact, denser near it: a random direction, a radius
        // skewed towards zero (squared), clamped into the bounds. Seeds
        // outside a non-rectangular outline just own no cell.
        const float a = rng.range(0.0f, 6.2831853f);
        const float r = reach * rng.unit() * rng.unit();
        glm::vec2 p = impact + glm::vec2(std::cos(a), std::sin(a)) * r;
        p = glm::clamp(p, mid - h * 0.98f, mid + h * 0.98f);
        bool near = false;
        for (const glm::vec2& s : seeds) near = near || glm::length(s - p) < 0.02f;
        if (!near) seeds.push_back(p);
    }
    std::vector<Polygon2> cells;
    for (size_t i = 0; i < seeds.size(); ++i) {
        Polygon2 cell = outline;
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

void shardPrism(const Polygon2& cell, const glm::vec2& c, float halfThick, const glm::vec3& top, const glm::vec3& side, std::vector<glm::vec3>& hull,
                std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, float shrink, const glm::vec2& material) {
    const size_t n = cell.size();
    if (n < 3) return;
    const size_t first = hull.size();
    for (int up = 0; up < 2; ++up)
        for (const glm::vec2& q : cell) {
            glm::vec2 d = q - c;
            const float l = glm::length(d);
            if (shrink > 0.0f && l > 1e-5f) d *= std::max(0.2f, (l - shrink) / l);
            hull.push_back({ d.x, up ? halfThick : -halfThick, d.y });
        }
    const glm::vec3* corner = hull.data() + first;
    auto vert = [&](const glm::vec3& pos, const glm::vec3& color, const glm::vec3& nrm) {
        v.push_back({ pos, color, nrm, material });
        return static_cast<uint32_t>(v.size() - 1);
    };
    // Top and bottom fans (the cell is counter-clockwise seen from above).
    for (int up = 0; up < 2; ++up) {
        const glm::vec3 nrm(0.0f, up ? 1.0f : -1.0f, 0.0f);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (size_t i = 0; i < n; ++i) vert(corner[static_cast<size_t>(up) * n + i], up ? top : side, nrm);
        for (uint32_t i = 1; i + 1 < n; ++i) {
            // Seen from its own side: counter-clockwise from above is the top's front.
            if (up) idx.insert(idx.end(), { base, base + i + 1, base + i });
            else idx.insert(idx.end(), { base, base + i, base + i + 1 });
        }
    }
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 a0 = corner[i], a1 = corner[(i + 1) % n], b0 = corner[n + i], b1 = corner[n + (i + 1) % n];
        glm::vec3 nrm = glm::cross(a1 - a0, b0 - a0);
        if (glm::dot(nrm, a0 + a1) < 0.0f) nrm = -nrm; // outward (the centre is the origin)
        nrm = glm::normalize(nrm);
        const uint32_t s = static_cast<uint32_t>(v.size());
        vert(a0, side, nrm);
        vert(a1, side, nrm);
        vert(b1, side, nrm);
        vert(b0, side, nrm);
        if (glm::dot(glm::cross(a1 - a0, b1 - a0), nrm) > 0.0f) idx.insert(idx.end(), { s, s + 1, s + 2, s, s + 2, s + 3 });
        else idx.insert(idx.end(), { s, s + 2, s + 1, s, s + 3, s + 2 });
    }
}

} // namespace party
