#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <optional>
#include <cstdint>

namespace engine {

class Window;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool isComplete() const { return graphics.has_value() && present.has_value(); }
};

// Wraps VkInstance + VkPhysicalDevice + VkDevice + VMA allocator.
// One "lego piece": everything the rest of the renderer needs to talk to the GPU.
class VulkanDevice {
public:
    VulkanDevice(Window& window, bool enableValidation);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkSurfaceKHR surface() const { return m_surface; }
    VmaAllocator allocator() const { return m_allocator; }

    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    const QueueFamilyIndices& queueFamilies() const { return m_queueFamilies; }

    VkCommandPool commandPool() const { return m_commandPool; }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

private:
    void createInstance(bool enableValidation);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice(bool enableValidation);
    void createAllocator();
    void createCommandPool();

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    bool isDeviceSuitable(VkPhysicalDevice device) const;

    Window& m_window;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    bool m_validationEnabled = false;
};

} // namespace engine
