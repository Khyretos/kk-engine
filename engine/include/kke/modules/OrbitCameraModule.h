#pragma once

#include "kke/Module.h"

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
    const char* name() const override { return "OrbitCamera"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;

private:
    Application* m_app = nullptr;

    float m_yaw = -0.6f;   // radians
    float m_pitch = 0.5f;  // radians, clamped away from the poles
    float m_distance = 3.5f;

    bool m_autoOrbit = false;
    float m_autoOrbitSpeedDegPerSec = 20.0f;

    float m_orbitSensitivity = 0.005f;
    float m_panSensitivity = 0.003f;
    float m_zoomSensitivity = 0.3f;
};

} // namespace kke
