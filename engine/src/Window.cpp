#include "kke/Window.h"
#include "kke/VulkanCheck.h"

#include <stb_image.h>

#include <cstddef>
#include <stdexcept>

namespace kke {

// assets/branding/logo-128.png, embedded by engine/CMakeLists.txt.
extern const unsigned char kEngineIconPng[];
extern const std::size_t kEngineIconPngSize;

Window::Window(const std::string& title, uint32_t width, uint32_t height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    m_window = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!m_window) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    setIconFromPng(kEngineIconPng, kEngineIconPngSize);
}

bool Window::setIconFromPng(const unsigned char* png, size_t size) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(png, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) return false;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    // Some platforms (Wayland without the xdg-toplevel-icon protocol,
    // macOS, which takes the icon from the app bundle) have no per-window
    // icon; SDL reports that as a failure, which is harmless here.
    const bool ok = surface && SDL_SetWindowIcon(m_window, surface);
    if (surface) SDL_DestroySurface(surface);
    stbi_image_free(pixels);
    return ok;
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
    // Deltas are per-frame — reset them here, then accumulate below as
    // events for this frame come in.
    m_mouseState.deltaX = 0.0f;
    m_mouseState.deltaY = 0.0f;
    m_mouseState.scrollDelta = 0.0f;

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
                if (event.key.key == SDLK_ESCAPE && m_quitOnEscape) {
                    m_shouldClose = true;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                m_mouseState.deltaX += event.motion.xrel;
                m_mouseState.deltaY += event.motion.yrel;
                m_mouseState.x = event.motion.x;
                m_mouseState.y = event.motion.y;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                m_mouseState.scrollDelta += event.wheel.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                if (event.button.button == SDL_BUTTON_LEFT) m_mouseState.leftButtonDown = down;
                if (event.button.button == SDL_BUTTON_RIGHT) m_mouseState.rightButtonDown = down;
                if (event.button.button == SDL_BUTTON_MIDDLE) m_mouseState.middleButtonDown = down;
                m_mouseState.x = event.button.x;
                m_mouseState.y = event.button.y;
                break;
            }
            default:
                break;
        }
    }
    return !m_shouldClose;
}

void Window::getFramebufferSize(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
}

void Window::setFullscreen(bool fullscreen) {
    SDL_SetWindowFullscreen(m_window, fullscreen);
}

bool Window::isFullscreen() const {
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

float Window::pixelsPerPoint() const {
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
    return (w > 0 && pw > 0) ? static_cast<float>(pw) / static_cast<float>(w) : 1.0f;
}

} // namespace kke
