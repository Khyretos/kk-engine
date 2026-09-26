#include "kke/modules/SettingsModule.h"

#include "kke/Application.h"
#include "kke/Capabilities.h"
#include "kke/Log.h"

namespace kke {

void SettingsModule::init(Application& app) {
    m_app = &app;
    m_baseAmbient = app.lighting().ambientColor;
    std::string error;
    m_settings = loadSettingsFile(m_path, &error);
    if (!error.empty()) log::get(name())->info("{}", error);
    else log::get(name())->info("loaded settings from '{}'", m_path);
    m_saved = m_settings;
    apply();
}

void SettingsModule::apply() {
    if (!m_app) return;
    m_settings.sanitize();
    const auto& g = m_settings.graphics;
    if (m_app->window().isFullscreen() != g.fullscreen) m_app->window().setFullscreen(g.fullscreen);
    m_app->renderer().setVSync(g.vsync);
    m_app->setResourceBudget(computeBudget(m_settings, usableCpuCount())); // sets the frame cap too
    m_app->camera().fovDegrees = g.fieldOfView;
    m_app->lighting().shadowsEnabled = g.shadows;
    m_app->lighting().ambientColor = m_baseAmbient * g.brightness;
    m_app->debugUi().setVisible(g.showDebugOverlay);
    m_app->setMaxFixedStepsPerFrame(static_cast<uint32_t>(m_settings.gameplay.maxPhysicsStepsPerFrame));
    for (ISettingsListener* listener : m_app->findCapability<ISettingsListener>()) {
        listener->onSettingsChanged(m_settings);
    }
}

bool SettingsModule::save() {
    m_settings.sanitize();
    if (!saveSettingsFile(m_settings, m_path)) {
        log::get(name())->error("could not write settings to '{}'", m_path);
        return false;
    }
    m_saved = m_settings;
    log::get(name())->info("saved settings to '{}'", m_path);
    return true;
}

void SettingsModule::revert() {
    m_settings = m_saved;
    apply();
}

void SettingsModule::resetToDefaults() {
    m_settings = EngineSettings{};
    apply();
}

} // namespace kke
