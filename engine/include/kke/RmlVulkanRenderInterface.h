#pragma once

#include "kke/Pipeline.h"
#include "kke/Buffer.h"
#include "kke/Renderer.h"
#include "kke/RmlFilters.h"

#include <RmlUi/Core/RenderInterface.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
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
    // stencilAvailable: the render pass's depth attachment has a stencil
    // aspect (Renderer::hasStencil()); without it clip masks are skipped.
    // targetEncoded: the frame's image is UNORM (a 10-bit swapchain,
    // SwapChain::encodesSrgb() false), so the UI writes sRGB-encoded colour.
    RmlVulkanRenderInterface(VulkanDevice& device, VkRenderPass renderPass, bool stencilAvailable = false, bool targetEncoded = false);
    ~RmlVulkanRenderInterface() override;

    // Called by UiModule once per frame, right before Rml::Context::Render()
    // — NOT part of Rml::RenderInterface's own contract; this is how we
    // thread frame-specific Vulkan state (which command buffer, how big is
    // the screen) through to the methods RmlUi calls without knowing any
    // of that exists.
    // `screenSizePixels` is the whole frame; `origin` is where the context's
    // (0,0) sits in it, so a context sized to the screen's safe area draws
    // inside that area (UiModule). `target` is the frame's own image
    // (Renderer::overlayTarget()), layer 0 of RmlUi's layer stack.
    void beginFrame(VkCommandBuffer cmd, glm::vec2 screenSizePixels, glm::vec2 origin, const OverlayTarget& target);

    // Called right after Rml::Context::Render(): leaves the frame's own
    // image's pass open again for whatever draws after the UI.
    void endFrame();
    // True once, the first time a backdrop-filter needed to read the frame's
    // own image and it could not be copied: UiModule then asks the renderer
    // for a readable swapchain (Renderer::enableCapture()).
    bool takeReadableRequest();

    // Rml::RenderInterface — required
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    // Optional in Rml::RenderInterface — without it every CSS `transform`
    // (scale/rotate/translate, and animations of them) was silently ignored.
    void SetTransform(const Rml::Matrix4f* transform) override;

    // Optional — CSS gradient decorators (linear-/radial-/conic-gradient,
    // and repeating- variants). Without these, RmlUi silently drew
    // nothing for any gradient background. See shaders/rml_gradient.frag.
    Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle shader) override;

    // Optional — clip masks. RmlUi uses these instead of a scissor
    // rectangle whenever an element with overflow clipping sits inside a
    // CSS transform (e.g. a scrolling list in a panel that slides in).
    // Without them nothing was clipped at all in that case.
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

    // Optional — layers and filters (ENG-G09, engine/src/RmlVulkanLayers.cpp):
    // CSS `filter`, `backdrop-filter`, `box-shadow` and `mask-image`. Without
    // them RmlUi drew none of these. Same scheme as RmlUi's GL3 backend:
    // each layer is a full-frame image, filters are full-frame passes.
    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;
    Rml::TextureHandle SaveLayerAsTexture() override;
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

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
        bool linear = false; // saved from a layer: drawn with rml_layer.frag
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
    struct CompiledGradient {
        CompiledTexture ramp;
        glm::vec2 p{0.0f}, v{0.0f};
        float t0 = 0.0f, t1 = 1.0f;
        int func = 0;
    };
    std::unordered_map<uintptr_t, CompiledGradient> m_gradients;
    uintptr_t m_nextGradientHandle = 1;
    std::unique_ptr<Pipeline> m_gradientPipeline;
    std::unique_ptr<Pipeline> m_maskReplacePipeline, m_maskIncrementPipeline; // stencil-only writes
    bool m_stencilAvailable = false;
    bool m_clipMaskEnabled = false;
    uint32_t m_stencilRef = 0;
    void applyStencilState(); // re-sets dynamic stencil state after a pipeline bind

    // The clip-mask operations since the last Set/SetInverse: a new pass
    // clears the stencil, so beginPass() draws them again.
    struct ClipMaskOp {
        Rml::ClipMaskOperation operation;
        const CompiledGeometry* geometry; // kept alive by m_pendingDeletions past this frame
        glm::vec2 translation;
        glm::mat4 transform;
    };
    std::vector<ClipMaskOp> m_clipOps;
    void drawClipMask(const ClipMaskOp& op);

    // --- Layers (engine/src/RmlVulkanLayers.cpp) ---
    // A full-frame colour image in the swapchain's format, so every RmlUi
    // pipeline draws into it, with its framebuffer (sharing m_layerDepth)
    // and a descriptor set to sample it.
    struct LayerTarget {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkExtent2D extent{};
    };
    void createLayerResources();
    void destroyLayerResources();
    std::unique_ptr<LayerTarget> createLayerTarget(VkExtent2D extent);
    void destroyLayerTarget(LayerTarget& target);
    LayerTarget* layerTarget(Rml::LayerHandle handle); // nullptr = the frame's own image
    Rml::LayerHandle topLayer() const;
    void transition(LayerTarget& target, VkImageLayout layout);
    void endPass();
    void beginPass(LayerTarget* target, bool filterPass = false); // nullptr = the frame's own image
    void copyToPostprocess(Rml::LayerHandle source);
    void drawFullscreen(LayerTarget* destination, LayerTarget& source, const Pipeline& pipeline, const RmlFilterPush& push,
                        VkDescriptorSet mask);
    void blur(float sigma, std::unique_ptr<LayerTarget>& image, std::unique_ptr<LayerTarget>& temp, const RmlFilterPush& region);
    void applyFilters(Rml::Span<const Rml::CompiledFilterHandle> filters);
    VkRect2D clampedScissor() const;
    VkRect2D filterArea(const LayerTarget& target) const;

    OverlayTarget m_base;
    VkImageLayout m_baseLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    LayerTarget* m_current = nullptr; // the open pass's target; nullptr = the frame's own image
    bool m_passOpen = false;
    bool m_readableRequest = false, m_readableAsked = false;
    VkRenderPass m_layerPass = VK_NULL_HANDLE, m_postPass = VK_NULL_HANDLE;
    VkExtent2D m_layerExtent{};
    VkFormat m_layerFormat = VK_FORMAT_UNDEFINED, m_layerDepthFormat = VK_FORMAT_UNDEFINED;
    VkImage m_layerDepth = VK_NULL_HANDLE;
    VmaAllocation m_layerDepthAllocation = VK_NULL_HANDLE;
    VkImageView m_layerDepthView = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<LayerTarget>> m_layers; // handle n is m_layers[n - 1], reused by depth
    std::vector<Rml::LayerHandle> m_layerStack;         // pushed this frame; empty = the frame's own image
    std::array<std::unique_ptr<LayerTarget>, 3> m_post;  // filter ping-pong; [0] holds the result
    std::unique_ptr<LayerTarget> m_mask;                 // the last SaveLayerAsMaskImage
    std::array<std::unique_ptr<LayerTarget>, 2> m_half;  // half-size images for wide blurs
    std::unordered_map<uintptr_t, RmlFilter> m_filters;
    uintptr_t m_nextFilterHandle = 1;
    std::unique_ptr<Pipeline> m_layerTexturePipeline;               // RenderGeometry with a saved layer texture
    std::unique_ptr<Pipeline> m_compositeBlend, m_compositeReplace; // onto a layer, under the clip mask
    std::unique_ptr<Pipeline> m_filterReplace, m_filterBlend;       // between postprocess images

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
    glm::vec2 m_origin{0.0f, 0.0f};
    glm::mat4 m_projection{1.0f};
    glm::mat4 m_transform{1.0f};
    bool m_scissorEnabled = false;
    VkRect2D m_scissor{}; // frame pixels; the whole frame while scissoring is off
};

} // namespace kke
