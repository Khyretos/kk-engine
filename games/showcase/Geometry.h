#pragma once

#include "kke/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Plain shapes for kke_demo's level and props, vertex-coloured (the
// renderer colours by vertex), appended to one mesh so a whole group is
// one draw.
namespace kke_showcase {

// A box of half extents `half`, placed by `m`. Tops are a little lighter
// than the sides and the bottom, so shape reads without textures.
void appendBox(const glm::mat4& m, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
// A sphere of `radius` around `m`'s origin.
void appendSphere(const glm::mat4& m, float radius, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx);
// A cylinder standing on its axis (local Y), `halfHeight` up and down
// from `m`'s origin; `bands` (0..1) darkens two rings (a barrel's hoops).
void appendCylinder(const glm::mat4& m, float radius, float halfHeight, const glm::vec3& color, std::vector<kke::Vertex>& v,
                    std::vector<uint32_t>& idx, bool bands = false);
// The points of that cylinder's outline (for a convex hull body).
std::vector<glm::vec3> cylinderPoints(float radius, float halfHeight, int sides = 12);

} // namespace kke_showcase
