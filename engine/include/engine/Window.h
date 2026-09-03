#pragma once

#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

namespace engine {

// Owns the OS window and knows how to hand Vulkan a VkSurfaceKHR.
// This is deliberately the only piece of the engine that talks to SDL for
// windowing — everything downstream just sees Vulkan handles.
class Window {
public:
    Window(const std::string& title, uint32_t width, uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Required instance extensions for creating a surface on this platform.
    std::vector<const char*> getRequiredInstanceExtensions() const;

    VkSurfaceKHR createSurface(VkInstance instance) const;

    // Pumps SDL events. Returns false once the user has requested to quit.
    bool pollEvents();

    void getFramebufferSize(int& width, int& height) const;
    bool wasResized() const { return m_resized; }
    void clearResizedFlag() { m_resized = false; }

    SDL_Window* handle() const { return m_window; }

private:
    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;
    bool m_resized = false;
};

} // namespace engine
