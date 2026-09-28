#pragma once

// Breaking a glass pane (the glass bridge): the pane's top face is cut
// into Voronoi cells around seeds that crowd in where it was stepped on,
// like real tempered glass (small bits at the impact, bigger ones out at
// the edges). Each cell becomes a thin prism, a Jolt convex hull that
// falls. Pure geometry, unit-tested in tests/test_party.cpp.

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

} // namespace party
