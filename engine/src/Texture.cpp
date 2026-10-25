#include "kke/Texture.h"
#include "kke/CookedFile.h"
#include "kke/Ktx2.h"
#include "kke/TextureCompress.h"
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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace kke {

std::string Texture::compressedTwin(const std::string& filePath) {
    std::filesystem::path p(filePath);
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".ktx2") return filePath;
    if (const char* off = std::getenv("KKE_TEXTURE_KTX2"); off && std::string(off) == "0") return {};
    p.replace_extension(".ktx2");
    const std::string twin = p.string();
    std::error_code ec;
    return std::filesystem::is_regular_file(twin, ec) ? twin : std::string();
}

bool Texture::loadKtx2(const std::string& ktxPath, bool srgb, std::string& why) {
    std::vector<uint8_t> bytes;
    if (!cooked::readAssetFile(ktxPath, bytes, &why)) return false;
    ktx2::Image image;
    if (!ktx2::read(bytes.data(), bytes.size(), image, &why)) return false;
    if (ktx2::isSrgb(image.vkFormat) != srgb) {
        why = srgb ? "cooked as data (kke_texcook --linear), used as colour" : "cooked as colour, used as data (normal, roughness or metallic map)";
        return false;
    }
    // Sampled through a full chain or none: the GPU wants every level down
    // to 1x1 for trilinear filtering, as createFromPixels makes.
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.levels = static_cast<uint32_t>(image.levels.size());
    desc.anisotropic = true;
    const bool bc7 = image.vkFormat == ktx2::kBc7Srgb || image.vkFormat == ktx2::kBc7Unorm;
    if (bc7) {
        static std::once_flag said;
        std::call_once(said, [&] {
            log::get("Texture")->info(m_device.bc7Supported() ? "compressed textures (.ktx2): BC7, sampled as they are"
                                                              : "compressed textures (.ktx2): no BC7 on this GPU, decoded to RGBA8 on load");
        });
    }
    std::vector<uint8_t> data;
    if (bc7 && m_device.bc7Supported()) {
        desc.format = srgb ? rhi::TextureFormat::BC7Srgb : rhi::TextureFormat::BC7;
        for (const std::vector<uint8_t>& level : image.levels) data.insert(data.end(), level.begin(), level.end());
    } else {
        // No BC on this GPU (most phones): decoded once here, so the game
        // still runs, at RGBA8's memory cost.
        desc.format = srgb ? rhi::TextureFormat::RGBA8Srgb : rhi::TextureFormat::RGBA8;
        data = toRgba8Chain(image);
    }
    m_gpu = rhi::VulkanGpuDevice(m_device).createTexture(desc, data.data());
    return true;
}

void Texture::loadFile(const std::string& filePath, bool srgb) {
    // A cooked twin (kke_texcook) first: smaller on disk and in VRAM, and
    // its mips are already made.
    if (const std::string twin = compressedTwin(filePath); !twin.empty()) {
        std::string why;
        if (loadKtx2(twin, srgb, why)) return;
        if (twin == filePath) throw std::runtime_error("kke::Texture: '" + filePath + "': " + why);
        log::get("Texture")->warn("'{}' not used ({}): loading '{}' instead", twin, why, filePath);
    }
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
    std::vector<uint8_t> chain;
    const uint32_t mipLevels = buildMipChainRgba8(rgbaPixels, width, height, srgb, chain);
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
