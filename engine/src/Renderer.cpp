#include "kke/Renderer.h"
#include "kke/Window.h"
#include "kke/VulkanCheck.h"

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

bool Renderer::beginFrame() {
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

void Renderer::beginRenderPass() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { 0.02f, 0.02f, 0.05f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_swapChain->renderPass();
    renderPassInfo.framebuffer = m_swapChain->framebuffer(m_currentImageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapChain->extent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChain->extent().width);
    viewport.height = static_cast<float>(m_swapChain->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, m_swapChain->extent() };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Renderer::endFrame() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
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
