#pragma once

#include "kke/Window.h"
#include "kke/Renderer.h"
#include "kke/DebugUi.h"
#include "kke/Module.h"
#include "kke/EngineError.h"
#include "kke/LightingBuffer.h"

#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
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

// One light — either directional (uses `direction`, ignores `position`)
// or a point light (uses `position`, ignores `direction`). Kept as one
// struct with a type flag rather than two separate types: the GPU-side
// UBO this feeds needs a fixed-layout array either way, and a shared
// CPU-side struct keeps LightingBuffer's upload code simple (see
// LightingBuffer.h/.cpp — Renderer owns one, updated once per frame
// from whatever's in Application::lighting() when render() runs).
struct Light {
    bool enabled = false;
    bool isDirectional = true;
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // meaningful only if isDirectional
    glm::vec3 position{0.0f};               // meaningful only if !isDirectional
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// Same reasoning as Camera just above: nearly every module that draws
// real geometry wants to read the current lights, so this lives on
// Application directly rather than behind a getModule<>() lookup.
// kMaxLights is a real, fixed limit (not "as many as you want") because
// the GPU-side UBO this feeds has a fixed-size array — see
// LightingBuffer.h for exactly how that's laid out and why 4 was
// chosen (a small, genuinely useful number for a first real multi-
// light slice, not an arbitrary round number).
struct Lighting {
    static constexpr int kMaxLights = 4;
    std::array<Light, kMaxLights> lights;
    glm::vec3 ambientColor{0.15f, 0.15f, 0.15f}; // flat fill light so unlit faces read as dim, not pure black

    Lighting() {
        // A sensible default so a demo that never touches lighting at
        // all still looks like the single-light version this replaced,
        // not suddenly pitch black — matches this project's own
        // "verify no regression" discipline rather than assuming a
        // silent behavior change is fine.
        lights[0].enabled = true;
        lights[0].isDirectional = true;
        lights[0].direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        lights[0].color = glm::vec3(1.0f, 0.98f, 0.92f);
        lights[0].intensity = 1.0f;
    }
};

// Recorded when a module throws during any lifecycle call — see
// Application::run()'s per-module try/catch and "Debugging" in the
// README for the full reasoning. Kept in call order, oldest first.
//
// friendlyMessage and technicalMessage are ALWAYS both populated,
// regardless of what was actually thrown: a module that throws a plain
// std::exception gets its what() text copied into both (the only text
// available), while a module that throws kke::EngineError (see
// EngineError.h) gets the real plain-language/technical split, plus
// source/file/line when known. DebugControlModule's Emergency Log shows
// friendlyMessage prominently and technicalMessage as a secondary
// detail — designed so a non-programmer gets something actionable
// without the technical text being hidden from someone who wants it.
struct BrokenModuleInfo {
    std::string moduleName;
    std::string stage;             // which lifecycle method threw, e.g. "update", "render"
    std::string friendlyMessage;   // plain language — the thing to show a scripter first
    std::string technicalMessage;  // e.what() — always available, shown as a secondary detail
    ErrorSource source = ErrorSource::Unknown;
    std::string file;              // empty if unknown
    int line = 0;                  // 0 if unknown
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
    Lighting& lighting() { return m_lighting; }
    LightingBuffer& lightingBuffer() { return *m_lightingBuffer; }

    // --- Debug pause/step ---
    // Freezes simulation (fixedUpdate/update stop advancing) while still
    // rendering every frame, so the same, stable frame keeps presenting
    // for as long as you like — the point being to give an external
    // capture tool (RenderDoc, a Vulkan profiling layer, or just staring
    // at validation-layer output) a frame that isn't changing out from
    // under you. Rendering itself is never paused: a frozen frame still
    // needs to actually get drawn and presented to be inspectable at all.
    bool isPaused() const { return m_paused; }
    void setPaused(bool paused) { m_paused = paused; }
    // Advances the simulation by exactly one fixed tick + one update()
    // call, then re-freezes — for stepping through frames one at a time
    // while paused. A no-op if not currently paused.
    void stepOneFrame() { if (m_paused) m_stepRequested = true; }

    // --- Per-module error isolation ---
    // See run()'s per-module try/catch: a module that throws during any
    // lifecycle call gets recorded here and is never called again for
    // the rest of this session (not even shutdown() — see run()'s
    // comment on that tradeoff). Everything else keeps running.
    const std::vector<BrokenModuleInfo>& brokenModules() const { return m_brokenModuleInfos; }

private:
    void resolveInitOrder();
    void safeInvoke(Module* m, const char* stage, const std::function<void()>& fn);

    Window m_window;
    std::unique_ptr<Renderer> m_renderer; // created after window, needs it for the surface
    std::unique_ptr<DebugUi> m_debugUi;
    Camera m_camera;
    Lighting m_lighting;
    std::unique_ptr<LightingBuffer> m_lightingBuffer;
    float m_fixedDt;

    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<Module*> m_initOrder; // m_modules reordered so dependencies come first
    std::unordered_map<std::type_index, Module*> m_moduleByType;

    bool m_paused = false;
    bool m_stepRequested = false;

    std::unordered_set<Module*> m_faultedModules; // see safeInvoke() — never called again once here
    std::vector<BrokenModuleInfo> m_brokenModuleInfos;
};

} // namespace kke
