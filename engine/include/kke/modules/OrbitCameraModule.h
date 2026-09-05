#pragma once

#include "kke/Module.h"
#include <glm/glm.hpp>

namespace kke {

// A generic mouse-driven orbit camera: left-drag to orbit, right-drag to
// pan, scroll to zoom, plus an "auto-orbit" toggle. Lives in the engine
// core (not app/) because essentially every 3D demo wants some version of
// this — it's a building block, not example content.
//
// Writes directly into Application::camera() every frame; doesn't own or
// render anything itself, so it has no init()/render() to speak of.
class OrbitCameraModule : public Module {
public:
    // Defaults match this class's original hardcoded values (tuned
    // for the generic kke_demo_game's small-scale content — a unit
    // cube at distance 3.5). Any demo whose content lives at a
    // different scale — a real-world-scale physics scene with a
    // 100-unit floor, for instance — should pass its own values
    // rather than fight the defaults with a render-side scale hack.
    explicit OrbitCameraModule(float initialDistance = 3.5f, float initialPitch = 0.5f,
                                float initialYaw = -0.6f, glm::vec3 initialTarget = glm::vec3(0.0f))
        : m_distance(initialDistance), m_pitch(initialPitch), m_yaw(initialYaw), m_target(initialTarget) {}

    const char* name() const override { return "OrbitCamera"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;

private:
    Application* m_app = nullptr;

    float m_yaw;
    float m_pitch; // radians, clamped away from the poles
    float m_distance;
    glm::vec3 m_target;

    bool m_autoOrbit = false;
    float m_autoOrbitSpeedDegPerSec = 20.0f;

    float m_orbitSensitivity = 0.005f;
    float m_panSensitivity = 0.003f;
    float m_zoomSensitivity = 0.3f;
};

} // namespace kke
