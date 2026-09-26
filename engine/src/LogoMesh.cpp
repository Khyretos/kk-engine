#include "kke/LogoMesh.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke::logo {

namespace {

struct GeneratedPiece {
    const char* name;
    glm::vec3 color;
    std::vector<std::vector<glm::vec2>> parts;
};
#include "LogoOutlines.inc"

float cross2(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

glm::vec2 outwardNormal(const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    return len > 0.0f ? glm::vec2(d.y, -d.x) / len : glm::vec2(0.0f);
}

bool pointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    // Strictly inside or on an edge, for a counter-clockwise triangle.
    return cross2(b - a, p - a) >= 0.0f && cross2(c - b, p - b) >= 0.0f && cross2(a - c, p - c) >= 0.0f;
}

Outline circle(glm::vec2 center, float r, int segments) {
    Outline o;
    for (int i = 0; i < segments; ++i) {
        float a = glm::two_pi<float>() * float(i) / float(segments);
        o.points.push_back(center + r * glm::vec2(std::cos(a), std::sin(a)));
    }
    return o;
}

glm::vec2 centroid(const std::vector<Outline>& outlines) {
    glm::vec2 sum(0.0f);
    size_t n = 0;
    for (const auto& o : outlines)
        for (const auto& p : o.points) { sum += p; ++n; }
    return n ? sum / float(n) : sum;
}

void pushTri(std::vector<uint32_t>& idx, uint32_t a, uint32_t b, uint32_t c) {
    idx.push_back(a);
    idx.push_back(b);
    idx.push_back(c);
}

uint32_t pushVertex(std::vector<Vertex>& v, glm::vec3 p, glm::vec3 n, const glm::vec3& color) {
    v.push_back(Vertex{ p, color, glm::normalize(n), glm::vec2(p.x, p.y) });
    return static_cast<uint32_t>(v.size() - 1);
}

} // namespace

float signedArea(const std::vector<glm::vec2>& points) {
    float a = 0.0f;
    for (size_t i = 0, n = points.size(); i < n; ++i) a += cross2(points[i], points[(i + 1) % n]);
    return 0.5f * a;
}

std::vector<uint32_t> triangulate(const std::vector<glm::vec2>& points) {
    std::vector<uint32_t> out;
    std::vector<uint32_t> ring(points.size());
    for (size_t i = 0; i < ring.size(); ++i) ring[i] = static_cast<uint32_t>(i);
    size_t guard = 0;
    while (ring.size() > 3 && guard < points.size() * points.size() + 16) {
        ++guard;
        bool clipped = false;
        const size_t n = ring.size();
        for (size_t i = 0; i < n; ++i) {
            uint32_t ia = ring[(i + n - 1) % n], ib = ring[i], ic = ring[(i + 1) % n];
            const glm::vec2 &a = points[ia], &b = points[ib], &c = points[ic];
            if (cross2(b - a, c - b) <= 1e-9f) continue; // reflex or degenerate: not an ear
            bool blocked = false;
            for (uint32_t j : ring) {
                if (j == ia || j == ib || j == ic) continue;
                const glm::vec2& p = points[j];
                if (p == a || p == b || p == c) continue;
                if (pointInTriangle(p, a, b, c)) { blocked = true; break; }
            }
            if (blocked) continue;
            pushTri(out, ia, ib, ic);
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // Only collinear runs left (or a non-simple input): drop the
            // flattest vertex rather than loop forever.
            size_t best = 0;
            float bestArea = INFINITY;
            for (size_t i = 0; i < n; ++i) {
                float area = std::abs(cross2(points[ring[i]] - points[ring[(i + n - 1) % n]],
                                             points[ring[(i + 1) % n]] - points[ring[i]]));
                if (area < bestArea) { bestArea = area; best = i; }
            }
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(best));
        }
    }
    if (ring.size() == 3) pushTri(out, ring[0], ring[1], ring[2]);
    return out;
}

void extrude(const Outline& outline, float backZ, float frontZ, float bevel, const glm::vec3& color,
             std::vector<Vertex>& v, std::vector<uint32_t>& idx, float roof) {
    const auto& pts = outline.points;
    const size_t n = pts.size();
    if (n < 3) return;

    // Roof: the front rises linearly towards x = 0, reaching frontZ + roof
    // there (one planar facet per outline, so one normal).
    float halfWidth = 0.0f, side = 0.0f;
    for (const auto& p : pts) {
        halfWidth = std::max(halfWidth, std::abs(p.x));
        side += p.x;
    }
    side = side < 0.0f ? -1.0f : 1.0f;
    if (roof > 0.0f) bevel = 0.0f;
    auto rise = [&](const glm::vec2& p) { return roof > 0.0f ? roof * (1.0f - std::abs(p.x) / halfWidth) : 0.0f; };
    const glm::vec3 frontNormal = roof > 0.0f ? glm::normalize(glm::vec3(side * roof / halfWidth, 0.0f, 1.0f)) : glm::vec3(0, 0, 1);

    std::vector<glm::vec2> edgeN(n); // outward normal of edge i -> i+1
    for (size_t i = 0; i < n; ++i) edgeN[i] = outwardNormal(pts[i], pts[(i + 1) % n]);
    const float smoothCos = std::cos(glm::radians(35.0f));
    std::vector<bool> smooth(n);
    std::vector<glm::vec2> inset(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2 a = edgeN[(i + n - 1) % n], b = edgeN[i];
        const float d = glm::dot(a, b);
        smooth[i] = d > smoothCos;
        // Mitre offset, capped so a needle-sharp corner can't shoot out.
        glm::vec2 miter = (a + b) / std::max(1.0f + d, 0.25f);
        inset[i] = pts[i] - miter * bevel;
    }
    auto wallNormal = [&](size_t edge, size_t vertex) {
        if (!smooth[vertex]) return edgeN[edge];
        return glm::normalize(edgeN[(vertex + n - 1) % n] + edgeN[vertex]);
    };

    const float chamferZ = frontZ - bevel;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const glm::vec2 na = wallNormal(i, i), nb = wallNormal(i, j);
        // Side wall.
        uint32_t a0 = pushVertex(v, { pts[i], backZ }, { na, 0 }, color), b0 = pushVertex(v, { pts[j], backZ }, { nb, 0 }, color),
                 b1 = pushVertex(v, { pts[j], chamferZ + rise(pts[j]) }, { nb, 0 }, color),
                 a1 = pushVertex(v, { pts[i], chamferZ + rise(pts[i]) }, { na, 0 }, color);
        pushTri(idx, a0, b0, b1);
        pushTri(idx, a0, b1, a1);
        if (bevel > 0.0f) { // 45 degree chamfer up to the inset front face
            uint32_t c0 = pushVertex(v, { pts[i], chamferZ }, { na, 1 }, color), d0 = pushVertex(v, { pts[j], chamferZ }, { nb, 1 }, color),
                     d1 = pushVertex(v, { inset[j], frontZ }, { nb, 1 }, color), c1 = pushVertex(v, { inset[i], frontZ }, { na, 1 }, color);
            pushTri(idx, c0, d0, d1);
            pushTri(idx, c0, d1, c1);
        }
    }

    // Front (inset) and back caps.
    const std::vector<glm::vec2>& front = bevel > 0.0f ? inset : pts;
    std::vector<uint32_t> tris = triangulate(front);
    const uint32_t frontBase = static_cast<uint32_t>(v.size());
    for (const auto& p : front) pushVertex(v, { p, frontZ + rise(p) }, frontNormal, color);
    for (uint32_t t : tris) idx.push_back(frontBase + t);
    std::vector<uint32_t> backTris = bevel > 0.0f ? triangulate(pts) : tris;
    const uint32_t backBase = static_cast<uint32_t>(v.size());
    for (const auto& p : pts) pushVertex(v, { p, backZ }, { 0, 0, -1 }, color);
    for (size_t t = 0; t + 2 < backTris.size(); t += 3)
        pushTri(idx, backBase + backTris[t], backBase + backTris[t + 2], backBase + backTris[t + 1]);
}

std::vector<Piece> buildPieces() {
    // Depth layout (front view, +Z towards the viewer): the ring is the
    // thickest slab, the K chevrons sit just behind its face, the needle's
    // faceted roof stands proud of everything, the hub caps it.
    struct Layer { const char* prefix; float backZ, frontZ, bevel, roof; };
    static const Layer kLayers[] = {
        { "ring_", -0.08f, 0.08f, 0.022f, 0.0f },
        { "crown_loop", -0.045f, 0.045f, 0.0f, 0.0f }, // too thin to bevel cleanly
        { "crown_stem", -0.06f, 0.06f, 0.015f, 0.0f },
        { "k_", -0.06f, 0.07f, 0.018f, 0.0f },
        { "needle_", -0.05f, 0.08f, 0.0f, 0.05f },
    };
    std::vector<Piece> pieces;
    for (const GeneratedPiece& g : kGeneratedPieces) {
        const Layer* layer = &kLayers[0];
        for (const Layer& l : kLayers)
            if (std::string(g.name).rfind(l.prefix, 0) == 0) layer = &l;
        Piece p;
        p.name = g.name;
        p.color = g.color;
        p.backZ = layer->backZ;
        p.frontZ = layer->frontZ + layer->roof;
        for (const auto& part : g.parts) p.outlines.push_back(Outline{ part });
        p.center = centroid(p.outlines);
        for (const auto& o : p.outlines) extrude(o, layer->backZ, layer->frontZ, layer->bevel, g.color, p.vertices, p.indices, layer->roof);
        pieces.push_back(std::move(p));
    }
    // The hub: a pin standing out of the needle's centre.
    Piece hub;
    hub.name = "hub";
    hub.color = kPurple;
    hub.backZ = -0.04f;
    hub.frontZ = 0.17f;
    hub.outlines = { circle({}, kHubRadius, 40) };
    extrude(hub.outlines[0], hub.backZ, hub.frontZ, 0.02f, kPurple, hub.vertices, hub.indices);
    pieces.push_back(std::move(hub));
    return pieces;
}

} // namespace kke::logo
