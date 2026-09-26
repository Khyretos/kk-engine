#pragma once

#include "kke/VulkanDevice.h"
#include "kke/SwapChain.h"
#include "kke/FrameRetireQueue.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kke {

class Window;

// Owns the frame lifecycle only — acquire, record, submit, present, plus
// GPU timing. It does NOT own any Pipeline or Mesh: those are "modules'"
// business (see Module.h / Application.h). This is deliberate — a renderer
// that owns one hardcoded pipeline can't host a grid pipeline, a particle
// pipeline, and a cube pipeline all drawing into the same frame.
//
// Frame shape:
//   if (renderer.beginFrame()) {              // acquire + start recording
//       // ... optional compute dispatches + barriers here, pre-render-pass
//       renderer.beginRenderPass();            // clears color+depth, sets viewport/scissor
//       // ... bind pipelines, push constants, draw calls here
//       renderer.endFrame();                   // ends render pass, submits, presents
//   }
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns false if the frame was skipped (e.g. swapchain out of date).
    // On success, the command buffer is recording but no render pass is
    // active yet — this is where compute dispatches belong.
    bool beginFrame();

    // Begins the swapchain's render pass (color+depth clear) and sets a
    // full-viewport/scissor. Call after beginFrame(), before any draws.
    void beginRenderPass();

    // Switches to full-resolution drawing for what goes on top of the 3D
    // image (UI, debug overlay). With a render scale below 1 this ends
    // the scaled 3D pass, upscales it onto the swapchain image and opens
    // the overlay pass; at scale 1 it does nothing (one pass, no copy).
    void beginOverlayPass();

    // Ends the render pass, submits, and presents.
    void endFrame();

    // 3D resolution relative to the window (resource governor, 1.6).
    // Clamped to 0.5..1; ignored (stays 1) when the swapchain can't be
    // blitted to. Takes effect on the next frame.
    void setRenderScale(float scale);
    float renderScale() const { return m_renderScale; }
    // Size of the 3D image: extent() times the render scale.
    VkExtent2D renderExtent() const;

    VkCommandBuffer currentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    // The pass render() draws in: create scene pipelines against this one.
    // With MSAA it's the multisampled scene pass; otherwise the swapchain's.
    VkRenderPass renderPass() const { return m_msaa > 1 ? m_scenePass : m_swapChain->renderPass(); }
    // The pass renderOverlay() draws in (always single-sampled, full
    // resolution): UI pipelines (RmlUi, ImGui) are created against this.
    VkRenderPass overlayRenderPass() const { return m_swapChain->renderPass(); }

    // Multisample anti-aliasing for the 3D pass: 1 (off), 2, 4 or 8,
    // clamped to what the device supports. Must be set before any scene
    // pipeline is created (pipelines bake the sample count in), so it
    // takes effect on restart. The MSAA image is resolved into a single-
    // sample one each frame, then the overlay draws on top as usual.
    // No temporal filtering, no dithering (docs/RENDERING_PRINCIPLES.md).
    void setMsaaSamples(uint32_t samples);
    uint32_t msaaSamples() const { return m_msaa; }
    // A new render pass laid out like the scene pass (same formats, sample
    // count, resolve and subpass dependency), so every scene pipeline works
    // in it; its single-sample colour ends in colorFinalLayout. For
    // offscreen views of the scene (thumbnails). Caller destroys it.
    VkRenderPass createSceneCompatiblePass(VkImageLayout colorFinalLayout);
    VkExtent2D extent() const { return m_swapChain->extent(); }
    bool hasStencil() const { return m_swapChain->hasStencil(); }
    // The main pass's attachment formats: an offscreen pass with the same
    // ones (and the same subpass dependency) can use every pipeline.
    VkFormat colorFormat() const { return m_swapChain->imageFormat(); }
    VkFormat depthFormat() const { return m_swapChain->depthFormat(); }
    float aspectRatio() const {
        auto e = m_swapChain->extent();
        return e.height > 0 ? static_cast<float>(e.width) / static_cast<float>(e.height) : 1.0f;
    }
    uint32_t swapChainImageCount() const { return static_cast<uint32_t>(m_swapChain->imageCount()); }

    VulkanDevice& device() { return *m_device; }

    // GPU time (in milliseconds) the *previous completed* frame's render
    // pass took, measured via Vulkan timestamp queries. -1 until the first
    // frame has fully round-tripped.
    float lastGpuFrameTimeMs() const { return m_lastGpuFrameTimeMs; }

    // Frames the CPU may be ahead of the GPU. Anything a module writes
    // from the CPU every frame (a dynamic vertex buffer, say) needs this
    // many copies, indexed by currentFrameIndex(): by the time
    // beginFrame() returns, the GPU is guaranteed done with that index's
    // previous use, but not with the other one.
    static constexpr int kMaxFramesInFlight = 2;
    uint32_t currentFrameIndex() const { return m_currentFrame; }
    // Keeps a GPU resource alive until every frame that may reference it
    // has finished on the GPU, then drops it. Use when an object that owns
    // buffers is removed mid-game (a fracture piece, a despawned model):
    //   renderer.retire(std::move(myBuffer));   // any unique_ptr/shared_ptr
    template <class T>
    void retire(std::unique_ptr<T> resource) { m_retired.retire(m_recordedFrames, std::shared_ptr<T>(std::move(resource))); }
    void retire(std::shared_ptr<void> resource) { m_retired.retire(m_recordedFrames, std::move(resource)); }
    // Changes present mode; the swapchain is rebuilt at the start of the
    // next beginFrame() (never mid-frame, while a command buffer that
    // references the old one is being recorded).
    void setVSync(bool vsync);
    // Background colour, sRGB-authored (converted to linear for the sRGB
    // swapchain). Default: near-black blue.
    void setClearColor(const glm::vec3& srgb);
    // Inside the scene pass: clear color and depth of one part of the
    // image only (a picture-in-picture view drawn over another) and
    // point the viewport and scissor at it.
    void beginView(const VkRect2D& rect, bool clear);
    bool vsync() const;

private:
    glm::vec3 m_clearLinear{0.02f, 0.02f, 0.05f};

    void createSyncObjects();
    void createCommandBuffers();
    void createQueryPools();
    void recreateSwapChain();
    bool scaled() const { return m_renderScale < 1.0f && m_swapChain->canBlitTo(); }
    // The 3D pass draws into an offscreen target (then blitted to the
    // swapchain) when scaled or multisampled.
    bool offscreenScene() const { return scaled() || m_msaa > 1; }
    void createScenePass();
    void ensureSceneTarget(uint32_t frame);
    void destroySceneTarget(uint32_t frame);

    Window& m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<SwapChain> m_swapChain;

    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkQueryPool> m_timestampPools; // 2 timestamps per frame-in-flight: [0]=start, [1]=end
    std::vector<bool> m_timestampPoolHasData;

    uint32_t m_currentFrame = 0;
    // Frames recorded so far (successful beginFrame() calls).
    uint64_t m_recordedFrames = 0;
    FrameRetireQueue m_retired;
    bool m_recreatePending = false;
    uint32_t m_currentImageIndex = 0;
    float m_lastGpuFrameTimeMs = -1.0f;

    // Render scale: the 3D pass draws into a smaller image per frame in
    // flight, blitted up in beginOverlayPass(). Created on first use.
    float m_renderScale = 1.0f;
    uint32_t m_msaa = 1;
    bool m_inScenePass = false;
    VkRenderPass m_scenePass = VK_NULL_HANDLE;
    VkFormat m_scenePassFormats[2] = {};
    uint32_t m_scenePassSamples = 0;
    struct SceneTarget {
        VkExtent2D extent{};
        // color: single-sample, the blit source. msaaColor: only with
        // MSAA, resolved into color at the end of the pass.
        VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE, msaaColor = VK_NULL_HANDLE;
        VmaAllocation colorAlloc = VK_NULL_HANDLE, depthAlloc = VK_NULL_HANDLE, msaaColorAlloc = VK_NULL_HANDLE;
        VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE, msaaColorView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    SceneTarget m_sceneTargets[kMaxFramesInFlight];
};

} // namespace kke
