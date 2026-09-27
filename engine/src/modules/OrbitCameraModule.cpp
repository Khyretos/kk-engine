#include "kke/modules/OrbitCameraModule.h"
#include "kke/Application.h"
#include "kke/EngineSettings.h"
#include "kke/modules/InputModule.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace kke {

void OrbitCameraModule::init(Application& app) {
    m_app = &app;
    app.camera().target = m_target;
    if (m_padControls) definePadActions();
}

void OrbitCameraModule::setPadControls(bool enabled) {
    m_padControls = enabled;
    if (enabled && m_app) definePadActions();
}

void OrbitCameraModule::definePadActions() {
    auto* input = m_app->getModule<InputModule>();
    if (!input) return;
    for (int p = 0; p < input->players(); ++p) {
        InputMap& m = input->map(p);
        if (!m.action("camera.orbit")) m.defineAction({ "camera.orbit", "Turn the camera", "Camera", "game", ActionType::Axis2D, true });
        if (!m.action("camera.zoom")) m.defineAction({ "camera.zoom", "Camera closer / further", "Camera", "game", ActionType::Axis1D, true });
        if (m.bindingsFor("camera.orbit").empty()) {
            Binding b = InputModule::bind("camera.orbit", InputModule::padAxis(SDL_GAMEPAD_AXIS_RIGHTX), Trigger::Continuous);
            b.sourceY = InputModule::padAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            b.deadzone = 0.2f;
            m.addBinding(b);
        }
        if (m.bindingsFor("camera.zoom").empty()) {
            Binding in = InputModule::bind("camera.zoom", InputModule::pad(SDL_GAMEPAD_BUTTON_DPAD_UP), Trigger::Continuous);
            Binding out = InputModule::bind("camera.zoom", InputModule::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN), Trigger::Continuous);
            out.scale = -1.0f;
            m.addBinding(in);
            m.addBinding(out);
        }
    }
}

// Before init() only the starting target exists; after it, the camera's
// own target is the live one (panning moves it).
void OrbitCameraModule::setTarget(const glm::vec3& target) {
    m_target = target;
    if (m_app) m_app->camera().target = target;
}

glm::vec3 OrbitCameraModule::target() const { return m_app ? m_app->camera().target : m_target; }

void OrbitCameraModule::setView(const glm::vec3& target, float distance, float pitch, float yaw) {
    setTarget(target);
    m_distance = std::clamp(distance, m_minDistance, m_maxDistance);
    m_pitch = pitch;
    m_yaw = yaw;
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
        m_pitch = std::clamp(m_pitch, m_minPitch, m_maxPitch);
    }

    // Two fingers: drag turns the view, pinch zooms, twist spins it.
    if (const TouchGestures::Frame t = m_touches.take(); t.active && m_touchGestures) {
        nudge(t.pan.x * m_touchOrbitSensitivity * m_sensitivityScale - t.twist,
              -t.pan.y * m_touchOrbitSensitivity * m_sensitivityScale * (m_invertY ? -1.0f : 1.0f),
              t.pinch > 1e-3f ? 1.0f / t.pinch : 1.0f);
    }

    // A controller: the right stick turns, the d-pad zooms (setPadControls).
    if (m_padControls) {
        if (auto* input = m_app->getModule<InputModule>()) {
            const InputMap& m = input->map(0);
            const glm::vec2 turn = m.axis2("camera.orbit");
            const float zoom = m.axis("camera.zoom");
            if (turn.x != 0.0f || turn.y != 0.0f || zoom != 0.0f)
                nudge(turn.x * 2.2f * ctx.dt * m_sensitivityScale, -turn.y * 1.6f * ctx.dt * m_sensitivityScale * (m_invertY ? -1.0f : 1.0f),
                      1.0f - zoom * 1.5f * ctx.dt);
        }
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

void OrbitCameraModule::setPitchLimits(float minPitch, float maxPitch) {
    constexpr float kPole = glm::half_pi<float>() - 0.05f;
    m_minPitch = std::clamp(std::min(minPitch, maxPitch), -kPole, kPole);
    m_maxPitch = std::clamp(std::max(minPitch, maxPitch), -kPole, kPole);
    m_pitch = std::clamp(m_pitch, m_minPitch, m_maxPitch);
}

void OrbitCameraModule::nudge(float yawRadians, float pitchRadians, float zoomFactor) {
    m_yaw += yawRadians;
    m_pitch = std::clamp(m_pitch + pitchRadians, m_minPitch, m_maxPitch);
    if (zoomFactor > 0.0f) m_distance = std::clamp(m_distance * zoomFactor, m_minDistance, m_maxDistance);
}

void OrbitCameraModule::onEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        // SDL gives 0..1 of the window; pixels keep pinch and twist unskewed.
        int w = 1, h = 1;
        SDL_GetWindowSize(m_app->window().handle(), &w, &h);
        const glm::vec2 pos(event.tfinger.x * static_cast<float>(w), event.tfinger.y * static_cast<float>(h));
        if (event.type == SDL_EVENT_FINGER_DOWN) m_touches.fingerDown(event.tfinger.fingerID, pos);
        else if (event.type == SDL_EVENT_FINGER_MOTION) m_touches.fingerMove(event.tfinger.fingerID, pos);
        else m_touches.fingerUp(event.tfinger.fingerID);
        break;
    }
    case SDL_EVENT_FINGER_CANCELED:
        m_touches.fingerUp(event.tfinger.fingerID);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_touches.clear();
        break;
    default:
        break;
    }
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
