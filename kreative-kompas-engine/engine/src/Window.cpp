#include "kke/Window.h"
#include "kke/VulkanCheck.h"

#include <stdexcept>

namespace kke {

Window::Window(const std::string& title, uint32_t width, uint32_t height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    m_window = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!m_window) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
    }
    SDL_Quit();
}

std::vector<const char*> Window::getRequiredInstanceExtensions() const {
    Uint32 count = 0;
    char const* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    return std::vector<const char*>(extensions, extensions + count);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    return surface;
}

bool Window::pollEvents(const EventCallback& onEvent) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (onEvent) onEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_shouldClose = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m_resized = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    m_shouldClose = true;
                }
                break;
            default:
                break;
        }
    }
    return !m_shouldClose;
}

void Window::getFramebufferSize(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
}

} // namespace kke
