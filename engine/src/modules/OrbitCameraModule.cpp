#include "kke/modules/OrbitCameraModule.h"
#include "kke/Application.h"
#include "kke/EngineSettings.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace kke {

void OrbitCameraModule::init(Application& app) {
    m_app = &app;
    app.camera().target = m_target;
}

void OrbitCameraModule::update(const UpdateContext& ctx) {
    Camera& camera = m_app->camera();
    const auto& mouse = m_app->window().mouseState();

    // Don't fight ImGui: if the mouse is over/dragging an ImGui widget,
    // that input belongs to the UI, not the camera.
    bool uiWantsMouse = ImGui::GetIO().WantCaptureMouse || m_app->uiCapturesMouse();

    bool editor = m_controls == Controls::Editor;
    bool orbitButton = editor ? mouse.rightButtonDown : mouse.leftButtonDown;
    bool panButton = editor ? mouse.middleButtonDown : mouse.rightButtonDown;
    if (!uiWantsMouse && orbitButton) {
        m_yaw += mouse.deltaX * m_orbitSensitivity * m_sensitivityScale;
        m_pitch -= mouse.deltaY * m_orbitSensitivity * m_sensitivityScale * (m_invertY ? -1.0f : 1.0f);
        constexpr float kPitchLimit = glm::half_pi<float>() - 0.05f;
        m_pitch = std::clamp(m_pitch, -kPitchLimit, kPitchLimit);
    }

    if (m_autoOrbit) {
        m_yaw += glm::radians(m_autoOrbitSpeedDegPerSec) * ctx.dt;
    }

    glm::vec3 forward(std::cos(m_pitch) * std::sin(m_yaw),
                       std::sin(m_pitch),
                       std::cos(m_pitch) * std::cos(m_yaw));
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::cross(right, forward);

    // Pan speed grows with distance so a drag moves the view by roughly
    // the same screen amount whether zoomed in or out.
    float panScale = editor ? m_distance / 3.5f : 1.0f;
    if (!uiWantsMouse && panButton) {
        camera.target += (right * (-mouse.deltaX * m_panSensitivity) + up * (mouse.deltaY * m_panSensitivity)) * panScale;
    }
    // WantTextInput, not WantCaptureKeyboard: ImGui claims the keyboard
    // whenever one of its windows merely has focus (BUG-041).
    if (editor && !ImGui::GetIO().WantTextInput) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        glm::vec3 flatForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
        glm::vec3 move(0.0f);
        if (keys[SDL_SCANCODE_W]) move += flatForward;
        if (keys[SDL_SCANCODE_S]) move -= flatForward;
        if (keys[SDL_SCANCODE_D]) move += flatRight;
        if (keys[SDL_SCANCODE_A]) move -= flatRight;
        if (keys[SDL_SCANCODE_E]) move.y += 1.0f;
        if (keys[SDL_SCANCODE_Q]) move.y -= 1.0f;
        float speed = std::max(2.0f, m_distance) * (keys[SDL_SCANCODE_LSHIFT] ? 3.0f : 1.0f);
        camera.target += move * speed * ctx.dt;
    }

    if (!uiWantsMouse) {
        m_distance -= mouse.scrollDelta * m_zoomSensitivity;
        m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
    }

    camera.position = camera.target - forward * m_distance;
}

void OrbitCameraModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(340, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera");
    if (m_controls == Controls::Editor) ImGui::TextWrapped("Right-drag: orbit   Middle-drag: pan   Scroll: zoom\nWASD: move   Q/E: down/up   Shift: faster");
    else ImGui::TextWrapped("Left-drag: orbit   Right-drag: pan   Scroll: zoom");
    ImGui::Checkbox("Auto-orbit", &m_autoOrbit);
    ImGui::SliderFloat("Auto-orbit speed", &m_autoOrbitSpeedDegPerSec, -180.0f, 180.0f);
    ImGui::End();
}

void OrbitCameraModule::onSettingsChanged(const EngineSettings& settings) {
    m_sensitivityScale = settings.controls.mouseSensitivity;
    m_invertY = settings.controls.invertY;
}

} // namespace kke
