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
        // Texels across each shadow cascade's tile: 1024 or 2048 (4 tiles per view).
        int shadowResolution = 2048;
        // Multisample anti-aliasing for the 3D view: 1 (off), 2, 4 or 8.
        // Read at startup (takes effect on restart); software rasterisers
        // (the min-spec reference) always run at 1.
        int msaa = 4;
        // Screen-space global illumination (Application::setScreenGi): light
        // bounced between nearby surfaces and soft shadow in corners.
        bool globalIllumination = false;
        // Reflections on smooth surfaces (kke/Reflections.h): "off", "screen",
        // "kke" (screen, then rays through stand-ins) or "classic" (full rays).
        std::string reflections = "off";
        // Volumetric fog (Application::setVolumetricFog): the height fog lit
        // for real, with light shafts in the sun's shadow and halos round lamps.
        bool volumetricFog = false;
        // Morphological anti-aliasing over MSAA (Renderer::setSmaa): edges in
        // shading and alpha tests that MSAA can't smooth. One frame, no ghosting.
        // On by default (Kees, 2026-10-18); TAA comes as an option beside it.
        bool smaa = true;
        // Temporal anti-aliasing (Application::setTaa), an option over the clean
        // image: smoother still when standing still, a little softer in motion.
        bool taa = false;
        // Bloom (Renderer::setBloom): bright light bleeding into what is around it.
        bool bloom = true;
        // Depth of field (Renderer::setDepthOfField): a lens's blur in front of and behind the focus.
        bool depthOfField = false;
        // Motion blur (Renderer::setMotionBlur): the camera's movement smears the image.
        bool motionBlur = false;
        // Occlusion culling (Renderer::setOcclusionCulling): models hidden behind others aren't drawn.
        bool occlusionCulling = false;
        // Automatic levels of detail for models (kke::ModelModule, docs/LOD.md): simpler meshes for small, far objects.
        // lodBias > 1 keeps detail to greater distances, < 1 drops it sooner.
        bool autoLod = true;
        float lodBias = 1.0f;
        // Ray-traced sun shadows (kke/ShadowRays.h): "off", "kke" (only past
        // the shadow map's reach) or "classic" (every pixel).
        std::string shadowRays = "off";
        float brightness = 1.0f;       // multiplies ambient light
        float uiScale = 1.0f;          // see UiModule::setUiScale()
        bool showDebugOverlay = false; // the ImGui developer panels (F1 in the showcase)
    } graphics;

    struct Audio {
        // Percent. AudioModule applies them (AudioModule::applyVolumes).
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

    // Language and captions (docs/LOCALIZATION.md, docs/ACCESSIBILITY.md).
    struct Text {
        std::string language = "auto"; // "auto" = the system's language, else a code from kke::Localization ("nl", "pt-BR")
        bool subtitles = true;         // captions for speech and sounds (kke::Subtitles)
        float subtitleSize = 1.0f;     // 0.75..2.5, times the UI scale
        float subtitleBackground = 0.6f; // 0..1: how dark the box behind the words is
    } text;

    // What lets more people play (docs/ACCESSIBILITY.md). Each is a switch the
    // game and the engine read; none changes the rules of a game.
    struct Accessibility {
        std::string colorBlind = "off"; // "off" | "protanopia" | "deuteranopia" | "tritanopia": colours shifted so they tell apart
        float colorBlindStrength = 1.0f; // 0..1
        bool highContrast = false;       // UI: white on black, thicker outlines
        bool reduceMotion = false;       // no screen shake, no motion blur, no camera sway
        bool screenReader = false;       // menus and notices are spoken (kke::Accessibility)
    } accessibility;

    // What may leave the machine (docs/CRASH_REPORTS.md). Nothing does unless the player says so.
    struct Privacy {
        bool sendCrashReports = false; // crash reports are always written to disk; this lets a game's uploader send them
    } privacy;

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
// `defaults` is what a missing key falls back to: plain EngineSettings{}
// or the hardware target's preset (kke/HardwareTarget.h), so a Steam Deck
// starts on Steam Deck settings and the player's saved choices still win.
std::string settingsToJson(const EngineSettings& settings);
EngineSettings settingsFromJson(const std::string& json, const EngineSettings& defaults = EngineSettings{}); // throws std::runtime_error on invalid JSON

// File helpers. loadSettingsFile returns defaults (and never throws) if
// the file is missing or unreadable — a first run has no file yet.
EngineSettings loadSettingsFile(const std::string& path, std::string* errorOut = nullptr,
                                const EngineSettings& defaults = EngineSettings{});
bool saveSettingsFile(const EngineSettings& settings, const std::string& path);

} // namespace kke
