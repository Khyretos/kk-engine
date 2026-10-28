#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace kke {

struct EngineSettings;

// What lets more people play (docs/ACCESSIBILITY.md): the switches from the
// player's settings, in one place every module can read, plus the screen
// reader hook. Language and captions live in kke/Localization.h and
// kke/Subtitles.h; remapping in kke/InputModule; UI size in
// EngineSettings::graphics.uiScale.
class Accessibility {
public:
    struct Options {
        std::string colorBlind = "off"; // "off" | "protanopia" | "deuteranopia" | "tritanopia"
        float colorBlindStrength = 1.0f;
        bool highContrast = false;
        bool reduceMotion = false;
        bool screenReader = false;
    };

    // The process-wide instance. Tests make their own.
    static Accessibility& get();

    const Options& options() const { return m_options; }
    // Replaces the options; listeners hear it when something changed.
    void set(const Options& options);
    void apply(const EngineSettings& settings); // from the player's settings
    bool reduceMotion() const { return m_options.reduceMotion; }
    bool highContrast() const { return m_options.highContrast; }
    bool screenReader() const { return m_options.screenReader; }

    // Colour correction for the colour-blind (daltonisation, Machado et al.
    // 2009): the 3x3 matrix, rows = output red, green, blue, applied to
    // linear colour in the tone-map pass. It moves what a person cannot see
    // into what they can; it does not simulate. Grey stays grey. Identity
    // for "off" or strength 0; strength blends the matrix with the identity.
    static glm::mat3 colorMatrix(const std::string& mode, float strength);
    // The three rows as the shader reads them (w = 1 when it is on).
    static std::vector<glm::vec4> colorMatrixRows(const std::string& mode, float strength);
    // The same correction on one colour, for tests and thumbnails.
    static glm::vec3 correct(const glm::vec3& linear, const std::string& mode, float strength);

    // ---- screen reader hook
    // A platform layer that has a real screen reader (NVDA, VoiceOver, Orca)
    // installs its own; speech(text, interrupt) must not block.
    using Announcer = std::function<void(const std::string& text, bool interrupt)>;
    void setAnnouncer(Announcer announcer);
    // Said aloud when `screenReader` is on and an announcer is set, and kept
    // in recent() either way. `interrupt` cuts off what is being said.
    // Returns true when it went to the announcer.
    bool announce(const std::string& text, bool interrupt = false);
    // The last announcements, newest last (at most 16).
    std::vector<std::string> recent() const;
    // The system's speech where there is one: spd-say (speech-dispatcher) on
    // Linux, say on macOS. Empty (a null function) where there is none.
    static Announcer systemSpeech();

    // Called after set() changes something. Returns an id.
    int onChange(std::function<void()> callback);
    void removeOnChange(int id);

private:
    Options m_options;
    Announcer m_announcer;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_recent;
    std::map<int, std::function<void()>> m_listeners;
    int m_nextListener = 1;
};

// What the player's settings say about words and access, applied everywhere
// they are read: the language (kke::Localization; KKE_LANGUAGE wins in a
// development build), the captions on or off, the accessibility switches.
// SettingsModule and the game menu call it when settings change.
void applyTextAndAccessibility(const EngineSettings& settings);

} // namespace kke
