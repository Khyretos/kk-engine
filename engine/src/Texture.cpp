#include "kke/Texture.h"
#include "kke/CookedFile.h"
#include "kke/TextureMips.h"
#include "kke/VulkanDevice.h"
#include "kke/Log.h"
#include "kke/rhi/VulkanDevice.h"

// No STB_IMAGE_IMPLEMENTATION here -- that's already defined exactly
// once, in RmlVulkanRenderInterface.cpp (confirmed via a real grep
// before adding it there). This just links against the same
// already-compiled implementation.
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace kke {

void Texture::loadFile(const std::string& filePath, bool srgb) {
    int width = 0, height = 0, channels = 0;
    // Forcing 4 channels (RGBA), same reasoning as
    // RmlVulkanRenderInterface::LoadTexture: matches this class's own
    // fixed VK_FORMAT_R8G8B8A8_SRGB exactly.
    // Through kke::cooked so cooked art (docs/COOKED_ART.md) loads too.
    std::vector<uint8_t> bytes;
    std::string readError;
    if (!cooked::readAssetFile(filePath, bytes, &readError)) throw std::runtime_error("kke::Texture: " + readError);
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) {
        throw std::runtime_error("kke::Texture: failed to load '" + filePath + "' (" +
                                  (stbi_failure_reason() ? stbi_failure_reason() : "unknown reason") + ")");
    }
    createFromPixels(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), srgb);
    stbi_image_free(pixels);
}

Texture::Texture(VulkanDevice& device, const std::string& filePath) : m_device(device) { loadFile(filePath, true); }

Texture::Texture(VulkanDevice& device, const std::string& filePath, LinearData) : m_device(device) { loadFile(filePath, false); }

Texture::Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height) : m_device(device) {
    createFromPixels(rgbaPixels, width, height);
}

Texture::Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height, LinearData) : m_device(device) {
    createFromPixels(rgbaPixels, width, height, false);
}

void Texture::createFromPixels(const uint8_t* rgbaPixels, uint32_t width, uint32_t height, bool srgb) {
    // The whole mip chain, level after level, for the device to upload.
    uint32_t mipLevels = 1;
    for (uint32_t m = std::max(width, height); m > 1; m >>= 1) ++mipLevels;
    std::vector<uint8_t> chain;
    {
        uint32_t w = width, h = height;
        chain.assign(rgbaPixels, rgbaPixels + static_cast<size_t>(w) * h * 4);
        size_t offset = 0;
        // Cutout textures keep the base level's alpha-test coverage in
        // every mip, so foliage doesn't thin out with distance (see
        // scaleAlphaToCoverage). 0.5 is model.frag's cutoff.
        constexpr float kAlphaCutoff = 0.5f;
        const size_t baseTexels = static_cast<size_t>(w) * h;
        const bool cutout = srgb && hasTransparentTexels(chain.data(), baseTexels);
        const float baseCoverage = cutout ? alphaCoverage(chain.data(), baseTexels, kAlphaCutoff) : 1.0f;
        for (uint32_t level = 0; level + 1 < mipLevels; ++level) {
            uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            size_t next = offset + static_cast<size_t>(w) * h * 4;
            chain.resize(next + static_cast<size_t>(nw) * nh * 4);
            if (srgb) downsampleRgba8Srgb(chain.data() + offset, w, h, chain.data() + next, nw, nh);
            else downsampleRgba8Linear(chain.data() + offset, w, h, chain.data() + next, nw, nh);
            if (cutout) scaleAlphaToCoverage(chain.data() + next, static_cast<size_t>(nw) * nh, baseCoverage, kAlphaCutoff);
            offset = next;
            w = nw;
            h = nh;
        }
    }
    // LINEAR + REPEAT, trilinear and anisotropic where the GPU has it: floors and walls
    // seen at a glancing angle stay sharp (docs/RENDERING_PRINCIPLES.md).
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.levels = mipLevels;
    desc.format = srgb ? rhi::TextureFormat::RGBA8Srgb : rhi::TextureFormat::RGBA8;
    desc.anisotropic = true;
    m_gpu = rhi::VulkanGpuDevice(m_device).createTexture(desc, chain.data());
}

Texture::Texture(VulkanDevice& device, const uint16_t* halfRgba, uint32_t width, uint32_t height, HalfFloatSky) : m_device(device) {
    // Round the horizon, not over the poles; one level.
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = rhi::TextureFormat::RGBA16F;
    desc.wrap = rhi::Wrap::RepeatAroundOnly;
    desc.blendLevels = false;
    m_gpu = rhi::VulkanGpuDevice(m_device).createTexture(desc, halfRgba);
}

Texture::Texture(VulkanDevice& device, const uint16_t* halfRgba, uint32_t width, uint32_t height, uint32_t levels, HalfFloatSkyMips) : m_device(device) {
    // Trilinear between the roughness levels.
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.levels = levels;
    desc.format = rhi::TextureFormat::RGBA16F;
    desc.wrap = rhi::Wrap::RepeatAroundOnly;
    m_gpu = rhi::VulkanGpuDevice(m_device).createTexture(desc, halfRgba);
}

Texture::~Texture() = default;

VkImageView Texture::imageView() const { return static_cast<const rhi::VulkanTexture&>(*m_gpu).view(); }

VkSampler Texture::sampler() const { return static_cast<const rhi::VulkanTexture&>(*m_gpu).sampler(); }

} // namespace kke
