#include "kke/Renderer.h"

#include "BloomPass.h"
#include "ColorLutTexture.h"
#include "DepthMapPass.h"
#include "DofPass.h"
#include "MotionBlurPass.h"
#include "SmaaPass.h"
#include "TaaPass.h"

#include <cmath>
#include "kke/Window.h"
#include "kke/Pipeline.h"
#include "kke/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace kke {

Renderer::Renderer(Window& window) : m_window(window) {
#if KKE_ENABLE_VALIDATION
    bool wantValidation = true;
#else
    bool wantValidation = false;
#endif
    m_device = std::make_unique<VulkanDevice>(window, wantValidation);
    m_swapChain = std::make_unique<SwapChain>(*m_device, window);

    createCommandBuffers();
    createSyncObjects();
    createQueryPools();
    createScenePass();
    createPostPass();
    setClearColor(m_clearSrgb);
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(m_device->device());
    m_retired.releaseAll();
    m_smaa.reset();
    m_bloom.reset();
    m_lutTexture.reset();
    m_identityLut.reset();
    m_taa.reset();
    m_dof.reset();
    m_motionBlur.reset();
    m_depthMap.reset();
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) destroySceneTarget(i);
    destroyPostPass();
    if (m_scenePass) vkDestroyRenderPass(m_device->device(), m_scenePass, nullptr);
    if (m_scenePassSplit) vkDestroyRenderPass(m_device->device(), m_scenePassSplit, nullptr);
    if (m_scenePassResume) vkDestroyRenderPass(m_device->device(), m_scenePassResume, nullptr);
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        vkDestroySemaphore(m_device->device(), m_imageAvailable[i], nullptr);
        vkDestroySemaphore(m_device->device(), m_renderFinished[i], nullptr);
        vkDestroyFence(m_device->device(), m_inFlightFences[i], nullptr);
        vkDestroyQueryPool(m_device->device(), m_timestampPools[i], nullptr);
    }
}

void Renderer::createCommandBuffers() {
    m_commandBuffers.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_device->commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    VK_CHECK(vkAllocateCommandBuffers(m_device->device(), &allocInfo, m_commandBuffers.data()));
}

void Renderer::createSyncObjects() {
    m_imageAvailable.resize(kMaxFramesInFlight);
    m_renderFinished.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(m_device->device(), &semInfo, nullptr, &m_imageAvailable[i]));
        VK_CHECK(vkCreateSemaphore(m_device->device(), &semInfo, nullptr, &m_renderFinished[i]));
        VK_CHECK(vkCreateFence(m_device->device(), &fenceInfo, nullptr, &m_inFlightFences[i]));
    }
}

void Renderer::createQueryPools() {
    m_timestampPools.resize(kMaxFramesInFlight);
    m_timestampPoolHasData.assign(kMaxFramesInFlight, false);

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = 2; // 0 = frame start, 1 = frame end

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        VK_CHECK(vkCreateQueryPool(m_device->device(), &poolInfo, nullptr, &m_timestampPools[i]));
    }
}

void Renderer::recreateSwapChain() {
    m_swapChain->recreate();
}

void Renderer::enableCapture() {
    if (m_swapChain->readable()) return;
    m_swapChain->setReadable(true);
    m_recreatePending = true;
}

bool Renderer::canCapture() const { return m_swapChain->readable() && capturableFormat(m_swapChain->imageFormat()); }

OverlayTarget Renderer::overlayTarget() const {
    OverlayTarget t;
    t.image = m_swapChain->image(m_currentImageIndex);
    t.framebuffer = m_swapChain->framebuffer(m_currentImageIndex);
    t.resumePass = m_swapChain->overlayPass();
    t.colorFormat = m_swapChain->imageFormat();
    t.depthFormat = m_swapChain->depthFormat();
    t.extent = m_swapChain->extent();
    t.readable = m_swapChain->readable();
    return t;
}

void Renderer::requestCapture(uint64_t tag) { m_captureRequests.push_back(tag); }

std::vector<Renderer::FrameCapture> Renderer::takeCaptures() {
    std::vector<FrameCapture> out;
    out.swap(m_captured);
    return out;
}

// After the frame's last pass: the swapchain image is in PRESENT_SRC.
// Copy it to this slot's readback buffer and put it back for presenting.
void Renderer::recordCapture(VkCommandBuffer cmd) {
    const uint64_t tag = m_captureRequests.front();
    m_captureRequests.erase(m_captureRequests.begin());
    if (!canCapture()) return; // not readable (yet, or on this device): the request is dropped
    CaptureSlot& slot = m_captureSlots[m_currentFrame];
    const VkExtent2D e = m_swapChain->extent();
    const VkDeviceSize size = VkDeviceSize(e.width) * e.height * 4;
    if (!slot.buffer || slot.size < size) {
        slot.buffer = std::make_unique<Buffer>(*m_device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        slot.size = size;
    }
    slot.extent = e;
    slot.format = m_swapChain->imageFormat();
    slot.tag = tag;
    slot.pending = true;

    const VkImage image = m_swapChain->image(m_currentImageIndex);
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

    // The slot's buffer was written by an earlier capture and read by the
    // host since: both finish before this copy overwrites it.
    VkBufferMemoryBarrier reuse{};
    reuse.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    reuse.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
    reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    reuse.srcQueueFamilyIndex = reuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    reuse.buffer = slot.buffer->handle();
    reuse.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         1, &reuse, 0, nullptr);

    VkBufferImageCopy copy{};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { e.width, e.height, 1 };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.buffer->handle(), 1, &copy);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    // The host reads the buffer after the fence; make the copy visible to it.
    VkBufferMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = slot.buffer->handle();
    hb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0, nullptr);
}

void Renderer::collectCapture(uint32_t frame) {
    CaptureSlot& slot = m_captureSlots[frame];
    if (!slot.pending) return;
    slot.pending = false;
    if (!capturableFormat(slot.format)) return;
    FrameCapture c;
    c.tag = slot.tag;
    c.width = slot.extent.width;
    c.height = slot.extent.height;
    std::vector<uint8_t> raw(size_t(c.width) * c.height * 4);
    slot.buffer->download(raw.data(), raw.size());
    c.rgba.resize(raw.size());
    captureToRgba8(raw.data(), size_t(c.width) * c.height, slot.format, c.rgba.data());
    m_captured.push_back(std::move(c));
}

void Renderer::setVSync(bool vsync) {
    if (vsync == m_swapChain->vsync()) return;
    m_swapChain->setVSync(vsync);
    m_recreatePending = true;
}

bool Renderer::vsync() const { return m_swapChain->vsync(); }

bool Renderer::beginFrame() {
    if (m_recreatePending) {
        m_recreatePending = false;
        recreateSwapChain();
    }
    vkWaitForFences(m_device->device(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    m_occlusionReadThisFrame = false; // this slot's depth map is finished now: occlusionMap() reads it
    collectCapture(m_currentFrame); // the fence says this slot's copy is done

    // The fence wait above guarantees this frame slot's prior GPU work (if
    // any) has fully completed, so reading its timestamp results here can't
    // stall — this is last frame's timing, not this one's.
    if (m_timestampPoolHasData[m_currentFrame]) {
        uint64_t timestamps[2];
        VkResult qr = vkGetQueryPoolResults(
            m_device->device(), m_timestampPools[m_currentFrame], 0, 2,
            sizeof(timestamps), timestamps, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (qr == VK_SUCCESS) {
            double periodNs = m_device->timestampPeriodNs();
            m_lastGpuFrameTimeMs = static_cast<float>((timestamps[1] - timestamps[0]) * periodNs / 1'000'000.0);
        }
    }

    VkResult result = vkAcquireNextImageKHR(
        m_device->device(), m_swapChain->handle(), UINT64_MAX,
        m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    vkResetFences(m_device->device(), 1, &m_inFlightFences[m_currentFrame]);

    // This slot's fence covered frame m_recordedFrames - 1 (the previous
    // use of this slot, kMaxFramesInFlight frames ago); queue order means
    // everything before it is done too. Frames are numbered from 1.
    ++m_recordedFrames;
    if (m_recordedFrames > kMaxFramesInFlight) m_retired.releaseCompleted(m_recordedFrames - kMaxFramesInFlight);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    vkCmdResetQueryPool(cmd, m_timestampPools[m_currentFrame], 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPools[m_currentFrame], 0);
    m_timestampPoolHasData[m_currentFrame] = true;

    return true;
}

void Renderer::setClearColor(const glm::vec3& srgb) {
    m_clearSrgb = srgb;
    m_clearScene = sceneClearColor(srgb, m_post);
}

void Renderer::setPostParams(const PostParams& params) {
    const bool same = params.tone == m_post.tone && params.lookSlope == m_post.lookSlope && params.lookOffset == m_post.lookOffset &&
                      params.lookPower == m_post.lookPower;
    if (same) return;
    m_post = params;
    m_clearScene = sceneClearColor(m_clearSrgb, m_post); // the background keeps its colour under the new curve
}

void Renderer::setRenderScale(float scale) {
    m_renderScale = std::clamp(scale, 0.5f, 1.0f);
}

VkExtent2D Renderer::renderExtent() const {
    VkExtent2D e = m_swapChain->extent();
    if (!scaled()) return e;
    return { std::max(1u, static_cast<uint32_t>(std::lround(e.width * m_renderScale))),
             std::max(1u, static_cast<uint32_t>(std::lround(e.height * m_renderScale))) };
}

void Renderer::setMsaaSamples(uint32_t samples) {
    uint32_t want = 1;
    while (want * 2 <= samples && want < 8) want *= 2; // 1, 2, 4 or 8
    const uint32_t maxSupported = static_cast<uint32_t>(m_device->maxUsableSampleCount());
    m_msaa = std::min(want, maxSupported);
    createScenePass(); // renderPass() hands it out from now on
}

// The scene pass's layout: HDR colour (kSceneColorFormat) and the
// swapchain's depth format; the colour ends in colorFinalLayout. With MSAA: the colour
// and depth are multisampled and a third, single-sample attachment
// receives the resolved colour; the render pass does the resolve.
VkRenderPass Renderer::createSceneCompatiblePass(VkImageLayout colorFinalLayout, PassKind kind) {
    const VkFormat color = kSceneColorFormat, depth = m_swapChain->depthFormat();
    const VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_msaa);
    const bool msaa = m_msaa > 1;

    std::array<VkAttachmentDescription, 3> a{};
    a[0].format = color;
    a[0].samples = samples;
    a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // The multisampled colour only lives until it's resolved.
    a[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : colorFinalLayout;
    // A split pass keeps the multisampled colour for the pass that resumes it.
    if (kind == PassKind::Split) a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[1].format = depth;
    a[1].samples = samples;
    a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Kept only when a pass after this one reads it (setSceneDepthReadable).
    a[1].storeOp = m_depthReadable ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].stencilLoadOp = m_swapChain->hasStencil() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (kind == PassKind::Split) {
        a[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (m_swapChain->hasStencil()) a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    } else if (kind == PassKind::Resume) {
        // Goes on over what the split pass drew (resumeScenePass() makes them attachments again).
        a[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        if (m_swapChain->hasStencil()) a[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    a[2].format = color;
    a[2].samples = VK_SAMPLE_COUNT_1_BIT;
    a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully written by the resolve
    a[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[2].finalLayout = colorFinalLayout;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference resolveRef{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pResolveAttachments = msaa ? &resolveRef : nullptr;
    subpass.pDepthStencilAttachment = &depthRef;

    // Before the pass clears its images: the last frame that used them has
    // finished writing them (colour, the MSAA colour, depth) and the
    // tone-map pass has finished reading the colour. The scene pass no
    // longer shares pipelines with the swapchain's pass (other formats),
    // so its dependency need not match that one. The hand-off to the
    // tone-map pass is a barrier in beginOverlayPass().
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; // compute reading last frame's images (endScenePass)
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = msaa ? 3u : 2u;
    info.pAttachments = a.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    VkRenderPass pass = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(m_device->device(), &info, nullptr, &pass));
    m_device->setRenderPassSamples(pass, samples);
    return pass;
}

void Renderer::createScenePass() {
    VkFormat color = kSceneColorFormat, depth = m_swapChain->depthFormat();
    if (m_scenePass && m_scenePassFormats[0] == color && m_scenePassFormats[1] == depth && m_scenePassSamples == m_msaa &&
        m_scenePassDepthStored == m_depthReadable)
        return;
    if (m_scenePass) vkDestroyRenderPass(m_device->device(), m_scenePass, nullptr);
    if (m_scenePassSplit) vkDestroyRenderPass(m_device->device(), m_scenePassSplit, nullptr);
    if (m_scenePassResume) vkDestroyRenderPass(m_device->device(), m_scenePassResume, nullptr);
    m_scenePassFormats[0] = color;
    m_scenePassFormats[1] = depth;
    m_scenePassSamples = m_msaa;
    m_scenePassDepthStored = m_depthReadable;
    // The pass leaves the 3D image as an attachment; beginOverlayPass()
    // moves it to SHADER_READ_ONLY with a barrier that makes the pass's
    // writes visible to the tone-map pass (a final-layout transition here
    // isn't ordered before it, which tiled GPUs show as unwritten patches).
    m_scenePass = createSceneCompatiblePass(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_scenePassSplit = createSceneCompatiblePass(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, PassKind::Split);
    m_scenePassResume = createSceneCompatiblePass(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, PassKind::Resume);
    // Formats changed: every target was made for the old pass.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) m_sceneTargets[i].extent = {};
}

void Renderer::destroySceneTarget(uint32_t frame) {
    if (m_lastDepthFrame == static_cast<int>(frame)) m_lastDepthFrame = -1; // its depth goes with it
    SceneTarget& t = m_sceneTargets[frame];
    VkDevice dev = m_device->device();
    if (t.framebuffer) vkDestroyFramebuffer(dev, t.framebuffer, nullptr);
    if (t.colorView) vkDestroyImageView(dev, t.colorView, nullptr);
    if (t.depthView) vkDestroyImageView(dev, t.depthView, nullptr);
    if (t.msaaColorView) vkDestroyImageView(dev, t.msaaColorView, nullptr);
    if (t.depthSampleView) vkDestroyImageView(dev, t.depthSampleView, nullptr);
    if (t.color) vmaDestroyImage(m_device->allocator(), t.color, t.colorAlloc);
    if (t.msaaColor) vmaDestroyImage(m_device->allocator(), t.msaaColor, t.msaaColorAlloc);
    if (t.depth) vmaDestroyImage(m_device->allocator(), t.depth, t.depthAlloc);
    t = SceneTarget{};
}

// Only this frame slot's target is touched, and beginFrame() already
// waited on its fence, so nothing on the GPU still uses it.
void Renderer::ensureSceneTarget(uint32_t frame) {
    createScenePass();
    VkExtent2D e = renderExtent();
    SceneTarget& t = m_sceneTargets[frame];
    if (t.framebuffer && t.extent.width == e.width && t.extent.height == e.height && (t.depthSampleView != VK_NULL_HANDLE) == m_depthReadable)
        return;
    destroySceneTarget(frame);
    t.extent = e;

    auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
                         VkImage& image, VmaAllocation& alloc, VkImageView& view) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { e.width, e.height, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = format;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = usage;
        ii.samples = samples;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_CHECK(vmaCreateImage(m_device->allocator(), &ii, &ai, &image, &alloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { aspect, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device->device(), &vi, nullptr, &view));
    };
    const VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_msaa);
    makeImage(m_scenePassFormats[0], VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, t.color, t.colorAlloc, t.colorView);
    makeImage(m_scenePassFormats[1], VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | (m_depthReadable ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
              VK_IMAGE_ASPECT_DEPTH_BIT | (m_swapChain->hasStencil() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0), samples, t.depth,
              t.depthAlloc, t.depthView);
    if (m_depthReadable) {
        // Shaders read depth only: a view of that aspect alone.
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.depth;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_scenePassFormats[1];
        vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device->device(), &vi, nullptr, &t.depthSampleView));
    }
    std::vector<VkImageView> views;
    if (m_msaa > 1) {
        makeImage(m_scenePassFormats[0], VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, samples, t.msaaColor,
                  t.msaaColorAlloc, t.msaaColorView);
        views = { t.msaaColorView, t.depthView, t.colorView }; // attachment 2 = resolve target
    } else {
        views = { t.colorView, t.depthView };
    }
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = m_scenePass;
    fb.attachmentCount = static_cast<uint32_t>(views.size());
    fb.pAttachments = views.data();
    fb.width = e.width;
    fb.height = e.height;
    fb.layers = 1;
    VK_CHECK(vkCreateFramebuffer(m_device->device(), &fb, nullptr, &t.framebuffer));
}

static void setFullViewport(VkCommandBuffer cmd, VkExtent2D e) {
    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(e.width), static_cast<float>(e.height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, e };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Renderer::beginView(const VkRect2D& rect, bool clear) {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    VkViewport viewport{ static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y), static_cast<float>(rect.extent.width),
                         static_cast<float>(rect.extent.height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &rect);
    if (!clear || rect.extent.width == 0 || rect.extent.height == 0) return;
    VkClearAttachment att[2]{};
    att[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    att[0].colorAttachment = 0;
    att[0].clearValue.color = { { m_clearScene.r, m_clearScene.g, m_clearScene.b, 1.0f } };
    att[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    att[1].clearValue.depthStencil = { 1.0f, 0 };
    VkClearRect cr{ rect, 0, 1 };
    vkCmdClearAttachments(cmd, 2, att, 1, &cr);
}

void Renderer::beginRenderPass(bool split) {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    std::array<VkClearValue, 3> clearValues{}; // [2]: MSAA resolve target, not cleared
    clearValues[0].color = { { m_clearScene.r, m_clearScene.g, m_clearScene.b, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    ensureSceneTarget(m_currentFrame);
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = split ? m_scenePassSplit : m_scenePass;
    renderPassInfo.framebuffer = m_sceneTargets[m_currentFrame].framebuffer;
    VkExtent2D extent = renderExtent();
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = m_msaa > 1 ? 3u : 2u;
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_inScenePass = true;
    m_overlayOpen = false;
    setFullViewport(cmd, extent);
}

void Renderer::setSceneDepthReadable(bool readable) { m_depthReadable = readable; }

Renderer::SceneImages Renderer::sceneImages() const {
    const SceneTarget& t = m_sceneTargets[m_currentFrame];
    return SceneImages{ t.colorView, t.depthSampleView, t.extent, m_scenePassSamples };
}

Renderer::LastFrameDepth Renderer::lastFrameDepth() const {
    if (m_lastDepthFrame < 0 || static_cast<uint32_t>(m_lastDepthFrame) == m_currentFrame) return {};
    const SceneTarget& t = m_sceneTargets[m_lastDepthFrame];
    if (!t.depthSampleView) return {};
    return LastFrameDepth{ t.depthSampleView, t.extent, m_lastDepthSamples, m_motionBlurPrevViewProj };
}

const DepthMap* Renderer::occlusionMap() {
    if (!m_occlusionOn || !m_occlusionSingleView || !m_depthMap) return nullptr;
    if (!m_occlusionReadThisFrame) {
        m_occlusionRead = m_depthMap->read(m_currentFrame);
        m_occlusionReadThisFrame = true;
    }
    return m_occlusionRead;
}

void Renderer::endScenePass() {
    if (!m_inScenePass) return;
    m_inScenePass = false;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkCmdEndRenderPass(cmd);

    // The 3D image: attachment -> sampled, its writes (and the MSAA
    // resolve) finished and visible before the tone-map pass (and any
    // compute between) reads it. The depth too, when it is kept.
    const SceneTarget& t = m_sceneTargets[m_currentFrame];
    std::array<VkImageMemoryBarrier, 2> b{};
    b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[0].image = t.color;
    b[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b[1] = b[0];
    b[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    b[1].image = t.depth;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (m_swapChain->hasStencil()) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    b[1].subresourceRange = { aspect, 0, 1, 0, 1 };
    const bool depth = t.depthSampleView != VK_NULL_HANDLE;
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (depth) src |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    vkCmdPipelineBarrier(cmd, src, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         depth ? 2u : 1u, b.data());
}

void Renderer::pauseScenePass() { endScenePass(); }

void Renderer::resumeScenePass() {
    if (m_inScenePass) return;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    // Back to attachments, after whatever read them while paused (a fragment
    // or compute shader) is done; the multisampled colour never left.
    const SceneTarget& t = m_sceneTargets[m_currentFrame];
    std::array<VkImageMemoryBarrier, 2> b{};
    b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[0].srcAccessMask = 0;
    b[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[0].image = t.color;
    b[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b[1] = b[0];
    b[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    b[1].image = t.depth;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (m_swapChain->hasStencil()) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    b[1].subresourceRange = { aspect, 0, 1, 0, 1 };
    const bool depth = t.depthSampleView != VK_NULL_HANDLE; // only a readable depth left its attachment layout
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, depth ? 2u : 1u, b.data());
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = m_scenePassResume;
    info.framebuffer = t.framebuffer;
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = renderExtent();
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    m_inScenePass = true;
    setFullViewport(cmd, renderExtent());
}

void Renderer::setColorLut(std::shared_ptr<const ColorLut> lut) {
    if (lut == m_lut) return;
    // A frame in flight may still read the old table.
    vkDeviceWaitIdle(m_device->device());
    m_lut = std::move(lut);
    m_lutTexture = m_lut && m_lut->size >= 2 ? std::make_unique<ColorLutTexture>(*m_device, *m_lut) : nullptr;
}

void Renderer::beginOverlayPass() {
    if (!m_inScenePass && m_overlayOpen) {
        // Same pass continues: only undo a split-screen view's viewport.
        setFullViewport(m_commandBuffers[m_currentFrame], m_swapChain->extent());
        return;
    }
    endScenePass();
    m_overlayOpen = true;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    const SceneTarget& t = m_sceneTargets[m_currentFrame];
    m_postSource = t.colorView;
    m_postSourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (m_smaaOn) {
        // Anti-aliasing over the finished scene, before the tone map reads it (outside any pass).
        if (!m_smaa) m_smaa = std::make_unique<SmaaPass>(*m_device);
        m_smaa->record(cmd, m_currentFrame, t.colorView, t.extent);
        m_postSource = m_smaa->output();
        m_postSourceLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    const bool taa = m_taaOn && t.depthSampleView != VK_NULL_HANDLE;
    if (taa) {
        // Then the history of earlier frames, reprojected with last frame's camera.
        if (!m_taa) m_taa = std::make_unique<TaaPass>(*m_device);
        m_taa->record(cmd, m_currentFrame, m_postSource, m_postSourceLayout, t.depthSampleView, m_scenePassSamples, t.extent,
                      m_taaPrevViewProj * glm::inverse(m_taaViewProj), m_taaHavePrev);
        m_postSource = m_taa->output();
        m_postSourceLayout = VK_IMAGE_LAYOUT_GENERAL;
        m_taaPrevViewProj = m_taaViewProj;
    }
    m_taaHavePrev = taa;
    if (m_dofOn && t.depthSampleView != VK_NULL_HANDLE) {
        // Depth of field over the anti-aliased light, before bloom spreads it.
        if (!m_dof) m_dof = std::make_unique<DofPass>(*m_device);
        m_dof->settings() = m_dofSettings;
        m_dof->record(cmd, m_currentFrame, m_postSource, m_postSourceLayout, t.depthSampleView, m_scenePassSamples, t.extent, m_dofProj, m_dofDt);
        m_postSource = m_dof->output();
        m_postSourceLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    const bool blur = m_motionBlurOn && t.depthSampleView != VK_NULL_HANDLE;
    if (blur) {
        // Motion blur along each pixel's movement since last frame, before bloom spreads the light.
        if (!m_motionBlur) m_motionBlur = std::make_unique<MotionBlurPass>(*m_device);
        m_motionBlur->settings() = m_motionBlurSettings;
        m_motionBlur->record(cmd, m_currentFrame, m_postSource, m_postSourceLayout, t.depthSampleView, m_scenePassSamples, t.extent,
                             m_motionBlurPrevViewProj * glm::inverse(m_taaViewProj), m_dofProj, m_motionBlurHavePrev);
        m_postSource = m_motionBlur->output();
        m_postSourceLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    m_motionBlurPrevViewProj = m_taaViewProj;
    m_motionBlurHavePrev = blur;
    m_lastDepthFrame = t.depthSampleView ? static_cast<int>(m_currentFrame) : -1;
    if (m_occlusionOn && m_occlusionSingleView && t.depthSampleView) {
        // This frame's depth map for the CPU, two frames from now (occlusionMap()).
        if (!m_depthMap) m_depthMap = std::make_unique<DepthMapPass>(*m_device);
        m_depthMap->record(cmd, m_currentFrame, t.depthSampleView, m_scenePassSamples, t.extent, m_taaViewProj);
    } else if (m_depthMap) {
        m_depthMap->forget(); // off or split: an old map would hide things wrongly when it comes back
    }
    m_lastDepthSamples = m_scenePassSamples;
    m_bloomSource = VK_NULL_HANDLE;
    if (m_bloomOn) {
        // Bloom over the anti-aliased light, mixed in by the tone map.
        if (!m_bloom) m_bloom = std::make_unique<BloomPass>(*m_device);
        m_bloom->record(cmd, m_currentFrame, m_postSource, m_postSourceLayout, t.extent);
        m_bloomSource = m_bloom->output();
    }

    // The swapchain's own pass: it clears the image (the acquire semaphore
    // is waited on at COLOR_ATTACHMENT_OUTPUT, which its dependency chains
    // after), and the tone map covers every pixel before the UI draws.
    std::array<VkClearValue, 2> clearValues{};
    clearValues[1].depthStencil = { 1.0f, 0 };
    const VkExtent2D full = m_swapChain->extent();
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = m_swapChain->renderPass();
    info.framebuffer = m_swapChain->framebuffer(m_currentImageIndex);
    info.renderArea = { { 0, 0 }, full };
    info.clearValueCount = static_cast<uint32_t>(clearValues.size());
    info.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    setFullViewport(cmd, full);
    drawPostPass(cmd);
}

void Renderer::createPostPass() {
    VkDevice dev = m_device->device();
    // Linear filtering upscales a scene drawn at a render scale below 1;
    // at scale 1 every pixel samples its own texel centre.
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_postSampler));

    // Binding 0: the scene light; 1: bloom; 2: the colour LUT (ENG-F10).
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; ++i) bindings[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(bindings.size());
    li.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_postSetLayout));

    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * kMaxFramesInFlight };
    m_identityLut = std::make_unique<ColorLutTexture>(*m_device, ColorLut::identity(2));
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = kMaxFramesInFlight;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_postPool));
    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts;
    layouts.fill(m_postSetLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_postPool;
    ai.descriptorSetCount = kMaxFramesInFlight;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, m_postSets));
}

void Renderer::destroyPostPass() {
    VkDevice dev = m_device->device();
    m_postPipeline.reset();
    if (m_postPool) vkDestroyDescriptorPool(dev, m_postPool, nullptr);
    if (m_postSetLayout) vkDestroyDescriptorSetLayout(dev, m_postSetLayout, nullptr);
    if (m_postSampler) vkDestroySampler(dev, m_postSampler, nullptr);
    m_postPool = VK_NULL_HANDLE;
    m_postSetLayout = VK_NULL_HANDLE;
    m_postSampler = VK_NULL_HANDLE;
}

// One full-screen triangle: this frame's scene image through the tone
// map, exposure and look (shaders/tonemap_post.frag), into the swapchain.
void Renderer::drawPostPass(VkCommandBuffer cmd) {
    if (!m_postPipeline || m_postFormat != m_swapChain->imageFormat()) {
        // The old pipeline may still be used by a frame in flight.
        if (m_postPipeline) retire(std::move(m_postPipeline));
        PipelineConfig config;
        config.useVertexInput = false;
        config.cullMode = rhi::CullMode::None;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.pushConstantRange = { rhi::kFragment, 0, sizeof(PostParams) };
        config.descriptorSetLayouts = { m_postSetLayout };
        m_postPipeline = std::make_unique<Pipeline>(*m_device, m_swapChain->renderPass(), "shaders/rml_fullscreen.vert.spv",
                                                    "shaders/tonemap_post.frag.spv", config);
        m_postFormat = m_swapChain->imageFormat();
    }
    // This slot's previous frame has finished (beginFrame waited on its
    // fence), so its set can point at this frame's scene image.
    const ColorLutTexture& lut = m_lutTexture ? *m_lutTexture : *m_identityLut;
    const std::array<VkDescriptorImageInfo, 3> images{ { { m_postSampler, m_postSource, m_postSourceLayout },
                                                         m_bloomSource ? VkDescriptorImageInfo{ m_postSampler, m_bloomSource, VK_IMAGE_LAYOUT_GENERAL }
                                                                       : VkDescriptorImageInfo{ m_postSampler, m_postSource, m_postSourceLayout },
                                                         { lut.sampler(), lut.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } } };
    std::array<VkWriteDescriptorSet, 3> w{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = m_postSets[m_currentFrame];
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_device->device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    m_postPipeline->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeline->layout(), 0, 1, &m_postSets[m_currentFrame], 0, nullptr);
    PostParams post = m_post;
    post.tone.w = m_bloomSource && m_bloom ? m_bloom->settings().strength : 0.0f;
    post.lookPower.a = m_lutTexture ? static_cast<float>(m_lutTexture->size()) : 0.0f;
    post.tone.z = m_swapChain->encodesSrgb() ? 0.0f : 1.0f; // a UNORM (10-bit) swapchain: the shader encodes sRGB
    vkCmdPushConstants(cmd, m_postPipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostParams), &post);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Renderer::endFrame() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    // A frame with nothing drawn over the 3D image still needs it tone mapped.
    beginOverlayPass();
    vkCmdEndRenderPass(cmd);
    if (!m_captureRequests.empty()) recordCapture(cmd);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampPools[m_currentFrame], 1);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSemaphore waitSemaphores[] = { m_imageAvailable[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_renderFinished[m_currentFrame] };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = { m_swapChain->handle() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_device->presentQueue(), &presentInfo);

    // While the compositor turns the image, SUBOPTIMAL comes every frame;
    // only a turn of the screen since the swapchain was made needs a new one.
    const bool suboptimal = result == VK_SUBOPTIMAL_KHR && (!m_swapChain->compositorRotates() || m_swapChain->screenTurned());
    if (result == VK_ERROR_OUT_OF_DATE_KHR || suboptimal || m_window.wasResized()) {
        m_window.clearResizedFlag();
        recreateSwapChain();
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        // SUBOPTIMAL while the compositor rotates (a landscape game on a
        // portrait phone) is expected every frame: the image was shown.
        throw std::runtime_error("failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

} // namespace kke
