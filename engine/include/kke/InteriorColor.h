#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// The colour of an object's broken insides (RayFire's "inner material").
//
// Crack faces used to sample the prop's texture at the UV of the nearest
// surface vertex. On a Synty atlas that lands on whatever swatch the
// nearest corner used (trim, a rivet, a painted stripe), so a stone
// pillar broke into faces of random colours, and a face that sampled a
// dark or transparent texel read as a hole. Now every crack face gets
// one colour: the texture's dominant colour, as the object actually
// uses it, a little deeper (darker, slightly more saturated), the way a
// broken rock or plank is darker inside than its weathered surface.
//
// No shader change: crack faces sample the texture at `uv`, a texel in
// the middle of a uniform patch of the dominant colour (so mipmaps don't
// bleed neighbouring swatches in), and their vertex colour is `tint`,
// which turns that texel into `color` exactly.
struct InteriorFill {
    bool valid = false;
    glm::vec3 dominant{1.0f};  // sRGB 0..1, the texture's most common colour
    glm::vec3 color{1.0f};     // sRGB 0..1, the deeper tone drawn inside
    glm::vec2 uv{0.0f};        // texel to sample (centre of a uniform patch)
    glm::vec3 tint{1.0f};      // sRGB vertex colour: tint x texel = color
    float coverage = 0.0f;     // share of the samples that fell in the dominant bin
};

// How much deeper than the surface: value x kInteriorDarken.
inline constexpr float kInteriorDarken = 0.72f;

// The deeper tone of an sRGB colour (darker, a touch more saturated).
glm::vec3 deeperTone(const glm::vec3& srgb);

// From RGBA8 pixels (row 0 = v 0, Vulkan/stb convention). `uvs`: where
// the object samples the texture (e.g. its triangles' UV centroids),
// `weights`: how much each counts (e.g. triangle area); empty weights =
// equal. No uvs = the whole texture counts.
InteriorFill interiorFillFromPixels(const uint8_t* rgba, int width, int height, const std::vector<glm::vec2>& uvs,
                                    const std::vector<float>& weights = {});

// Same, from an image file. Decoded images are cached (shrunk to at
// most 256 px) so breaking a second prop with the same atlas costs only
// the sampling. Invalid result if the file can't be read.
InteriorFill interiorFillFromTexture(const std::string& path, const std::vector<glm::vec2>& uvs, const std::vector<float>& weights = {});

} // namespace kke
