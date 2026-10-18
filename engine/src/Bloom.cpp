#include "kke/Bloom.h"

#include <algorithm>
#include <cmath>

namespace kke {

glm::vec3 BloomImage::at(int x, int y) const {
    return light[static_cast<size_t>(std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1))];
}

glm::vec3 BloomImage::sample(float x, float y) const {
    const float fx = x - 0.5f, fy = y - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), tx), glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
}

uint32_t bloomLevels(int width, int height, uint32_t wanted) {
    uint32_t n = 0;
    for (int w = width, h = height; n < wanted && w / 2 >= 2 && h / 2 >= 2; w /= 2, h /= 2) ++n;
    return n;
}

namespace {

float luma(const glm::vec3& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }

} // namespace

BloomImage bloomDownsample(const BloomImage& src, bool karis) {
    BloomImage out;
    out.width = std::max(src.width / 2, 1);
    out.height = std::max(src.height / 2, 1);
    out.light.resize(static_cast<size_t>(out.width * out.height));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            // The 13 taps around the 2x2 block this pixel covers (bilinear: each tap averages 2x2 texels).
            const float cx = 2.0f * static_cast<float>(x) + 1.0f, cy = 2.0f * static_cast<float>(y) + 1.0f;
            auto s = [&](float dx, float dy) { return src.sample(cx + dx, cy + dy); };
            const glm::vec3 a = s(-2, -2), b = s(0, -2), c = s(2, -2), d = s(-1, -1), e = s(1, -1), f = s(-2, 0), g = s(0, 0), h = s(2, 0),
                            i = s(-1, 1), j = s(1, 1), k = s(-2, 2), l = s(0, 2), m = s(2, 2);
            // Five boxes: the inner one (half the weight) and four overlapping corner ones (an eighth each).
            const glm::vec3 boxes[5] = { (d + e + i + j) * 0.25f, (a + b + f + g) * 0.25f, (b + c + g + h) * 0.25f, (f + g + k + l) * 0.25f,
                                         (g + h + l + m) * 0.25f };
            const float weights[5] = { 0.5f, 0.125f, 0.125f, 0.125f, 0.125f };
            glm::vec3 sum(0.0f);
            float total = 0.0f;
            for (int q = 0; q < 5; ++q) {
                const float w = weights[q] * (karis ? 1.0f / (1.0f + luma(boxes[q])) : 1.0f);
                sum += boxes[q] * w;
                total += w;
            }
            out.light[static_cast<size_t>(y * out.width + x)] = sum / total;
        }
    return out;
}

BloomImage bloomUpsample(const BloomImage& fine, const BloomImage& coarse) {
    BloomImage out = fine;
    const float sx = static_cast<float>(coarse.width) / static_cast<float>(fine.width);
    const float sy = static_cast<float>(coarse.height) / static_cast<float>(fine.height);
    for (int y = 0; y < fine.height; ++y)
        for (int x = 0; x < fine.width; ++x) {
            const float cx = (static_cast<float>(x) + 0.5f) * sx, cy = (static_cast<float>(y) + 0.5f) * sy;
            glm::vec3 t(0.0f);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    t += coarse.sample(cx + static_cast<float>(dx), cy + static_cast<float>(dy)) *
                         static_cast<float>((2 - std::abs(dx)) * (2 - std::abs(dy)));
            out.light[static_cast<size_t>(y * fine.width + x)] += t / 16.0f;
        }
    return out;
}

BloomImage bloom(const BloomImage& scene, const BloomSettings& settings) {
    const uint32_t n = bloomLevels(scene.width, scene.height, settings.levels);
    if (n == 0) return scene;
    std::vector<BloomImage> down;
    down.push_back(bloomDownsample(scene, true));
    for (uint32_t i = 1; i < n; ++i) down.push_back(bloomDownsample(down.back(), false));
    BloomImage up = down.back();
    for (int i = static_cast<int>(n) - 2; i >= 0; --i) up = bloomUpsample(down[static_cast<size_t>(i)], up);
    for (glm::vec3& c : up.light) c /= static_cast<float>(n); // every level adds the light once
    return up;
}

} // namespace kke
