#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace kke {

// Opaque geometry drawn nearest first lets the depth test reject hidden
// pixels before they're shaded (early-Z), instead of shading a far wall
// and then painting over it. docs/RENDERING_PRINCIPLES.md §4, issue #37.
//
// Returns the indices of `positions` ordered nearest to `eye` first.
// Stable: equal distances keep their input order, so the draw order (and
// with it the image where depths tie) doesn't flicker frame to frame.
std::vector<uint32_t> frontToBackOrder(const std::vector<glm::vec3>& positions, const glm::vec3& eye);

} // namespace kke
