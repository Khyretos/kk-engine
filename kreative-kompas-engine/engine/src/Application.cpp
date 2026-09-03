#include "kke/Application.h"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

namespace kke {

Application::Application(const std::string& title, uint32_t width, uint32_t height)
    : m_window(title, width, height) {
    m_renderer = std::make_unique<Renderer>(m_window);
    m_debugUi = std::make_unique<DebugUi>(m_window, m_renderer->device(), m_renderer->renderPass(),
                                           m_renderer->swapChainImageCount());
}

Application::~Application() {
    for (auto& module : m_modules) {
        module->shutdown();
    }
}

void Application::run() {
    for (auto& module : m_modules) {
        module->init(*this);
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrameTime = startTime;

    while (m_window.pollEvents([this](const SDL_Event& e) { m_debugUi->processEvent(e); })) {
        auto now = std::chrono::high_resolution_clock::now();
        UpdateContext updateCtx{};
        updateCtx.dt = std::chrono::duration<float>(now - lastFrameTime).count();
        updateCtx.totalTime = std::chrono::duration<float>(now - startTime).count();
        lastFrameTime = now;

        for (auto& module : m_modules) {
            module->update(updateCtx);
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
        for (auto& module : m_modules) {
            module->renderUi();
        }

        if (m_renderer->beginFrame()) {
            VkCommandBuffer cmd = m_renderer->currentCommandBuffer();

            for (auto& module : m_modules) {
                module->compute(cmd);
            }

            m_renderer->beginRenderPass();
            renderCtx.cmd = cmd;
            for (auto& module : m_modules) {
                module->render(renderCtx);
            }
            m_debugUi->render(cmd);

            m_renderer->endFrame();
        }
    }
}

} // namespace kke
