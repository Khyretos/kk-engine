#pragma once

#include "kke/Pipeline.h"
#include "kke/Buffer.h"

#include <RmlUi/Core/RenderInterface.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

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
// LoadTexture (the path actual image files — <img>, background-image —
// come through) is real now too: decodes with stb_image (already a
// real dependency elsewhere, see the root CMakeLists.txt), forcing
// RGBA8 output to match createTextureFromPixels' own expected format
// exactly, then reuses that same GPU upload path GenerateTexture
// already relies on. A file that fails to load or decode falls back to
// the same 1x1 white default untextured geometry already uses, rather
// than treating a missing/corrupt image as fatal.
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

    // A real, gdb-and-validation-layer-diagnosed bug this fixes: both
    // ReleaseGeometry() and ReleaseTexture() used to destroy their
    // underlying GPU resources immediately, with no check that the GPU
    // had actually finished using them. Renderer.cpp waits on a fence
    // for frame slot N at the start of every frame N re-uses that slot
    // (kMaxFramesInFlight = 2, see Renderer.h) — so a resource is only
    // truly safe to destroy once that many frames have genuinely
    // elapsed since it was released, not the instant RmlUi says it's
    // done with it. Confirmed as the real cause of a long-standing
    // crash (RmlUi's own debugger "Outlines" tool, which churns
    // through far more temporary geometry per frame than normal
    // content ever does) by installing real Vulkan validation layers
    // in the same sandbox that had none — the previous investigation
    // only ever had a bare, symbol-less segfault deep inside the
    // driver to go on. With validation on, the actual error was exact
    // and unambiguous: "vkCmdWriteTimestamp(): was called in
    // VkCommandBuffer ... which is invalid because bound VkBuffer ...
    // was destroyed" — a buffer freed while a still-in-flight command
    // buffer from a prior frame was still referencing it.
    struct PendingDeletion {
        uint64_t queuedAtFrame;
        std::unique_ptr<CompiledGeometry> geometry; // exactly one of these two is set
        CompiledTexture texture;
        bool isTexture = false;
    };
    std::vector<PendingDeletion> m_pendingDeletions;
    uint64_t m_frameCounter = 0;
    // A real margin beyond the bare minimum (kMaxFramesInFlight = 2),
    // not copied blindly — cheap insurance against being off-by-one
    // in reasoning about exactly when a fence wait guarantees
    // completion, for a queue this small and this infrequently
    // touched that the extra frame of latency costs nothing real.
    static constexpr uint64_t kDeletionDelayFrames = 3;

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
