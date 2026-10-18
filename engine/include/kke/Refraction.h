#pragma once

#include <glm/glm.hpp>

#include <functional>

namespace kke {

// Refraction through see-through surfaces (ENG-F02 part 4): where on screen
// the light seen through a glass or water surface comes from. The refracted
// ray (Snell) is followed as far as the surface behind it and projected; the
// scene image made before the glass is read there. Unlike a plain grab pass,
// a sample that lands on something nearer than the glass (in front of it)
// is not used: the straight-through pixel is, so nothing in front smears
// into the glass. The CPU copy of shaders/translucent_refract.frag and the
// tests' reference.
struct RefractionSample {
    glm::vec2 uv{0.0f}; // where to read the scene behind (0..1, top left 0)
    bool bent = false;  // false: the straight-through pixel (total reflection, off screen or blocked)
};

// `viewProj` with Vulkan's flipped Y, as the engine draws; `behindDistance(uv)`
// is the distance from the camera to what the scene image shows at uv (0 =
// sky). A ray goes at most `maxDepth` metres past the surface (the body is
// taken as solid that far: jelly, water, a glass block).
RefractionSample refractedSample(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& worldPos, const glm::vec3& normal,
                                 float ior, const std::function<float(const glm::vec2&)>& behindDistance, float maxDepth = 1.0f);

} // namespace kke
