#include "kke/InteriorColor.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace kke {

namespace {

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float toSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// 3 bits per channel: 512 bins, wide enough that a noisy stone or wood
// texture's texels mostly share one bin, narrow enough to tell swatches
// of an atlas apart.
constexpr int kBits = 3;
constexpr int kBins = 1 << (3 * kBits);
int binOf(const uint8_t* p) {
    constexpr int s = 8 - kBits;
    return ((p[0] >> s) << (2 * kBits)) | ((p[1] >> s) << kBits) | (p[2] >> s);
}

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

// Box-filters down to at most `maxSide` px (averaging only opaque texels).
Image shrink(const uint8_t* rgba, int width, int height, int maxSide) {
    Image out;
    const int f = std::max(1, (std::max(width, height) + maxSide - 1) / maxSide);
    out.w = std::max(1, width / f);
    out.h = std::max(1, height / f);
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            uint32_t sum[4] = { 0, 0, 0, 0 }, n = 0, all = 0;
            for (int dy = 0; dy < f; ++dy) {
                for (int dx = 0; dx < f; ++dx) {
                    const int sx = std::min(width - 1, x * f + dx), sy = std::min(height - 1, y * f + dy);
                    const uint8_t* p = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
                    ++all;
                    sum[3] += p[3];
                    if (p[3] < 128) continue;
                    for (int c = 0; c < 3; ++c) sum[c] += p[c];
                    ++n;
                }
            }
            uint8_t* o = out.rgba.data() + (static_cast<size_t>(y) * out.w + x) * 4;
            for (int c = 0; c < 3; ++c) o[c] = n ? static_cast<uint8_t>(sum[c] / n) : 0;
            o[3] = static_cast<uint8_t>(sum[3] / all);
        }
    }
    return out;
}

InteriorFill analyse(const Image& img, const std::vector<glm::vec2>& uvs, const std::vector<float>& weights) {
    InteriorFill fill;
    if (img.w <= 0 || img.h <= 0) return fill;
    auto texel = [&](int x, int y) { return img.rgba.data() + (static_cast<size_t>(y) * img.w + x) * 4; };
    auto texelAt = [&](const glm::vec2& uv, int& x, int& y) {
        const float u = uv.x - std::floor(uv.x), v = uv.y - std::floor(uv.y); // repeat addressing
        x = std::clamp(static_cast<int>(u * img.w), 0, img.w - 1);
        y = std::clamp(static_cast<int>(v * img.h), 0, img.h - 1);
    };

    // 1. Weighted histogram of where the object samples the texture.
    std::vector<double> binWeight(kBins, 0.0);
    std::vector<glm::dvec3> binSum(kBins, glm::dvec3(0.0)); // linear colour x weight
    double total = 0.0;
    auto add = [&](const uint8_t* p, double w) {
        if (p[3] < 128 || !(w > 0.0)) return; // cut-out / transparent texels are not the material
        const int b = binOf(p);
        binWeight[b] += w;
        binSum[b] += glm::dvec3(toLinear(p[0] / 255.0f), toLinear(p[1] / 255.0f), toLinear(p[2] / 255.0f)) * w;
        total += w;
    };
    if (uvs.empty()) {
        for (int y = 0; y < img.h; ++y) for (int x = 0; x < img.w; ++x) add(texel(x, y), 1.0);
    } else {
        for (size_t i = 0; i < uvs.size(); ++i) {
            int x = 0, y = 0;
            texelAt(uvs[i], x, y);
            add(texel(x, y), i < weights.size() ? weights[i] : 1.0);
        }
    }
    if (total <= 0.0) return fill;
    const int dom = static_cast<int>(std::max_element(binWeight.begin(), binWeight.end()) - binWeight.begin());
    const glm::dvec3 meanLin = binSum[dom] / binWeight[dom];
    fill.dominant = glm::vec3(toSrgb(float(meanLin.x)), toSrgb(float(meanLin.y)), toSrgb(float(meanLin.z)));
    fill.coverage = static_cast<float>(binWeight[dom] / total);
    fill.color = deeperTone(fill.dominant);

    // 2. A texel of that colour in the middle of a uniform patch: the
    // most same-bin neighbours in a 7x7 window (summed-area table), then
    // the closest to the mean. Mipmapped sampling from a distance then
    // still reads this colour, not a blend with the next swatch.
    const int w = img.w, h = img.h;
    std::vector<int> sat(static_cast<size_t>(w + 1) * (h + 1), 0);
    auto at = [&](int x, int y) -> int& { return sat[static_cast<size_t>(y) * (w + 1) + x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = texel(x, y);
            at(x + 1, y + 1) = (p[3] >= 128 && binOf(p) == dom) + at(x, y + 1) + at(x + 1, y) - at(x, y);
        }
    constexpr int r = 3;
    int bestScore = -1, bestX = -1, bestY = -1;
    float bestDist = 1e30f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = texel(x, y);
            if (p[3] < 128 || binOf(p) != dom) continue;
            const int x0 = std::max(0, x - r), y0 = std::max(0, y - r), x1 = std::min(w, x + r + 1), y1 = std::min(h, y + r + 1);
            const int score = at(x1, y1) - at(x0, y1) - at(x1, y0) + at(x0, y0);
            const glm::vec3 lin(toLinear(p[0] / 255.0f), toLinear(p[1] / 255.0f), toLinear(p[2] / 255.0f));
            const glm::vec3 d = lin - glm::vec3(meanLin);
            const float dist = glm::dot(d, d);
            if (score > bestScore || (score == bestScore && dist < bestDist)) {
                bestScore = score;
                bestDist = dist;
                bestX = x;
                bestY = y;
            }
        }
    }
    if (bestX < 0) return fill; // unreachable: the dominant bin came from these texels
    fill.uv = glm::vec2((bestX + 0.5f) / w, (bestY + 0.5f) / h);

    // 3. Vertex colour that turns that texel into the deeper tone (the
    // shader multiplies linear texture by linear vertex colour).
    const uint8_t* p = texel(bestX, bestY);
    for (int c = 0; c < 3; ++c) {
        const float texLin = toLinear(p[c] / 255.0f);
        const float want = toLinear(fill.color[c]);
        const float t = texLin > 1e-4f ? std::clamp(want / texLin, 0.0f, 1.0f) : 0.0f;
        fill.tint[c] = toSrgb(t);
    }
    fill.valid = true;
    return fill;
}

} // namespace

glm::vec3 deeperTone(const glm::vec3& srgb) {
    // HSV: darker value, a touch more saturation (a fresh break shows
    // the material's own colour, unfaded).
    const glm::vec3 c = glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f));
    const float mx = std::max({ c.r, c.g, c.b }), mn = std::min({ c.r, c.g, c.b });
    const float v = mx, d = mx - mn;
    if (mx <= 0.0f) return glm::vec3(0.0f);
    float s = d / mx;
    float hue = 0.0f;
    if (d > 1e-6f) {
        if (mx == c.r) hue = std::fmod((c.g - c.b) / d, 6.0f);
        else if (mx == c.g) hue = (c.b - c.r) / d + 2.0f;
        else hue = (c.r - c.g) / d + 4.0f;
        if (hue < 0.0f) hue += 6.0f;
    }
    s = std::min(1.0f, s * 1.15f);
    const float v2 = v * kInteriorDarken;
    const float chroma = v2 * s;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    glm::vec3 rgb;
    switch (static_cast<int>(hue) % 6) {
    case 0: rgb = { chroma, x, 0 }; break;
    case 1: rgb = { x, chroma, 0 }; break;
    case 2: rgb = { 0, chroma, x }; break;
    case 3: rgb = { 0, x, chroma }; break;
    case 4: rgb = { x, 0, chroma }; break;
    default: rgb = { chroma, 0, x }; break;
    }
    return rgb + glm::vec3(v2 - chroma);
}

InteriorFill interiorFillFromPixels(const uint8_t* rgba, int width, int height, const std::vector<glm::vec2>& uvs,
                                    const std::vector<float>& weights) {
    if (!rgba || width <= 0 || height <= 0) return {};
    return analyse(shrink(rgba, width, height, 256), uvs, weights);
}

InteriorFill interiorFillFromTexture(const std::string& path, const std::vector<glm::vec2>& uvs, const std::vector<float>& weights) {
    static std::mutex mutex;
    static std::map<std::string, Image> cache; // shrunk: <= 256 KB each
    const Image* img = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(path);
        if (it == cache.end()) {
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = path.empty() ? nullptr : stbi_load(path.c_str(), &w, &h, &channels, 4);
            Image shrunk;
            if (pixels) {
                shrunk = shrink(pixels, w, h, 256);
                stbi_image_free(pixels);
            }
            it = cache.emplace(path, std::move(shrunk)).first; // failures cached too: no retry per break
        }
        img = &it->second; // std::map nodes never move
    }
    if (img->w == 0) return {};
    return analyse(*img, uvs, weights);
}

} // namespace kke
