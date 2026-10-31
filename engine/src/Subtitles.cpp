#include "kke/Subtitles.h"

#include <algorithm>

namespace kke {

float SubtitleQueue::readingSeconds(const std::string& text) {
    size_t characters = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++characters; // not a UTF-8 continuation byte
    return std::clamp(static_cast<float>(characters) / 17.0f, 1.5f, 10.0f) + kFadeIn + kFadeOut;
}

uint32_t SubtitleQueue::say(const std::string& text, const std::string& speaker, float seconds) {
    if (text.empty()) return 0;
    Line line;
    line.id = m_nextId++;
    line.text = text;
    line.speaker = speaker;
    line.seconds = seconds > 0.0f ? seconds + kFadeIn + kFadeOut : readingSeconds(text);
    m_lines.push_back(std::move(line));
    return m_lines.back().id;
}

void SubtitleQueue::update(float dt) {
    for (Line& line : m_lines)
        if (line.age >= 0.0f) line.age += dt;
    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [](const Line& l) { return l.age >= l.seconds; }), m_lines.end());
    // Waiting lines move up as room appears; their clock starts there.
    int showing = 0;
    for (const Line& line : m_lines)
        if (line.age >= 0.0f) ++showing;
    for (Line& line : m_lines) {
        if (showing >= m_maxLines) break;
        if (line.age < 0.0f) {
            line.age = 0.0f;
            ++showing;
        }
    }
}

std::vector<SubtitleView> SubtitleQueue::visible() const {
    std::vector<SubtitleView> out;
    for (const Line& line : m_lines) {
        if (line.age < 0.0f) continue;
        const float in = std::clamp(line.age / kFadeIn, 0.0f, 1.0f);
        const float outFade = std::clamp((line.seconds - line.age) / kFadeOut, 0.0f, 1.0f);
        out.push_back({ line.id, line.text, line.speaker, std::min(in, outFade) });
    }
    return out;
}

void SubtitleQueue::remove(uint32_t id) {
    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [id](const Line& l) { return l.id == id; }), m_lines.end());
}

void SubtitleQueue::clear() { m_lines.clear(); }

size_t SubtitleQueue::pending() const {
    return static_cast<size_t>(std::count_if(m_lines.begin(), m_lines.end(), [](const Line& l) { return l.age < 0.0f; }));
}

size_t SubtitleQueue::shown() const { return m_lines.size() - pending(); }

} // namespace kke
