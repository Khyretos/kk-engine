#include "kke/Renderer.h"

#include <cmath>
#include "kke/Window.h"
#include "kke/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

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
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(m_device->device());
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) destroySceneTarget(i);
    if (m_scenePass) vkDestroyRenderPass(m_device->device(), m_scenePass, nullptr);
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

void Renderer::setClearColor(const glm::vec3& c) {
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    m_clearLinear = glm::vec3(lin(c.r), lin(c.g), lin(c.b));
}

void Renderer::setRenderScale(float scale) {
    scale = std::clamp(scale, 0.5f, 1.0f);
    if (scale < 1.0f && !m_swapChain->canBlitTo()) {
        std::cerr << "[renderer] render scale unavailable: the swapchain can't be blitted to; staying at 1.0" << std::endl;
        scale = 1.0f;
    }
    m_renderScale = scale;
}

VkExtent2D Renderer::renderExtent() const {
    VkExtent2D e = m_swapChain->extent();
    if (!scaled()) return e;
    return { std::max(1u, static_cast<uint32_t>(std::lround(e.width * m_renderScale))),
             std::max(1u, static_cast<uint32_t>(std::lround(e.height * m_renderScale))) };
}

// Same attachments as the swapchain's pass (so the same pipelines draw
// in it), but the colour ends as a blit source.
void Renderer::createScenePass() {
    VkFormat color = m_swapChain->imageFormat(), depth = m_swapChain->depthFormat();
    if (m_scenePass && m_scenePassFormats[0] == color && m_scenePassFormats[1] == depth) return;
    if (m_scenePass) vkDestroyRenderPass(m_device->device(), m_scenePass, nullptr);
    m_scenePassFormats[0] = color;
    m_scenePassFormats[1] = depth;

    std::array<VkAttachmentDescription, 2> a{};
    a[0].format = color;
    a[0].samples = VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    a[1].format = depth;
    a[1].samples = VK_SAMPLE_COUNT_1_BIT;
    a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].stencilLoadOp = m_swapChain->hasStencil() ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Identical to SwapChain's own dependency: render passes are only
    // compatible (share pipelines) when their dependencies match. The
    // hand-off to the blit is a barrier in beginOverlayPass() instead.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(a.size());
    info.pAttachments = a.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(m_device->device(), &info, nullptr, &m_scenePass));
    // Formats changed: every target was made for the old pass.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) m_sceneTargets[i].extent = {};
}

void Renderer::destroySceneTarget(uint32_t frame) {
    SceneTarget& t = m_sceneTargets[frame];
    VkDevice dev = m_device->device();
    if (t.framebuffer) vkDestroyFramebuffer(dev, t.framebuffer, nullptr);
    if (t.colorView) vkDestroyImageView(dev, t.colorView, nullptr);
    if (t.depthView) vkDestroyImageView(dev, t.depthView, nullptr);
    if (t.color) vmaDestroyImage(m_device->allocator(), t.color, t.colorAlloc);
    if (t.depth) vmaDestroyImage(m_device->allocator(), t.depth, t.depthAlloc);
    t = SceneTarget{};
}

// Only this frame slot's target is touched, and beginFrame() already
// waited on its fence, so nothing on the GPU still uses it.
void Renderer::ensureSceneTarget(uint32_t frame) {
    createScenePass();
    VkExtent2D e = renderExtent();
    SceneTarget& t = m_sceneTargets[frame];
    if (t.framebuffer && t.extent.width == e.width && t.extent.height == e.height) return;
    destroySceneTarget(frame);
    t.extent = e;

    auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image,
                         VmaAllocation& alloc, VkImageView& view) {
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
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
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
    makeImage(m_scenePassFormats[0], VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, t.color, t.colorAlloc, t.colorView);
    makeImage(m_scenePassFormats[1], VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT | (m_swapChain->hasStencil() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0), t.depth,
              t.depthAlloc, t.depthView);

    std::array<VkImageView, 2> views = { t.colorView, t.depthView };
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

void Renderer::beginRenderPass() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { m_clearLinear.r, m_clearLinear.g, m_clearLinear.b, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    if (scaled()) {
        ensureSceneTarget(m_currentFrame);
        renderPassInfo.renderPass = m_scenePass;
        renderPassInfo.framebuffer = m_sceneTargets[m_currentFrame].framebuffer;
    } else {
        renderPassInfo.renderPass = m_swapChain->renderPass();
        renderPassInfo.framebuffer = m_swapChain->framebuffer(m_currentImageIndex);
    }
    VkExtent2D extent = renderExtent();
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_inScenePass = scaled();
    setFullViewport(cmd, extent);
}

void Renderer::beginOverlayPass() {
    if (!m_inScenePass) return;
    m_inScenePass = false;
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkCmdEndRenderPass(cmd);

    const SceneTarget& src = m_sceneTargets[m_currentFrame];
    VkImage target = m_swapChain->image(m_currentImageIndex);
    auto barrier = [&](VkImage image, VkImageLayout layout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        b.oldLayout = layout;
        b.newLayout = layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    // The 3D image (already TRANSFER_SRC from the pass) is written before it's read.
    barrier(src.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Swapchain image: whatever it held -> blit target. The acquire
    // semaphore is waited on at COLOR_ATTACHMENT_OUTPUT, so the barrier
    // starts from that stage to chain after it.
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = target;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toDst);

    VkExtent2D full = m_swapChain->extent();
    VkImageBlit blit{};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[1] = { static_cast<int32_t>(src.extent.width), static_cast<int32_t>(src.extent.height), 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1] = { static_cast<int32_t>(full.width), static_cast<int32_t>(full.height), 1 };
    vkCmdBlitImage(cmd, src.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);
    // The overlay pass loads (reads) and draws over what the blit wrote.
    barrier(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = m_swapChain->overlayPass();
    info.framebuffer = m_swapChain->framebuffer(m_currentImageIndex);
    info.renderArea = { { 0, 0 }, full };
    info.clearValueCount = static_cast<uint32_t>(clearValues.size());
    info.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    setFullViewport(cmd, full);
}

void Renderer::endFrame() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    // A frame with nothing drawn over the 3D image still needs it upscaled.
    beginOverlayPass();
    vkCmdEndRenderPass(cmd);
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

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasResized()) {
        m_window.clearResizedFlag();
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

} // namespace kke
