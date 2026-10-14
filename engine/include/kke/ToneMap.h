#pragma once

#include <glm/glm.hpp>

namespace kke {

struct ColorGrade;

// How scene light (unbounded) becomes displayable colour. Since ENG-G17
// the scene draws into an HDR image and one pass at the start of the UI
// pass applies this to every pixel once (shaders/tonemap_post.frag). The
// values are what that shader reads, so keep the order. AgX is the
// default: highlights desaturate towards white instead of shifting hue.
// ACES gives more contrast (the "movie" look); Reinhard is the engine's
// original curve. None passes finished display colours through (the logo
// intro). See docs/RENDERING_PRINCIPLES.md §7.
enum class ToneMapper : int { AgX = 0, ACES = 1, Reinhard = 2, None = 3 };

// The tone-map pass's push constants (shaders/tonemap_post.frag), 64 bytes.
struct PostParams {
    glm::vec4 tone{0.0f, 1.0f, 0.0f, 0.0f}; // x = ToneMapper, y = exposure
    glm::vec4 lookSlope{1.0f};              // rgb = slope, a = saturation
    glm::vec4 lookOffset{0.0f};             // rgb = offset, a = 1 when a look is set
    glm::vec4 lookPower{1.0f};              // rgb = power
};
static_assert(sizeof(PostParams) == 64, "the tone-map pass's push constants");

// Packs a curve, an exposure and a look the way tonemap_post.frag reads them.
PostParams makePostParams(ToneMapper op, float exposure, const ColorGrade& grade);

// CPU mirror of shaders/tonemap.glsl's toneMapLook(): scene light ->
// display-linear 0..1 (before the sRGB encode). Used for clear colours and
// thumbnails, and as the tests' reference for the shader.
glm::vec3 toneMapCpu(const glm::vec3& sceneLight, const PostParams& p);

// The scene light toneMapCpu() turns into displayLinear, or the nearest the
// curve reaches (AgX never quite gets to 1.0).
glm::vec3 inverseToneMapCpu(const glm::vec3& displayLinear, const PostParams& p);

// The value to clear the HDR scene image with so the background shows as
// the sRGB colour a game asked for (Renderer::setClearColor).
glm::vec3 sceneClearColor(const glm::vec3& srgb, const PostParams& p);

} // namespace kke
