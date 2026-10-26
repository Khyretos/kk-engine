#pragma once

#include "kke/Ktx2.h"

#include <cstdint>
#include <vector>

namespace kke {

// BC7 block compression (ENG-F60), with bc7enc (MIT / public domain,
// external/bc7enc): 8 bits a texel instead of RGBA8's 32, sampled by the
// GPU as it is, colour and alpha alike. Cooked offline by kke_texcook;
// the decoder is the fallback on a GPU without BC formats.

// One RGBA8 level -> its BC7 blocks (ktx2::levelSize bytes). Edge blocks
// of a size that isn't a multiple of 4 repeat the last row and column.
// `perceptual` weighs the error for colour (sRGB) rather than data.
// threads 0 = every core.
std::vector<uint8_t> encodeBc7(const uint8_t* rgba, uint32_t width, uint32_t height, bool perceptual, unsigned threads = 0);
// BC7 blocks of one level -> RGBA8 (width * height * 4 bytes).
void decodeBc7(const uint8_t* blocks, uint32_t width, uint32_t height, uint8_t* rgba);

// An image as kke::Texture would make it (buildMipChainRgba8: the same
// mips and cutout fix), every level BC7: what kke_texcook writes.
ktx2::Image cookBc7(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb, unsigned threads = 0);
// Every level of a BC7 or RGBA8 image as one RGBA8 chain, level 0
// first (the upload kke::Texture makes when the GPU can't sample BC7).
std::vector<uint8_t> toRgba8Chain(const ktx2::Image& image);

} // namespace kke
