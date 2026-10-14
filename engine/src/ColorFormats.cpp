#include "kke/ColorFormats.h"

#include <cstring>

namespace kke {

bool formatEncodesSrgb(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& offered, bool allowTenBit) {
    std::vector<VkFormat> wanted;
    if (allowTenBit) wanted = { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32 };
    // BGRA on desktops, RGBA on Android, which offers no BGRA.
    wanted.push_back(VK_FORMAT_B8G8R8A8_SRGB);
    wanted.push_back(VK_FORMAT_R8G8B8A8_SRGB);
    for (VkFormat w : wanted)
        for (const VkSurfaceFormatKHR& f : offered)
            if (f.format == w && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    return offered.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : offered.front();
}

bool capturableFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return true;
    default:
        return false;
    }
}

void captureToRgba8(const uint8_t* src, size_t pixels, VkFormat format, uint8_t* rgba) {
    const bool tenBit = format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    const bool redHigh = format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    auto to8 = [](uint32_t v) { return static_cast<uint8_t>((v * 255u + 511u) / 1023u); };
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* in = src + 4 * i;
        uint8_t* out = rgba + 4 * i;
        if (tenBit) {
            uint32_t p = 0;
            std::memcpy(&p, in, 4);
            const uint32_t lo = p & 1023u, mid = (p >> 10) & 1023u, hi = (p >> 20) & 1023u;
            out[0] = to8(redHigh ? hi : lo);
            out[1] = to8(mid);
            out[2] = to8(redHigh ? lo : hi);
        } else {
            out[0] = redHigh ? in[2] : in[0];
            out[1] = in[1];
            out[2] = redHigh ? in[0] : in[2];
        }
        out[3] = 255;
    }
}

} // namespace kke
