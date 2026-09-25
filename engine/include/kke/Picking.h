#pragma once

#include <glm/glm.hpp>

namespace kke {

// Mouse picking math: screen point -> world ray -> what it hits. Pure
// functions, unit-tested (tests/test_picking.cpp). Matches the engine's
// camera exactly: glm::lookAt + glm::perspective with Vulkan's Y flip
// (see Application.cpp).
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // normalized
    glm::vec3 at(float t) const { return origin + direction * t; }
};

// `pixel` in framebuffer pixels, top-left origin; `viewport` in pixels.
// `proj` must include the Vulkan Y flip, i.e. exactly what's rendered with.
Ray screenToRay(const glm::vec2& pixel, const glm::vec2& viewport, const glm::mat4& view, const glm::mat4& proj);

// Projection the engine renders with, for a given camera setup.
glm::mat4 engineProjection(float fovDegrees, float aspect, float nearPlane, float farPlane);

// Distance along the ray to the horizontal plane y = planeY, or a
// negative value if it's parallel or behind the origin.
float rayPlaneY(const Ray& ray, float planeY);

// Slab test against an axis-aligned box. Returns the entry distance
// (0 if the origin is inside), or a negative value on a miss.
float rayAabb(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax);

// World-space AABB of a local box after an arbitrary affine transform
// (Arvo's method: exact bounds of the 8 transformed corners, no loop over them).
void transformAabb(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& m, glm::vec3& outMin, glm::vec3& outMax);

// Snap to a grid step (step <= 0 returns the value unchanged).
float snapTo(float value, float step);

} // namespace kke
