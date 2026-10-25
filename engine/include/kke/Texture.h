#pragma once

#include "kke/rhi/Device.h"

#include <volk.h>
#include <string>
#include <cstdint>
#include <memory>

namespace kke {

class VulkanDevice;

// A real, general-purpose 2D texture for the 3D rendering pipeline —
// the first real consumer of stb_image outside RmlUi's own
// RmlVulkanRenderInterface (which has its own, separate texture
// creation code; deliberately not shared with this class, to avoid
// risking a working, already-verified RmlUi code path while building
// something new — see this class's own git history/README entry for
// the reasoning). Owns everything needed to bind and sample it:
// image, view, and a real sampler (LINEAR filtering, REPEAT wrap —
// standard defaults for a material texture, as opposed to
// RmlVulkanRenderInterface's own CLAMP_TO_EDGE-appropriate UI
// textures).
//
// Two ways to create one: from a real image file on disk (decoded via
// stb_image, forcing RGBA8 to match the fixed VK_FORMAT_R8G8B8A8_UNORM
// this always uses), or from raw in-memory RGBA8 pixels directly (used
// for a solid-color placeholder texture — see kke::Application's own
// default 1x1 white texture, the "untextured" stand-in every pipeline
// sharing cube.frag needs bound regardless of whether that specific
// object has a real texture of its own).
class Texture {
public:
    Texture(VulkanDevice& device, const std::string& filePath);
    Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height);
    // Linear HDR light as RGBA half floats (VK_FORMAT_R16G16B16A16_SFLOAT):
    // one level, wrapping sideways and clamped top and bottom, the way an
    // equirectangular sky is sampled (kke::SkyRenderer).
    struct HalfFloatSky {};
    Texture(VulkanDevice& device, const uint16_t* halfRgba, uint32_t width, uint32_t height, HalfFloatSky);
    // The same with `levels` mips stored one after another in halfRgba, level 0
    // first, each half as wide and tall as the one before (kke::EnvMap::toHalfRgba),
    // sampled trilinearly: the prefiltered sky the lit shaders reflect.
    struct HalfFloatSkyMips {};
    Texture(VulkanDevice& device, const uint16_t* halfRgba, uint32_t width, uint32_t height, uint32_t levels, HalfFloatSkyMips);
    // Data, not colour (ENG-G03: normal, roughness and metallic maps):
    // VK_FORMAT_R8G8B8A8_UNORM, so the shader reads the stored numbers, and
    // mips averaged as stored (downsampleRgba8Linear), with no alpha-test
    // coverage fix.
    struct LinearData {};
    Texture(VulkanDevice& device, const std::string& filePath, LinearData);
    Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height, LinearData);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // What a bind group names (docs/plans/rhi.md, R1e).
    rhi::TextureRef ref() const { return m_gpu->ref(); }
    VkImageView imageView() const;
    VkSampler sampler() const;

    // The cooked twin kke::Texture loads instead of `filePath`: the same
    // name with .ktx2 (kke_texcook writes it), when it is there and
    // KKE_TEXTURE_KTX2 isn't 0; "" otherwise. A .ktx2 path is its own.
    static std::string compressedTwin(const std::string& filePath);

private:
    void loadFile(const std::string& filePath, bool srgb);
    // False when the file isn't a KTX2 this colour space can use (the
    // caller falls back to the source image), with the reason.
    bool loadKtx2(const std::string& ktxPath, bool srgb, std::string& why);
    void createFromPixels(const uint8_t* rgbaPixels, uint32_t width, uint32_t height, bool srgb = true);

    VulkanDevice& m_device;
    std::unique_ptr<rhi::Texture> m_gpu; // made by the RHI device: the image, its view and its sampler
};

} // namespace kke
