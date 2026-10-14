#pragma once

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kke {

// The swapchain's colour formats (ENG-G17). Tests: tests/test_color_formats.cpp.

// True for the *_SRGB formats, where the GPU encodes the colour written to
// them; a UNORM swapchain needs the shaders to write sRGB-encoded values.
bool formatEncodesSrgb(VkFormat format);

// The surface format the swapchain uses: a 10-bit one (A2B10G10R10 or
// A2R10G10B10, sRGB colour space) when offered and allowed, which leaves
// no visible steps in dark gradients; else 8-bit BGRA or RGBA sRGB; else
// whatever the surface offers first (a 30-bit X display offers only
// A2R10G10B10). KKE_SWAPCHAIN_8BIT=1 sets allowTenBit to false.
VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& offered, bool allowTenBit);

// Whether a screenshot of a swapchain in this format can be turned into
// RGBA8 by captureToRgba8.
bool capturableFormat(VkFormat format);
// A screenshot copied from a swapchain image (4 bytes a pixel, top row
// first) -> RGBA8 with alpha 255. The bytes keep the image's encoding.
void captureToRgba8(const uint8_t* src, size_t pixels, VkFormat format, uint8_t* rgba);

} // namespace kke
