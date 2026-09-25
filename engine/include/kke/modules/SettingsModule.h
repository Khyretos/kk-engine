#pragma once

#include "kke/Module.h"
#include "kke/EngineSettings.h"

#include <glm/glm.hpp>
#include <string>

namespace kke {

// Owns the player's EngineSettings: loads them from disk at startup,
// applies them to the running engine, and saves them on request. A
// settings menu edits settings() and calls apply() (live preview) and
// save() (keep). What "apply" actually touches:
//   window fullscreen, swapchain VSync, frame-rate limit, camera FOV,
//   shadows on/off, ambient brightness, the ImGui developer overlay,
//   max physics catch-up steps — directly on Application;
//   UI scale, mouse sensitivity/invert — through ISettingsListener, so
//   those modules stay optional (see kke/Capabilities.h).
// Audio values are stored but nothing plays sound yet.
class SettingsModule : public Module {
public:
    explicit SettingsModule(std::string path = "settings.json") : m_path(std::move(path)) {}

    const char* name() const override { return "Settings"; }
    void init(Application& app) override;

    EngineSettings& settings() { return m_settings; }
    const EngineSettings& saved() const { return m_saved; }

    void apply();            // push settings() into the engine (live)
    bool save();             // write settings() to disk; it becomes saved()
    void revert();           // settings() = saved(), then apply()
    void resetToDefaults();  // settings() = defaults, then apply() (not saved until save())
    bool hasUnsavedChanges() const { return m_settings != m_saved; }
    const std::string& path() const { return m_path; }

private:
    Application* m_app = nullptr;
    std::string m_path;
    EngineSettings m_settings;
    EngineSettings m_saved;
    glm::vec3 m_baseAmbient{0.15f}; // ambient before brightness was applied
};

} // namespace kke
