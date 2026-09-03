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

    VkSwapchainKHR handle() const { return m_swapChain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkFormat depthFormat() const { return m_depthFormat; }
    VkExtent2D extent() const { return m_extent; }
    VkRenderPass renderPass() const { return m_renderPass; }
    VkFramebuffer framebuffer(uint32_t index) const { return m_framebuffers[index]; }
    size_t imageCount() const { return m_images.size(); }

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

    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthImageAllocation = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    VkFormat m_imageFormat{};
    VkExtent2D m_extent{};
};

} // namespace kke
