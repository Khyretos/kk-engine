#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace kke {

// Temporal anti-aliasing, an option over the clean image (ENG-F05 part 2,
// Kees: SMAA by default, TAA beside it; docs/RENDERING_PRINCIPLES.md §1:
// nothing may need it, it only adds on top). Each frame the projection is
// shifted by under a pixel (a Halton sequence), and the new frame is mixed
// into the history of earlier ones, reprojected through the depth with
// last frame's camera. Where something moved, the history is clipped to
// the colours around the pixel this frame (variance clipping), so it
// can't trail far behind: a softer edge in motion, not a ghost.
struct TaaSettings {
    float blend = 0.1f;       // the new frame's share of each pixel
    float clipSpread = 1.0f;  // how many standard deviations around this frame's colours the history may sit
    uint32_t jitterCycle = 8; // frames before the sub-pixel offsets repeat
};

// The radical inverse of `index` in `base` (the Halton sequence, 0..1).
float taaHalton(uint32_t index, uint32_t base);
// Frame `frame`'s sub-pixel offset, in pixels (-0.5..0.5 each way).
glm::vec2 taaJitter(uint32_t frame, const TaaSettings& settings);
// `proj` shifted by `jitterPixels` on a `size`-pixel image.
glm::mat4 taaJitteredProjection(const glm::mat4& proj, const glm::vec2& jitterPixels, const glm::vec2& size);
// Where pixel `uv` (0..1) at depth `depth` (0..1) was last frame, in uv.
glm::vec2 taaReproject(const glm::vec2& uv, float depth, const glm::mat4& invViewProj, const glm::mat4& prevViewProj);
// The history colour pulled inside the box of mean +- spread * deviation
// of this frame's colours around the pixel (in YCoCg, along the line to the mean).
glm::vec3 taaClip(const glm::vec3& history, const glm::vec3& mean, const glm::vec3& deviation, float spread);
glm::vec3 taaToYCoCg(const glm::vec3& rgb);
glm::vec3 taaFromYCoCg(const glm::vec3& ycocg);

} // namespace kke
