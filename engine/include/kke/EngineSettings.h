#pragma once

#include <map>
#include <string>

namespace kke {

// Player-facing settings, as plain data: what a Settings menu edits and
// what gets saved to disk. Deliberately knows nothing about Vulkan, SDL
// or modules — kke::SettingsModule is what applies these to a running
// Application. Keeping the data separate means the JSON round-trip is
// unit-testable (tests/test_engine_settings.cpp) and a game can add its
// own fields in `custom` without touching the engine.
struct EngineSettings {
    struct Graphics {
        bool fullscreen = false;
        bool vsync = true;
        float frameRateLimit = 0.0f;   // 0 = unlimited
        float fieldOfView = 60.0f;     // vertical, degrees
        bool shadows = true;
        float brightness = 1.0f;       // multiplies ambient light
        float uiScale = 1.0f;          // see UiModule::setUiScale()
        bool showDebugOverlay = false; // the ImGui developer panels (F1 in the showcase)
    } graphics;

    struct Audio {
        // Stored and shown in the menu; there is no audio module yet
        // (see ROADMAP.md) — a future one reads these.
        int master = 80;
        int music = 60;
        int effects = 80;
        bool muteWhenUnfocused = true;
    } audio;

    struct Controls {
        float mouseSensitivity = 1.0f;
        bool invertY = false;
        // action name -> SDL key name (SDL_GetKeyName / SDL_GetKeyFromName)
        std::map<std::string, std::string> keyBindings = {
            {"move_forward", "W"}, {"move_back", "S"}, {"move_left", "A"}, {"move_right", "D"},
            {"jump", "Space"}, {"interact", "E"}, {"inventory", "I"}, {"pause", "Escape"},
        };
    } controls;

    struct Gameplay {
        std::string difficulty = "normal"; // "easy" | "normal" | "hard"
        int maxPhysicsStepsPerFrame = 2;   // Application::setMaxFixedStepsPerFrame()
        bool showDamageNumbers = true;
    } gameplay;

    // The resource governor's inputs (kke/ResourceGovernor.h): by default
    // the engine takes what it needs and leaves the rest of the machine
    // alone; "use everything" lifts that.
    struct Performance {
        bool useEverything = false;
        int workerThreads = 0;            // 0 = governor decides; takes effect on restart
        float renderScale = 1.0f;         // 0.5..1: fewer pixels for the 3D view
        float backgroundFrameRate = 15.0f; // cap while unfocused (0 = no cap)
    } performance;

    // Game-specific extras, saved and loaded untouched.
    std::map<std::string, std::string> custom;

    // Clamps every numeric field into its valid range and fixes unknown
    // enum-like strings. Called after loading, and by menus after edits,
    // so a hand-edited or old settings file can never put the engine in
    // a broken state (e.g. a 0 FOV or a 50x UI scale).
    void sanitize();

    bool operator==(const EngineSettings& other) const;
    bool operator!=(const EngineSettings& other) const { return !(*this == other); }
};

// JSON <-> settings. Loading is forgiving on purpose: missing keys keep
// their defaults and unknown keys are ignored, so settings files from an
// older or newer build still load. Only unparseable JSON is an error.
std::string settingsToJson(const EngineSettings& settings);
EngineSettings settingsFromJson(const std::string& json); // throws std::runtime_error on invalid JSON

// File helpers. loadSettingsFile returns defaults (and never throws) if
// the file is missing or unreadable — a first run has no file yet.
EngineSettings loadSettingsFile(const std::string& path, std::string* errorOut = nullptr);
bool saveSettingsFile(const EngineSettings& settings, const std::string& path);

} // namespace kke
