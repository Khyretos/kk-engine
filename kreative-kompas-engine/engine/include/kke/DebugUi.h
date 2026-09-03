#pragma once

#include <volk.h>
#include <SDL3/SDL.h>
#include <cstdint>

namespace kke {

class Window;
class VulkanDevice;

// Wraps Dear ImGui's SDL3 + Vulkan backends. Nothing outside this file
// touches an ImGui header — same rule as Window.h hiding SDL3, so ImGui
// stays swappable/removable as its own lego piece.
class DebugUi {
public:
    DebugUi(Window& window, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    // Call once per frame before building any ImGui:: UI calls.
    void beginFrame();

    // Forwards a raw SDL event into ImGui (mouse/keyboard/etc). Call this
    // from Window::pollEvents()'s callback so main.cpp never has to touch
    // an ImGui header itself.
    void processEvent(const SDL_Event& event);

    // Records ImGui's draw data into cmd. Must be called while a render
    // pass compatible with the one passed to the constructor is active,
    // and before vkCmdEndRenderPass.
    void render(VkCommandBuffer cmd);

private:
    VulkanDevice& m_device;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};

} // namespace kke
