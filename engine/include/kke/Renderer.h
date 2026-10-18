#pragma once

#include "kke/VulkanDevice.h"
#include "kke/SwapChain.h"
#include "kke/Buffer.h"
#include "kke/FrameRetireQueue.h"
#include "kke/ToneMap.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace kke {

class Window;
class Pipeline;

// The image the overlay (UI) draws into this frame, and how to draw into it
// again after leaving its pass: kke::RmlVulkanRenderInterface's UI layers
// switch to their own images and back (ENG-G09).
struct OverlayTarget {
    VkImage image = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    // Keeps the colour, clears depth and stencil; the image must be in
    // COLOR_ATTACHMENT_OPTIMAL when it begins, and ends in PRESENT_SRC.
    VkRenderPass resumePass = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    // Usable as a copy source (after enableCapture()).
    bool readable = false;
};

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
class SmaaPass;
class TaaPass;
class BloomPass;
class ColorLutTexture;
struct ColorLut;

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
    // `split`: the pass keeps its colour and depth, for pauseScenePass().
    void beginRenderPass(bool split = false);

    // Switches to full-resolution drawing for what goes on top of the 3D
    // image (UI, debug overlay): ends the HDR scene pass and opens the
    // swapchain's pass with the tone map as its first draw, one full-screen
    // triangle that reads the scene image (filtering it up at a render
    // scale below 1) and writes display colour (ENG-G17). Called again in
    // the same frame it only resets the viewport.
    void beginOverlayPass();

    // Ends the render pass, submits, and presents.
    void endFrame();

    // The scene's depth kept for passes after the 3D one to read (ENG-F01,
    // global illumination): stored and sampleable when on, thrown away when
    // off (the default: no bandwidth spent). Takes effect on the next frame.
    void setSceneDepthReadable(bool readable);
    bool sceneDepthReadable() const { return m_depthReadable; }
    // Ends the 3D pass early, so work can read this frame's scene before the
    // overlay pass opens (beginOverlayPass() ends it if nobody did). After
    // it, sceneImages() are readable: the colour in SHADER_READ_ONLY, the
    // depth (when readable) in DEPTH_STENCIL_READ_ONLY, both written and
    // visible to compute and fragment shaders.
    void endScenePass();
    // Refraction (ENG-F02 part 4), after beginRenderPass(true): ends the 3D
    // pass as endScenePass() does, so a pass can read what is drawn so far,
    // then resumeScenePass() opens one that goes on drawing over the same
    // colour and depth (its pipelines are the scene pass's).
    void pauseScenePass();
    void resumeScenePass();
    struct SceneImages {
        VkImageView color = VK_NULL_HANDLE; // single-sample HDR (kSceneColorFormat)
        VkImageView depth = VK_NULL_HANDLE; // depth aspect only; null unless sceneDepthReadable()
        VkExtent2D extent{};
        uint32_t depthSamples = 1;          // multisampled with MSAA: read sample 0 (sampler2DMS)
    };
    // This frame's scene images (between beginFrame and endFrame).
    SceneImages sceneImages() const;

    // 3D resolution relative to the window (resource governor, 1.6).
    // Clamped to 0.5..1. Takes effect on the next frame.
    void setRenderScale(float scale);
    // Morphological anti-aliasing over the scene before the tone map (ENG-F05,
    // kke/Smaa.h): edges MSAA can't reach (shading, alpha tests, a render
    // scale below 1). One frame, no history. Off by default.
    void setSmaa(bool on) { m_smaaOn = on; }
    bool smaa() const { return m_smaaOn; }
    // Temporal anti-aliasing, an option over the clean image (ENG-F05 part 2,
    // kke/Taa.h): needs the scene's depth readable (setSceneDepthReadable)
    // and each frame the (jittered) viewProj it was drawn with
    // (setTaaCamera). Off by default.
    void setTaa(bool on) { m_taaOn = on; }
    bool taa() const { return m_taaOn; }
    void setTaaCamera(const glm::mat4& viewProj) { m_taaViewProj = viewProj; }
    // Bloom (ENG-F10, kke/Bloom.h): bright light bleeding into what is around
    // it, mixed in before the tone map. On by default (graphics setting "bloom").
    void setBloom(bool on) { m_bloomOn = on; }
    // A colour LUT after the tone map (ENG-F10, kke/ColorLut.h); null = none.
    // A new one is uploaded once (the call waits for the GPU).
    void setColorLut(std::shared_ptr<const ColorLut> lut);
    bool bloom() const { return m_bloomOn; }
    float renderScale() const { return m_renderScale; }
    // Size of the 3D image: extent() times the render scale.
    VkExtent2D renderExtent() const;

    VkCommandBuffer currentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    // The pass render() draws in: create scene pipelines against this one.
    // Its colour is kSceneColorFormat (scene light, before tone mapping),
    // multisampled when MSAA is on.
    VkRenderPass renderPass() const { return m_scenePass; }
    // The pass renderOverlay() draws in (always single-sampled, full
    // resolution): UI pipelines (RmlUi, ImGui) are created against this.
    VkRenderPass overlayRenderPass() const { return m_swapChain->renderPass(); }
    // False on a UNORM (10-bit) swapchain: what draws in the overlay pass
    // writes sRGB-encoded colour itself (UI, ImGui, the tone-map pass).
    bool outputEncodesSrgb() const { return m_swapChain->encodesSrgb(); }
    // The image the overlay draws into this frame (call between beginFrame
    // and endFrame).
    OverlayTarget overlayTarget() const;

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
    // `kind`: Split keeps the colour and depth (stored) for a Resume pass,
    // which loads them instead of clearing.
    enum class PassKind { Whole, Split, Resume };
    VkRenderPass createSceneCompatiblePass(VkImageLayout colorFinalLayout, PassKind kind = PassKind::Whole);
    VkExtent2D extent() const { return m_swapChain->extent(); }
    bool hasStencil() const { return m_swapChain->hasStencil(); }
    // The scene pass's attachment formats: an offscreen pass with the same
    // ones (and the same subpass dependency) can use every scene pipeline.
    // The colour holds scene light; kke::toneMapCpu() turns it into display
    // colour on the CPU, as the tone-map pass does on the GPU.
    static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat colorFormat() const { return kSceneColorFormat; }
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
    // Background colour, sRGB-authored: the scene is cleared to the light
    // that the current tone map shows as this colour. Default: near-black blue.
    void setClearColor(const glm::vec3& srgb);
    // The tone map, exposure and look the next frames are shown with
    // (kke::makePostParams); the Application sets them from its Lighting
    // every frame. Default: AgX at exposure 1, no look.
    void setPostParams(const PostParams& params);
    const PostParams& postParams() const { return m_post; }
    // Inside the scene pass: clear color and depth of one part of the
    // image only (a picture-in-picture view drawn over another) and
    // point the viewport and scissor at it.
    void beginView(const VkRect2D& rect, bool clear);
    bool vsync() const;

    // Screenshots of what the player sees (3D and UI), for the benchmark's
    // best/worst-moment pictures. enableCapture() makes the swapchain
    // readable (rebuilt at the next beginFrame()); canCapture() says
    // whether it worked (the surface allows it, an 8- or 10-bit RGBA/BGRA format).
    // requestCapture() copies the next frame endFrame() presents; the
    // pixels arrive once the GPU has finished it, a frame or two later,
    // through takeCaptures(). Each copy costs a transfer on the GPU and a
    // memcpy of the image on the CPU, nothing else.
    struct FrameCapture {
        uint64_t tag = 0; // what requestCapture() was given
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba; // width * height * 4, top row first, alpha 255
    };
    void enableCapture();
    bool canCapture() const;
    void requestCapture(uint64_t tag);
    std::vector<FrameCapture> takeCaptures();

private:
    glm::vec3 m_clearSrgb{0.152f, 0.152f, 0.248f}; // linear 0.02, 0.02, 0.05
    glm::vec3 m_clearScene{0.0f}; // sceneClearColor(m_clearSrgb, m_post)
    PostParams m_post;

    void createSyncObjects();
    void createCommandBuffers();
    void createQueryPools();
    void recreateSwapChain();
    bool scaled() const { return m_renderScale < 1.0f; }
    void createScenePass();
    // The tone-map pass: its pipeline (against the swapchain's pass,
    // remade when the swapchain's format changes), sampler and one
    // descriptor set per frame in flight.
    void createPostPass();
    void destroyPostPass();
    void drawPostPass(VkCommandBuffer cmd);
    void ensureSceneTarget(uint32_t frame);
    void destroySceneTarget(uint32_t frame);

    Window& m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<SmaaPass> m_smaa; // made the first time SMAA is on
    bool m_smaaOn = false;
    std::unique_ptr<BloomPass> m_bloom; // made the first time bloom is on
    std::shared_ptr<const ColorLut> m_lut;
    std::unique_ptr<ColorLutTexture> m_lutTexture, m_identityLut; // the LUT on the GPU; a 2^3 identity bound while there is none
    bool m_bloomOn = true;
    VkImageView m_bloomSource = VK_NULL_HANDLE; // this frame's bloom (GENERAL), or none
    std::unique_ptr<TaaPass> m_taa; // made the first time TAA is on
    bool m_taaOn = false, m_taaHavePrev = false;
    glm::mat4 m_taaViewProj{1.0f}, m_taaPrevViewProj{1.0f};
    VkImageView m_postSource = VK_NULL_HANDLE; // what the tone map reads this frame (the scene, or SMAA's or TAA's result)
    VkImageLayout m_postSourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

    // Screenshots (requestCapture): one readback buffer per frame slot.
    struct CaptureSlot {
        std::unique_ptr<Buffer> buffer;
        VkDeviceSize size = 0;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint64_t tag = 0;
        bool pending = false;
    };
    std::array<CaptureSlot, kMaxFramesInFlight> m_captureSlots;
    std::vector<uint64_t> m_captureRequests;
    std::vector<FrameCapture> m_captured;
    void recordCapture(VkCommandBuffer cmd);
    void collectCapture(uint32_t frame);
    float m_lastGpuFrameTimeMs = -1.0f;

    // The 3D pass draws into an HDR image per frame in flight (smaller at
    // a render scale below 1), which the tone-map pass reads in
    // beginOverlayPass(). Created on first use.
    float m_renderScale = 1.0f;
    uint32_t m_msaa = 1;
    bool m_inScenePass = false;
    bool m_depthReadable = false;
    bool m_overlayOpen = false; // beginOverlayPass() opened the swapchain's pass this frame
    VkRenderPass m_scenePass = VK_NULL_HANDLE;
    VkRenderPass m_scenePassSplit = VK_NULL_HANDLE, m_scenePassResume = VK_NULL_HANDLE; // pauseScenePass()
    VkFormat m_scenePassFormats[2] = {};
    uint32_t m_scenePassSamples = 0;
    bool m_scenePassDepthStored = false;
    struct SceneTarget {
        VkExtent2D extent{};
        // color: single-sample, read by the tone-map pass. msaaColor: only with
        // MSAA, resolved into color at the end of the pass.
        VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE, msaaColor = VK_NULL_HANDLE;
        VmaAllocation colorAlloc = VK_NULL_HANDLE, depthAlloc = VK_NULL_HANDLE, msaaColorAlloc = VK_NULL_HANDLE;
        VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE, msaaColorView = VK_NULL_HANDLE;
        VkImageView depthSampleView = VK_NULL_HANDLE; // depth aspect only, when the depth is readable
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    SceneTarget m_sceneTargets[kMaxFramesInFlight];

    std::unique_ptr<Pipeline> m_postPipeline;
    VkFormat m_postFormat = VK_FORMAT_UNDEFINED;
    VkSampler m_postSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_postSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_postPool = VK_NULL_HANDLE;
    VkDescriptorSet m_postSets[kMaxFramesInFlight] = {};
};

} // namespace kke
