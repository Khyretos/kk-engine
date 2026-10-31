#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace kke {

// Captions (docs/ACCESSIBILITY.md): what is said, and the sounds that matter,
// as text for a while. A game calls say(); UiModule draws the lines at the
// bottom of the screen (size and box from the player's settings) and the
// text goes through the localizer like any other, so a caption keyed in the
// player's language shows in it. This class is only the timed queue:
// nothing in it draws.
class Subtitles {
public:
    struct Options {
        std::string speaker;        // "Mira": shown before the words
        float duration = 0.0f;      // seconds; 0 = long enough to read
        std::string kind = "speech"; // "speech", "sound" ([door slams]) or "note"
    };
    struct Line {
        int id = 0;
        std::string speaker, text, kind;
        float duration = 0.0f, remaining = 0.0f;
    };

    // The process-wide instance. Tests make their own.
    static Subtitles& get();

    // Shows a caption; returns its id. The same text and speaker already on
    // screen is renewed, not repeated. Does nothing but return 0 when off.
    int say(const std::string& text, const Options& options);
    int say(const std::string& text) { return say(text, Options{}); }
    void remove(int id);
    void clear();
    // Time passes: lines whose time is up go.
    void update(float dt);
    // On screen now, oldest first, at most maxLines() of the newest.
    std::vector<Line> lines() const;
    // Bumped whenever the lines change: a renderer rebuilds only then.
    unsigned revision() const { return m_revision; }

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    void setMaxLines(int n);
    int maxLines() const { return m_maxLines; }

    // How long a caption of this text takes to read: 1.2 s and 17 characters
    // a second, between 1.5 and 12 s.
    static float readingTime(const std::string& text);

private:
    mutable std::mutex m_mutex;
    std::vector<Line> m_lines;
    int m_nextId = 1;
    int m_maxLines = 3;
    bool m_enabled = true;
    unsigned m_revision = 0;
};

} // namespace kke
