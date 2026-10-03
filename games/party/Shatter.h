#pragma once

// Breaking a slab (the glass bridge's panes, Hex-a-Gone's tiles): its top
// face is cut into Voronoi cells around seeds that crowd in where it was
// stepped on, like real tempered glass or cracked stone (small bits at the
// impact, bigger ones out at the edges). Each cell becomes a prism, a Jolt
// convex hull that falls. Pure geometry, unit-tested in tests/test_party.cpp.

#include "kke/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace party {

// A convex polygon in the pane's plane (x, z), counter-clockwise seen
// from above.
using Polygon2 = std::vector<glm::vec2>;

// Keeps the part of `poly` where dot(p, n) <= d (Sutherland-Hodgman
// against one half-plane).
Polygon2 clipHalfPlane(const Polygon2& poly, const glm::vec2& n, float d);
float polygonArea(const Polygon2& poly);
glm::vec2 polygonCentroid(const Polygon2& poly);

struct ShardDesc {
    glm::vec2 halfSize{0.6f, 0.6f}; // the pane, around its centre (x, z)
    glm::vec2 impact{0.0f};         // where it was stepped on (pane space)
    int pieces = 14;
    uint32_t seed = 1;
};

// The cells: every one inside the pane, together covering it exactly
// (no gaps, no overlaps), each convex. Slivers under 1 cm² are dropped.
std::vector<Polygon2> shatterPane(const ShardDesc& desc);

// The same for any convex outline (counter-clockwise from above), seeds
// crowding round `impact` (outline space).
std::vector<Polygon2> shatterPolygon(const Polygon2& outline, const glm::vec2& impact, int pieces, uint32_t seed);

// A shard as a prism `halfThick` up and down from the cell, centred on `c`
// (its own middle, so the body spins about it): the hull's points and a
// flat-shaded mesh, `top` on the top face, `side` on the rest. `shrink`
// metres pulls every edge in towards `c` (a crack you can see between
// pieces still in place). `material` goes in every vertex's uv (glow for
// opaque parts, density/milkiness for translucent glass).
void shardPrism(const Polygon2& cell, const glm::vec2& c, float halfThick, const glm::vec3& top, const glm::vec3& side, std::vector<glm::vec3>& hull,
                std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, float shrink = 0.0f, const glm::vec2& material = glm::vec2(0.0f));

} // namespace party
