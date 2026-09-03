#include "kke/Application.h"
#include "kke/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <queue>
#include <stdexcept>
#include <string>

namespace kke {

Application::Application(const std::string& title, uint32_t width, uint32_t height, float fixedUpdateHz)
    : m_window(title, width, height), m_fixedDt(1.0f / fixedUpdateHz) {
    log::init(title);
    m_renderer = std::make_unique<Renderer>(m_window);
    m_debugUi = std::make_unique<DebugUi>(m_window, m_renderer->device(), m_renderer->renderPass(),
                                           m_renderer->swapChainImageCount());
}

Application::~Application() {
    // Shut down in reverse dependency order: a module that depends on
    // another should tear itself down first, while what it depends on is
    // still alive to be torn down safely after.
    for (auto it = m_initOrder.rbegin(); it != m_initOrder.rend(); ++it) {
        (*it)->shutdown();
    }
    log::shutdown(); // flush the async queue before the process exits
}

void Application::resolveInitOrder() {
    m_moduleByType.clear();
    for (auto& m : m_modules) {
        // typeid(*m) resolves through the vtable to the concrete type, so
        // this map key is the module's real type regardless of how it's
        // stored (unique_ptr<Module>).
        m_moduleByType[std::type_index(typeid(*m))] = m.get();
    }

    std::unordered_map<Module*, std::vector<Module*>> dependents; // dependency -> [modules that need it]
    std::unordered_map<Module*, int> inDegree;
    for (auto& m : m_modules) inDegree[m.get()] = 0;

    for (auto& m : m_modules) {
        for (const auto& dep : m->dependencies()) {
            auto it = m_moduleByType.find(dep.type);
            if (it == m_moduleByType.end()) {
                if (dep.required) {
                    throw std::runtime_error(
                        std::string("Module '") + m->name() + "' requires a module of type '" +
                        dep.type.name() + "'" +
                        (std::string(dep.reason).empty() ? "" : (std::string(" (") + dep.reason + ")")) +
                        ", but it wasn't added to the Application.");
                }
                continue; // optional and absent — fine, the module must handle getModule<T>()==nullptr itself
            }
            dependents[it->second].push_back(m.get());
            inDegree[m.get()]++;
        }
    }

    // Kahn's algorithm: dependencies before dependents.
    std::queue<Module*> ready;
    for (auto& m : m_modules) {
        if (inDegree[m.get()] == 0) ready.push(m.get());
    }

    m_initOrder.clear();
    while (!ready.empty()) {
        Module* n = ready.front();
        ready.pop();
        m_initOrder.push_back(n);
        for (Module* dependent : dependents[n]) {
            if (--inDegree[dependent] == 0) ready.push(dependent);
        }
    }

    if (m_initOrder.size() != m_modules.size()) {
        throw std::runtime_error("Circular module dependency detected — check dependencies() on your modules.");
    }
}

void Application::run() {
    resolveInitOrder();
    for (Module* m : m_initOrder) {
        m->init(*this);
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrameTime = startTime;
    float accumulator = 0.0f;
    uint64_t tickIndex = 0;

    while (m_window.pollEvents([this](const SDL_Event& e) {
        m_debugUi->processEvent(e);
        for (Module* m : m_initOrder) {
            m->onEvent(e);
        }
    })) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        lastFrameTime = now;

        // Cap how much simulation time a single slow frame can inject —
        // otherwise a long stall (asset load, breakpoint) causes a burst
        // of catch-up fixed ticks that then takes even longer, stalling
        // further: the classic "spiral of death."
        accumulator = std::min(accumulator + dt, m_fixedDt * 8.0f);

        while (accumulator >= m_fixedDt) {
            tickIndex++;
            FixedUpdateContext fixedCtx{ m_fixedDt, tickIndex };
            for (Module* m : m_initOrder) {
                m->fixedUpdate(fixedCtx);
            }
            accumulator -= m_fixedDt;
        }

        UpdateContext updateCtx{ dt, totalTime };
        for (Module* m : m_initOrder) {
            m->update(updateCtx);
        }

        glm::mat4 view = glm::lookAt(m_camera.position, m_camera.target, m_camera.up);
        glm::mat4 proj = glm::perspective(glm::radians(m_camera.fovDegrees), m_renderer->aspectRatio(),
                                           m_camera.nearPlane, m_camera.farPlane);
        proj[1][1] *= -1.0f; // Vulkan's clip space Y is flipped relative to GLM's assumption.

        RenderContext renderCtx{};
        renderCtx.view = view;
        renderCtx.proj = proj;
        renderCtx.cameraPos = m_camera.position;
        renderCtx.aspectRatio = m_renderer->aspectRatio();
        renderCtx.renderPass = m_renderer->renderPass();

        m_debugUi->beginFrame();
        for (Module* m : m_initOrder) {
            m->renderUi();
        }

        if (m_renderer->beginFrame()) {
            VkCommandBuffer cmd = m_renderer->currentCommandBuffer();

            for (Module* m : m_initOrder) {
                m->compute(cmd);
            }

            m_renderer->beginRenderPass();
            renderCtx.cmd = cmd;
            for (Module* m : m_initOrder) {
                m->render(renderCtx);
            }
            m_debugUi->render(cmd);

            m_renderer->endFrame();
        }
    }
}

} // namespace kke
