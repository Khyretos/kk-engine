#include "kke/modules/OrbitCameraModule.h"
#include "kke/Application.h"

#include <imgui.h>
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
    bool uiWantsMouse = ImGui::GetIO().WantCaptureMouse;

    if (!uiWantsMouse && mouse.leftButtonDown) {
        m_yaw += mouse.deltaX * m_orbitSensitivity;
        m_pitch -= mouse.deltaY * m_orbitSensitivity;
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

    if (!uiWantsMouse && mouse.rightButtonDown) {
        camera.target += right * (-mouse.deltaX * m_panSensitivity) + up * (mouse.deltaY * m_panSensitivity);
    }

    if (!uiWantsMouse) {
        m_distance -= mouse.scrollDelta * m_zoomSensitivity;
        m_distance = std::clamp(m_distance, 0.5f, 30.0f);
    }

    camera.position = camera.target - forward * m_distance;
}

void OrbitCameraModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(340, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera");
    ImGui::TextWrapped("Left-drag: orbit   Right-drag: pan   Scroll: zoom");
    ImGui::Checkbox("Auto-orbit", &m_autoOrbit);
    ImGui::SliderFloat("Auto-orbit speed", &m_autoOrbitSpeedDegPerSec, -180.0f, 180.0f);
    ImGui::End();
}

} // namespace kke
