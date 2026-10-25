#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kke::ktx2 {

// KTX 2.0 (Khronos' GPU texture container), the subset the engine cooks
// and reads (ENG-F60): one 2D image with its mips, no layers, faces or
// supercompression. kke_texcook writes BC7 ones next to the PNGs and
// kke::Texture prefers them (docs/ASSETS.md "Compressed textures").

// The vkFormat values the engine knows.
inline constexpr uint32_t kRgba8Unorm = 37;  // VK_FORMAT_R8G8B8A8_UNORM
inline constexpr uint32_t kRgba8Srgb = 43;   // VK_FORMAT_R8G8B8A8_SRGB
inline constexpr uint32_t kBc7Unorm = 145;   // VK_FORMAT_BC7_UNORM_BLOCK
inline constexpr uint32_t kBc7Srgb = 146;    // VK_FORMAT_BC7_SRGB_BLOCK

struct Image {
    uint32_t vkFormat = 0;
    uint32_t width = 0, height = 0;
    std::vector<std::vector<uint8_t>> levels; // level 0 (full size) first
};

bool isKtx2(const uint8_t* bytes, size_t size);
// False (with `error`) when the bytes aren't a KTX2 file this subset
// reads, or a level is cut short.
bool read(const uint8_t* bytes, size_t size, Image& out, std::string* error = nullptr);
// A complete file: header, level index, data format descriptor, then the
// levels smallest first, as the format wants.
std::vector<uint8_t> write(const Image& image);

// Bytes in one level of `format`: 16 per 4x4 block for BC7, 4 per texel
// for RGBA8; 0 for a format this subset doesn't know.
size_t levelSize(uint32_t vkFormat, uint32_t width, uint32_t height);
bool isSrgb(uint32_t vkFormat);

} // namespace kke::ktx2
