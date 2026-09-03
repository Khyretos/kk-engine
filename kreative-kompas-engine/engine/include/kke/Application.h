#pragma once

#include "kke/Window.h"
#include "kke/Renderer.h"
#include "kke/DebugUi.h"
#include "kke/Module.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace kke {

// A basic orbit-style camera. Not a module itself — nearly every module
// wants to read the camera, so it lives on Application directly rather
// than behind another lookup.
struct Camera {
    glm::vec3 position{2.0f, 2.0f, 2.5f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

// Owns the window, renderer, and debug UI; drives the Module lifecycle;
// runs the main loop. This is the "simple game loop" piece — main.cpp for
// a real game is expected to look like:
//
//   kke::Application app("My Game", 1280, 720);
//   app.addModule<MyGameplayModule>();
//   app.addModule<kke::StatsModule>();
//   app.run();
//
// See the README's "Adding a module" section for how to write MyGameplayModule.
class Application {
public:
    Application(const std::string& title, uint32_t width, uint32_t height);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    template <typename T, typename... Args>
    T& addModule(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *module;
        m_modules.push_back(std::move(module));
        return ref;
    }

    void run();

    Window& window() { return m_window; }
    Renderer& renderer() { return *m_renderer; }
    VulkanDevice& device() { return m_renderer->device(); }
    Camera& camera() { return m_camera; }

private:
    Window m_window;
    std::unique_ptr<Renderer> m_renderer; // created after window, needs it for the surface
    std::unique_ptr<DebugUi> m_debugUi;
    Camera m_camera;
    std::vector<std::unique_ptr<Module>> m_modules;
};

} // namespace kke
