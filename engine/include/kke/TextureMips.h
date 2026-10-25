#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kke {

// CPU mip generation for kke::Texture, kept free of Vulkan so it can be
// unit-tested (docs/OPTIMIZATION.md log #14, docs/RENDERING_PRINCIPLES.md).

// One 2x2 box-filter step for RGBA8 with sRGB colour: colour averaged in
// linear light, alpha averaged as is.
void downsampleRgba8Srgb(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh);

// The same 2x2 box step for data (normal, roughness and metallic maps):
// every channel averaged as stored, rounding to nearest.
void downsampleRgba8Linear(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh);

// True when any texel isn't fully opaque: the texture is a cutout card
// (leaves, fences, hair) for model.frag's alpha test.
bool hasTransparentTexels(const uint8_t* rgba, size_t texels);

// Fraction of texels an alpha test at `cutoff` (0..1) keeps.
float alphaCoverage(const uint8_t* rgba, size_t texels, float cutoff);

// Scales alpha so the alpha test keeps `targetCoverage` of this level's
// texels. Averaging alpha down the mip chain otherwise pulls it towards
// 0.5 and past the cutoff, so foliage thins out and vanishes with
// distance (Castano, "Computing Alpha Mipmaps", 2010). This keeps the
// silhouette solid without any dithering or temporal help.
void scaleAlphaToCoverage(uint8_t* rgba, size_t texels, float targetCoverage, float cutoff);

// The whole mip chain kke::Texture uploads, level 0 first, one after
// another in `chain`: colour (`srgb`) averaged in linear light with the
// cutout fix above, data averaged as stored. Returns the level count.
// kke_texcook builds the same chain before compressing it.
uint32_t buildMipChainRgba8(const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb, std::vector<uint8_t>& chain);

} // namespace kke
