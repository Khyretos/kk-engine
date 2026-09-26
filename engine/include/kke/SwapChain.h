#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

namespace kke {

class VulkanDevice;
class Window;

class SwapChain {
public:
    SwapChain(VulkanDevice& device, Window& window);
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    void recreate();

    // true: FIFO (waits for the display's refresh — no tearing, capped
    // frame rate, least power). false: MAILBOX (no tearing, uncapped),
    // falling back to IMMEDIATE, then FIFO if neither is supported.
    // Takes effect on the next recreate().
    void setVSync(bool vsync) { m_vsync = vsync; }
    bool vsync() const { return m_vsync; }

    VkSwapchainKHR handle() const { return m_swapChain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkFormat depthFormat() const { return m_depthFormat; }
    VkExtent2D extent() const { return m_extent; }
    VkRenderPass renderPass() const { return m_renderPass; }
    // Compatible with renderPass(), but loads the colour instead of
    // clearing it (starts in TRANSFER_DST layout, after a blit).
    VkRenderPass overlayPass() const { return m_overlayPass; }
    VkImage image(uint32_t index) const { return m_images[index]; }
    // Swapchain images accept linear blits (render scale works).
    bool canBlitTo() const { return m_canBlitTo; }
    VkFramebuffer framebuffer(uint32_t index) const { return m_framebuffers[index]; }
    size_t imageCount() const { return m_images.size(); }
    bool hasStencil() const { return m_hasStencil; }

private:
    void create();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void cleanup();

    VulkanDevice& m_device;
    Window& m_window;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkRenderPass m_overlayPass = VK_NULL_HANDLE;
    bool m_canBlitTo = false;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthImageAllocation = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    bool m_hasStencil = false;

    bool m_vsync = false; // matches this engine's behavior before the setting existed
    VkFormat m_imageFormat{};
    VkExtent2D m_extent{};
};

} // namespace kke
