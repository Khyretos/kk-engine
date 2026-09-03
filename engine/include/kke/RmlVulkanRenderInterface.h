#pragma once

#include "kke/Pipeline.h"
#include "kke/Buffer.h"

#include <RmlUi/Core/RenderInterface.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace kke {

class VulkanDevice;

// The Vulkan backend for RmlUi — implements Rml::RenderInterface by
// compiling RmlUi's geometry into this engine's own Buffer/Pipeline
// primitives, the same way every other module draws.
//
// Textured AND untextured geometry both work: GenerateTexture (the path
// font glyph atlases come through) creates a real VMA-backed image +
// sampler + descriptor set. Untextured draws (texture handle 0) bind a
// persistent 1x1 opaque-white default texture instead of needing a
// separate pipeline/shader variant — sampling white and multiplying by
// vertex color in the fragment shader is a no-op, so one code path
// covers both cases.
//
// STILL STUBBED: LoadTexture (the path actual image files — <img>,
// background-image — come through) returns the same default white
// texture rather than decoding real image data. That needs stb_image
// wired up first (see README Roadmap) and is a separate, independent
// piece of work from what made text rendering possible here.
class RmlVulkanRenderInterface : public Rml::RenderInterface {
public:
    RmlVulkanRenderInterface(VulkanDevice& device, VkRenderPass renderPass);
    ~RmlVulkanRenderInterface() override;

    // Called by UiModule once per frame, right before Rml::Context::Render()
    // — NOT part of Rml::RenderInterface's own contract; this is how we
    // thread frame-specific Vulkan state (which command buffer, how big is
    // the screen) through to the methods RmlUi calls without knowing any
    // of that exists.
    void beginFrame(VkCommandBuffer cmd, glm::vec2 screenSizePixels);

    // Rml::RenderInterface — required
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct CompiledGeometry {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount;
    };

    struct CompiledTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    CompiledTexture createTextureFromPixels(const uint8_t* pixels, uint32_t width, uint32_t height);
    void destroyTexture(CompiledTexture& texture);
    VkDescriptorSet allocateAndWriteDescriptor(VkImageView view);

    VulkanDevice& m_device;
    std::unique_ptr<Pipeline> m_pipeline;

    VkDescriptorSetLayout m_textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    CompiledTexture m_defaultTexture; // bound for untextured draws (texture handle 0)

    std::unordered_map<uintptr_t, std::unique_ptr<CompiledGeometry>> m_geometry;
    uintptr_t m_nextGeometryHandle = 1;
    std::unordered_map<uintptr_t, CompiledTexture> m_textures;
    uintptr_t m_nextTextureHandle = 1;

    VkCommandBuffer m_currentCmd = VK_NULL_HANDLE;
    glm::vec2 m_screenSize{0.0f, 0.0f};
    bool m_scissorEnabled = false;
};

} // namespace kke
