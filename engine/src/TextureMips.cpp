#include "kke/TextureMips.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// Two small lookup tables keep the linear-light average fast: 256 floats
// for decode, 4096 bytes for encode.
struct SrgbTables {
    float toLinear[256];
    uint8_t toSrgb[4096];
    SrgbTables() {
        for (int i = 0; i < 256; ++i) {
            float c = i / 255.0f;
            toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i < 4096; ++i) {
            float l = i / 4095.0f;
            float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            toSrgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
};

size_t countKept(const uint8_t* rgba, size_t texels, float cutoff, float scale) {
    size_t kept = 0;
    for (size_t i = 0; i < texels; ++i) {
        if (rgba[i * 4 + 3] / 255.0f * scale >= cutoff) ++kept;
    }
    return kept;
}

} // namespace

// Averaging sRGB bytes directly darkens every mip (a black/white checker
// would fade to 0.5 sRGB = 0.21 linear instead of 0.5).
void downsampleRgba8Srgb(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    static const SrgbTables t;
    for (uint32_t y = 0; y < dh; ++y) {
        uint32_t y0 = std::min(y * 2, sh - 1), y1 = std::min(y * 2 + 1, sh - 1);
        for (uint32_t x = 0; x < dw; ++x) {
            uint32_t x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
            const uint8_t* p[4] = { src + (y0 * sw + x0) * 4, src + (y0 * sw + x1) * 4, src + (y1 * sw + x0) * 4, src + (y1 * sw + x1) * 4 };
            uint8_t* o = dst + (y * dw + x) * 4;
            for (int c = 0; c < 3; ++c) {
                float l = (t.toLinear[p[0][c]] + t.toLinear[p[1][c]] + t.toLinear[p[2][c]] + t.toLinear[p[3][c]]) * 0.25f;
                o[c] = t.toSrgb[static_cast<int>(l * 4095.0f + 0.5f)];
            }
            o[3] = static_cast<uint8_t>((p[0][3] + p[1][3] + p[2][3] + p[3][3] + 2) / 4);
        }
    }
}

bool hasTransparentTexels(const uint8_t* rgba, size_t texels) {
    for (size_t i = 0; i < texels; ++i) {
        if (rgba[i * 4 + 3] != 255) return true;
    }
    return false;
}

float alphaCoverage(const uint8_t* rgba, size_t texels, float cutoff) {
    if (texels == 0) return 0.0f;
    return static_cast<float>(countKept(rgba, texels, cutoff, 1.0f)) / static_cast<float>(texels);
}

void scaleAlphaToCoverage(uint8_t* rgba, size_t texels, float targetCoverage, float cutoff) {
    if (texels == 0) return;
    const size_t target = static_cast<size_t>(std::lround(std::clamp(targetCoverage, 0.0f, 1.0f) * static_cast<float>(texels)));
    // Coverage only grows with the scale, so bisect for the smallest
    // scale that keeps at least the target count.
    float lo = 0.0f, hi = 64.0f;
    if (countKept(rgba, texels, cutoff, hi) < target) {
        lo = hi; // can't reach it (too few non-zero texels): scale as far as allowed
    } else {
        for (int i = 0; i < 20; ++i) {
            float mid = 0.5f * (lo + hi);
            if (countKept(rgba, texels, cutoff, mid) >= target) hi = mid;
            else lo = mid;
        }
    }
    const float scale = hi;
    for (size_t i = 0; i < texels; ++i) {
        uint8_t& a = rgba[i * 4 + 3];
        a = static_cast<uint8_t>(std::clamp(std::lround(a * scale), 0l, 255l));
    }
}

} // namespace kke
