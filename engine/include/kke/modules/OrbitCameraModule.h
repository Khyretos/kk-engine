#pragma once

#include "kke/Module.h"
#include "kke/Capabilities.h"
#include <glm/glm.hpp>

namespace kke {

// A generic mouse-driven orbit camera: left-drag to orbit, right-drag to
// pan, scroll to zoom, plus an "auto-orbit" toggle. Lives in the engine
// core (not app/) because essentially every 3D demo wants some version of
// this — it's a building block, not example content.
//
// Writes directly into Application::camera() every frame; doesn't own or
// render anything itself, so it has no init()/render() to speak of.
class OrbitCameraModule : public Module, public ISettingsListener {
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
    void onSettingsChanged(const EngineSettings& settings) override;

    void setAutoOrbit(bool enabled, float degreesPerSecond = 20.0f) { m_autoOrbit = enabled; m_autoOrbitSpeedDegPerSec = degreesPerSecond; }

    // Viewer (default): left-drag orbit, right-drag pan, scroll zoom.
    // Editor: left click is free for the tool (select/place), so orbit is
    // right-drag, pan is middle-drag, and WASD moves the target across
    // the ground (Shift = faster, Q/E = down/up), scaled by distance.
    enum class Controls { Viewer, Editor };
    void setControls(Controls c) { m_controls = c; }
    void setDistanceLimits(float minDistance, float maxDistance) { m_minDistance = minDistance; m_maxDistance = maxDistance; }
    // Follow something (the view angles and distance stay the user's).
    void setTarget(const glm::vec3& target);
    glm::vec3 target() const;
    // Jump to a view (e.g. when a demo switches scenes). Angles in radians.
    void setView(const glm::vec3& target, float distance, float pitch, float yaw);
    float distance() const { return m_distance; }
    float pitch() const { return m_pitch; }
    float yaw() const { return m_yaw; }

private:
    Application* m_app = nullptr;

    float m_yaw;
    float m_pitch; // radians, clamped away from the poles
    float m_distance;
    glm::vec3 m_target;

    Controls m_controls = Controls::Viewer;
    float m_minDistance = 0.5f;
    float m_maxDistance = 30.0f;

    bool m_autoOrbit = false;
    float m_autoOrbitSpeedDegPerSec = 20.0f;

    float m_orbitSensitivity = 0.005f;
    float m_panSensitivity = 0.003f;
    float m_zoomSensitivity = 0.3f;
    float m_sensitivityScale = 1.0f; // from EngineSettings::controls.mouseSensitivity
    bool m_invertY = false;
};

} // namespace kke
