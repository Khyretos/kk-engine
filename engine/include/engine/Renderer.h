#pragma once

#include "engine/VulkanDevice.h"
#include "engine/SwapChain.h"
#include "engine/Pipeline.h"
#include "engine/Mesh.h"

#include <memory>
#include <vector>

namespace engine {

class Window;

// Ties the whole "lego set" together for one frame: acquire image, record
// command buffer, submit, present. Gameplay code above this just calls
// beginFrame()/drawMesh()/endFrame() and doesn't see any Vulkan handles.
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns false if the frame was skipped (e.g. swapchain out of date).
    bool beginFrame();
    void drawMesh(const Mesh& mesh, const glm::mat4& mvp);
    void endFrame();

    VulkanDevice& device() { return *m_device; }

private:
    static constexpr int kMaxFramesInFlight = 2;

    void createSyncObjects();
    void createCommandBuffers();
    void recreateSwapChain();

    Window& m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<SwapChain> m_swapChain;
    std::unique_ptr<Pipeline> m_pipeline;

    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_inFlightFences;

    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    bool m_frameActive = false;
};

} // namespace engine
