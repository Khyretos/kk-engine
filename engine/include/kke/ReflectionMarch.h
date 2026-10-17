#pragma once

#include <glm/glm.hpp>

#include <functional>

namespace kke {

// The screen-space half of reflections (ENG-F02): from a surface point, its
// reflected ray marched along its line on screen in equal steps, depth
// interpolated perspective-correctly, then four halvings to the crossing.
// The CPU copy of shaders/reflect.glsl and the tests' reference.
struct ReflectionMarchView {
    int width = 0, height = 0;
    glm::mat4 proj{1.0f}; // view space -> clip (Vulkan's flipped Y)
    std::function<float(int x, int y)> viewZ; // view-space z of what pixel (x, y) shows (< 0); 0 = sky
};

struct ReflectionMarchResult {
    bool hit = false;
    bool leftScreen = false; // the ray left the screen before meeting anything (KKE traces those)
    glm::vec2 pixel{0.0f};   // where it met something (full-resolution pixel)
};

// `origin` and `direction` in view space; `steps` samples over `maxDistance`
// metres; a surface is `thickness` metres deep.
ReflectionMarchResult reflectionMarch(const ReflectionMarchView& view, const glm::vec3& origin, const glm::vec3& direction, int steps,
                                      float maxDistance, float thickness);

} // namespace kke
