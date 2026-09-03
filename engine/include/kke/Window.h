#pragma once

#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace kke {

class Window {
public:
    using EventCallback = std::function<void(const SDL_Event&)>;

    // Per-frame mouse state. Deltas are "since the last pollEvents() call",
    // not since button-down — accumulate them yourself (see
    // OrbitCameraModule) if you need total drag distance.
    struct MouseState {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        float scrollDelta = 0.0f;
        bool leftButtonDown = false;
        bool rightButtonDown = false;
    };

    Window(const std::string& title, uint32_t width, uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Required instance extensions for creating a surface on this platform.
    std::vector<const char*> getRequiredInstanceExtensions() const;

    VkSurfaceKHR createSurface(VkInstance instance) const;

    // Pumps SDL events. Returns false once the user has requested to quit.
    // If set, onEvent is invoked with every raw SDL_Event before this class
    // interprets it — lets callers (e.g. a debug UI) see events without
    // Window needing to know they exist.
    bool pollEvents(const EventCallback& onEvent = nullptr);

    void getFramebufferSize(int& width, int& height) const;
    bool wasResized() const { return m_resized; }
    void clearResizedFlag() { m_resized = false; }

    // Valid for the frame between this pollEvents() call and the next.
    const MouseState& mouseState() const { return m_mouseState; }

    SDL_Window* handle() const { return m_window; }

private:
    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;
    bool m_resized = false;
    MouseState m_mouseState;
};

} // namespace kke
