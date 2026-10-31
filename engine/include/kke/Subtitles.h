#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Dialogue subtitles (ENG-F85): lines that appear one after another, stay as
// long as they take to read, and fade. Pure logic; kke::SubtitleModule draws
// them and sound captions are SoundVisualizerModule's.
//
// A line shows as soon as fewer than maxLines are on screen (so a long
// conversation never piles up), and its time starts then. With no time given
// it stays as long as the text needs: 17 characters a second, at least 1.5 s
// and at most 10 s, plus a short breath.
struct SubtitleView {
    uint32_t id = 0;
    std::string text;
    std::string speaker; // "" = none
    float alpha = 1.0f;  // 0..1: faded in and out
};

class SubtitleQueue {
public:
    // Seconds a text needs to be read (UTF-8 characters, not bytes).
    static float readingSeconds(const std::string& text);

    // Queues a line; `seconds` <= 0 = as long as the text needs. Returns its id.
    uint32_t say(const std::string& text, const std::string& speaker = {}, float seconds = 0.0f);
    void update(float dt);
    // What is on screen now, oldest first.
    std::vector<SubtitleView> visible() const;
    void remove(uint32_t id);
    void clear();

    void setMaxLines(int lines) { m_maxLines = lines < 1 ? 1 : lines; }
    int maxLines() const { return m_maxLines; }
    size_t pending() const;  // queued, not shown yet
    size_t shown() const;    // on screen
    bool empty() const { return m_lines.empty(); }

    static constexpr float kFadeIn = 0.15f;
    static constexpr float kFadeOut = 0.35f;

private:
    struct Line {
        uint32_t id = 0;
        std::string text, speaker;
        float seconds = 0.0f; // on screen, fades included
        float age = -1.0f;    // < 0: still waiting
    };
    std::vector<Line> m_lines;
    uint32_t m_nextId = 1;
    int m_maxLines = 3;
};

} // namespace kke
