#pragma once

#include "kke/Window.h"
#include "kke/Renderer.h"
#include "kke/DebugUi.h"
#include "kke/Module.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
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

// Owns the window, renderer, and debug UI; resolves module dependency
// order; drives the Module lifecycle (including a fixed-timestep tick for
// deterministic simulation); runs the main loop.
//
// Cross-module communication has two tiers, deliberately kept separate:
//
//   - getModule<T>()      — "give me the one Concrete module of type T,
//                            if it exists." Use when a module genuinely
//                            needs a specific other module and would be
//                            broken without it (declare it via
//                            dependencies() too, so init order is right
//                            and a missing required one fails loudly).
//
//   - findCapability<T>() — "give me every module that implements
//                            interface T, whatever they are." Use for
//                            loose, optional cross-talk — the "recognize
//                            each other if both present, fine alone if
//                            not" case (see kke/Capabilities.h).
//
// See the README's "Cross-module communication" section for the worked
// networking/destruction example.
class Application {
public:
    Application(const std::string& title, uint32_t width, uint32_t height, float fixedUpdateHz = 60.0f);
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

    // Typed lookup by concrete module type. Returns nullptr if that
    // module wasn't added to this Application — always check.
    template <typename T>
    T* getModule() {
        auto it = m_moduleByType.find(std::type_index(typeid(T)));
        if (it == m_moduleByType.end()) return nullptr;
        return dynamic_cast<T*>(it->second);
    }

    // Every module (whatever its concrete type) that implements interface
    // Capability. Safe to call with zero results — that's the "this
    // module isn't present, so nobody asks for my state" case working as
    // intended, not an error.
    template <typename Capability>
    std::vector<Capability*> findCapability() {
        std::vector<Capability*> result;
        for (auto& m : m_modules) {
            if (auto* c = dynamic_cast<Capability*>(m.get())) {
                result.push_back(c);
            }
        }
        return result;
    }

    void run();

    Window& window() { return m_window; }
    Renderer& renderer() { return *m_renderer; }
    VulkanDevice& device() { return m_renderer->device(); }
    Camera& camera() { return m_camera; }

private:
    void resolveInitOrder();

    Window m_window;
    std::unique_ptr<Renderer> m_renderer; // created after window, needs it for the surface
    std::unique_ptr<DebugUi> m_debugUi;
    Camera m_camera;
    float m_fixedDt;

    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<Module*> m_initOrder; // m_modules reordered so dependencies come first
    std::unordered_map<std::type_index, Module*> m_moduleByType;
};

} // namespace kke
