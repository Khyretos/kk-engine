#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

// Bloom (ENG-F10): bright light bleeding into what is around it, as a lens
// does. The method of Call of Duty: Advanced Warfare (Jimenez 2014), with
// no brightness threshold: the scene light is halved again and again (a
// 13-tap filter each time; the first one averages its four boxes weighted by
// 1 / (1 + brightness), so one hot pixel can't flicker into a big blob),
// then built back up, each level adding a 3x3 tent of the one below.
// Everything glows a little, the brightest the most, as through real
// glass; `strength` mixes it in before the tone map. Spatial only: nothing
// carried between frames (docs/RENDERING_PRINCIPLES.md §1).
struct BloomSettings {
    float strength = 0.04f; // the bloom's share of the final light
    uint32_t levels = 6;    // halvings, from half resolution down
};

struct BloomImage {
    int width = 0, height = 0;
    std::vector<glm::vec3> light;
    glm::vec3 at(int x, int y) const;                   // clamped to the edge
    glm::vec3 sample(float x, float y) const;           // bilinear, in pixels (pixel centres at +0.5)
};

// The levels that fit: halvings until a side would drop below 2 pixels, at most `wanted`.
uint32_t bloomLevels(int width, int height, uint32_t wanted);
// One halving with the 13-tap filter; `karis`: weight its boxes by 1 / (1 + luma).
BloomImage bloomDownsample(const BloomImage& src, bool karis);
// `fine` plus a 3x3 tent of `coarse` (half its size) read at fine's pixels.
BloomImage bloomUpsample(const BloomImage& fine, const BloomImage& coarse);
// The whole chain: the bloom image at half resolution, normalised so a flat
// image blooms to itself. The CPU copy of shaders/bloom_*.comp.
BloomImage bloom(const BloomImage& scene, const BloomSettings& settings);

} // namespace kke
