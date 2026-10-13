#include "kke/EnvMap.h"
#include "kke/Sky.h"

#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>

namespace kke {

namespace {
constexpr float kPi = 3.14159265358979f;
} // namespace

// Point i of the Hammersley set: evenly spread, the same every run.
glm::vec2 hammersley(uint32_t i, uint32_t count) {
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return glm::vec2(static_cast<float>(i) / static_cast<float>(count), static_cast<float>(bits) * 2.3283064365386963e-10f);
}

// A GGX half vector around n (Karis 2013, ImportanceSampleGGX).
glm::vec3 importanceSampleGGX(const glm::vec2& xi, const glm::vec3& n, float alpha) {
    const float a2 = alpha * alpha;
    const float phi = 2.0f * kPi * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const glm::vec3 h(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tx = glm::normalize(glm::cross(up, n));
    const glm::vec3 ty = glm::cross(n, tx);
    return glm::normalize(tx * h.x + ty * h.y + n * h.z);
}

// Karis's EnvBRDFApprox (2014): the environment BRDF without a lookup texture.
glm::vec3 envBRDFApprox(const glm::vec3& specularColor, float roughness, float NdotV) {
    const glm::vec4 c0(-1.0f, -0.0275f, -0.572f, 0.022f);
    const glm::vec4 c1(1.0f, 0.0425f, 1.04f, -0.04f);
    const glm::vec4 r = roughness * c0 + c1;
    const float a004 = std::min(r.x * r.x, std::exp2(-9.28f * NdotV)) * r.x + r.y;
    const glm::vec2 ab = glm::vec2(-1.04f, 1.04f) * a004 + glm::vec2(r.z, r.w);
    return specularColor * ab.x + ab.y;
}

// The perceptual roughness of a level, level / (levels - 1).
float EnvMap::levelRoughness(int level, int levels) {
    return levels > 1 ? static_cast<float>(level) / static_cast<float>(levels - 1) : 0.0f;
}

// Gets the radiance value at a specific texel (x, y) within a level.
glm::vec3 EnvMap::texel(int level, int x, int y) const {
    const int w = levelWidth(level);
    return m_levels[static_cast<size_t>(level)][static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
}

// Samples the environment map towards dir using bilinear interpolation, wrapping around the horizon and clamped at the poles.
glm::vec3 EnvMap::sample(int level, const glm::vec3& dir) const {
    const int w = levelWidth(level), h = w / 2;
    const glm::vec2 uv = equirectUv(glm::normalize(dir));
    const float fx = uv.x * static_cast<float>(w) - 0.5f, fy = uv.y * static_cast<float>(h) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const int xa = ((x0 % w) + w) % w, xb = (xa + 1) % w;
    const int ya = std::clamp(y0, 0, h - 1), yb = std::clamp(y0 + 1, 0, h - 1);
    return glm::mix(glm::mix(texel(level, xa, ya), texel(level, xb, ya), tx), glm::mix(texel(level, xa, yb), texel(level, xb, yb), tx), ty);
}

// Converts every level to a vector of RGBA half floats (alpha 1), with level 0 first.
std::vector<uint16_t> EnvMap::toHalfRgba() const {
    std::vector<uint16_t> out;
    for (const auto& lv : m_levels) {
        for (const auto& c : lv) {
            out.push_back(glm::packHalf1x16(c.r));
            out.push_back(glm::packHalf1x16(c.g));
            out.push_back(glm::packHalf1x16(c.b));
            out.push_back(glm::packHalf1x16(1.0f));
        }
    }
    return out;
}

// Prefilters the environment map (ENG-G06). Level 0 is the radiance at each texel's centre;
// every rougher level convolves level 0 with GGX around the texel's direction, N = V = R.
EnvMap EnvMap::prefilter(const std::function<glm::vec3(const glm::vec3&)>& radiance, int width, int levels, int samples) {
    EnvMap m;
    m.m_width = width;
    m.m_levels.resize(static_cast<size_t>(levels));

    auto dirOf = [](int x, int y, int w) {
        return equirectDir({ (static_cast<float>(x) + 0.5f) / static_cast<float>(w), (static_cast<float>(y) + 0.5f) / static_cast<float>(w / 2) });
    };

    for (int i = 0; i < levels; ++i) {
        const int w = width >> i, h = w / 2;
        std::vector<glm::vec3>& out = m.m_levels[static_cast<size_t>(i)];
        out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        const float r = levelRoughness(i, levels);
        const float alpha = r * r;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const glm::vec3 n = dirOf(x, y, w);
                glm::vec3& t = out[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];

                if (i == 0) {
                    t = radiance(n);
                    continue;
                }

                glm::vec3 sum(0.0f);
                float weight = 0.0f;

                for (int s = 0; s < samples; ++s) {
                    const glm::vec3 hv = importanceSampleGGX(hammersley(static_cast<uint32_t>(s), static_cast<uint32_t>(samples)), n, alpha);
                    const glm::vec3 l = 2.0f * glm::dot(n, hv) * hv - n;
                    const float nl = glm::dot(n, l);

                    if (nl > 0.0f) {
                        sum += m.sample(0, l) * nl;
                        weight += nl;
                    }
                }

                t = weight > 0.0f ? sum / weight : m.texel(0, x, y);
            }
        }
    }

    return m;
}

} // namespace kke
