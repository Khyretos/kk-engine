#pragma once

#include "kke/AudioMixer.h"
#include "kke/Module.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace kke {

class AudioModule;

// Sound made visible, for deaf and hard-of-hearing players (docs/AUDIO.md
// "Accessibility"). Every playing sound becomes a mark on a ring around
// the screen centre, in the direction it comes from (top = ahead), sized
// by loudness, coloured by category, drawn hollow when it comes through a
// wall; optional captions name the material ("Metal impact"). Sounds with
// no position (UI, music) are captioned at the bottom.
//
// Everything is the player's to tune: on/off, size, opacity, which
// categories show, their colours, how quiet a sound may be and still
// show, captions. Saved to a JSON file (default accessibility.json).
//
// A separate module from AudioModule on purpose: its overlay has to stay
// on when engine panels are hidden, and a game can swap it for its own.
class SoundVisualizerModule : public Module {
public:
    struct Settings {
        bool enabled = false;                    // opt-in; games can turn it on by default
        float ringScale = 0.40f;                 // ring radius, fraction of the smaller screen side
        float markScale = 1.0f;
        float opacity = 0.85f;
        float minLoudness = 0.02f;
        bool captions = true;
        float holdSeconds = 0.8f;                // how long a mark stays after the sound
        bool showCategory[size_t(SoundCategory::Count)] = {true, true, true, true, true, true, true};
        glm::vec3 color[size_t(SoundCategory::Count)] = {
            {1.00f, 0.75f, 0.20f},  // Impact: amber
            {0.60f, 0.85f, 1.00f},  // Footstep: light blue
            {0.40f, 1.00f, 0.50f},  // Voice: green
            {0.75f, 0.75f, 0.75f},  // Ambient: grey
            {0.90f, 0.90f, 1.00f},  // UI
            {0.85f, 0.55f, 1.00f},  // Music: violet
            {1.00f, 0.25f, 0.25f},  // Alert: red
        };
    };

    explicit SoundVisualizerModule(std::string settingsPath = "accessibility.json");
    const char* name() const override { return "SoundVisualizer"; }
    void init(Application& app) override;
    void renderUi() override;

    Settings settings;
    bool showPanel = false;

    bool load();
    bool save() const;

    // Where a sound at `azimuth` lands on the ring (screen pixels): exposed
    // for tests; 0 = straight up, +pi/2 = right.
    static glm::vec2 ringPoint(float azimuth, glm::vec2 center, float radius);

private:
    void drawOverlay();
    void drawPanel();

    Application* m_app = nullptr;
    AudioModule* m_audio = nullptr;
    struct Mark { ActiveSound sound; float level = 0.0f; };
    std::unordered_map<uint32_t, Mark> m_marks;
    std::string m_path;
};

} // namespace kke
