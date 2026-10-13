#pragma once

// The sky prefiltered for reflections (ENG-G06). kke::SkyResolver builds one per
// sky (SkyEnvironment::reflections); kke::SkyRenderer uploads it as one RGBA16F
// texture with a mip per level, which shaders/pbr_common.glsl samples at the surface's roughness.

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

// Level i is levelWidth(i) x levelWidth(i) / 2 RGB texels, rows from the zenith
// down, for perceptual roughness levelRoughness(i); level 0 is the radiance itself.
class EnvMap {
public:
    static constexpr int kLevels = 6;  // the number of levels
    static constexpr int kWidth = 128; // level 0's width in texels

    // Builds every level from radiance(dir) (dir is a unit vector, y up); `samples` GGX samples per texel of the rough levels.
    static EnvMap prefilter(const std::function<glm::vec3(const glm::vec3&)>& radiance, int width = kWidth, int levels = kLevels, int samples = 256);

    // The perceptual roughness of a level, level / (levels - 1).
    static float levelRoughness(int level, int levels);

    int levelCount() const { return static_cast<int>(m_levels.size()); }
    // Texels across a level (half as many down).
    int levelWidth(int level) const { return m_width >> level; }
    // Returns the vector of radiance samples for a specific level.
    const std::vector<glm::vec3>& level(int level) const { return m_levels[static_cast<size_t>(level)]; }
    // Gets the radiance value at a specific texel (x, y) within a level.
    glm::vec3 texel(int level, int x, int y) const;
    // Samples the environment map towards dir using bilinear interpolation, wrapping around the horizon and clamped at the poles.
    glm::vec3 sample(int level, const glm::vec3& dir) const;
    // Converts every level to a vector of RGBA half floats (alpha 1), with level 0 first.
    std::vector<uint16_t> toHalfRgba() const;

private:
    int m_width = 0;
    std::vector<std::vector<glm::vec3>> m_levels;
};

// Generates a point i of `count` of the Hammersley set in [0, 1)^2: (i / count, i's bits mirrored).
glm::vec2 hammersley(uint32_t i, uint32_t count);

// Samples a GGX half vector around n for the random pair xi, alpha = roughness squared.
glm::vec3 importanceSampleGGX(const glm::vec2& xi, const glm::vec3& n, float alpha);

// The split sum's second factor, Karis's analytic fit: what a surface of this specular colour and perceptual roughness reflects of an environment, seen at NdotV.
glm::vec3 envBRDFApprox(const glm::vec3& specularColor, float roughness, float NdotV);

} // namespace kke
