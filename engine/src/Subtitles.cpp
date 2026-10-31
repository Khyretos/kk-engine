#include "kke/Subtitles.h"

#include <algorithm>

namespace kke {

Subtitles& Subtitles::get() {
    static Subtitles instance;
    return instance;
}

float Subtitles::readingTime(const std::string& text) {
    size_t characters = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++characters; // not a continuation byte: one character
    return std::clamp(1.2f + static_cast<float>(characters) / 17.0f, 1.5f, 12.0f);
}

int Subtitles::say(const std::string& text, const Options& options) {
    if (text.empty()) return 0;
    std::lock_guard lock(m_mutex);
    if (!m_enabled) return 0;
    const float duration = options.duration > 0.0f ? options.duration : readingTime(text);
    for (Line& l : m_lines)
        if (l.text == text && l.speaker == options.speaker && l.kind == options.kind) {
            l.duration = l.remaining = duration;
            ++m_revision;
            return l.id;
        }
    Line line;
    line.id = m_nextId++;
    line.speaker = options.speaker;
    line.text = text;
    line.kind = options.kind;
    line.duration = line.remaining = duration;
    m_lines.push_back(std::move(line));
    ++m_revision;
    return m_lines.back().id;
}

void Subtitles::remove(int id) {
    std::lock_guard lock(m_mutex);
    const size_t before = m_lines.size();
    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [id](const Line& l) { return l.id == id; }), m_lines.end());
    if (m_lines.size() != before) ++m_revision;
}

void Subtitles::clear() {
    std::lock_guard lock(m_mutex);
    if (m_lines.empty()) return;
    m_lines.clear();
    ++m_revision;
}

void Subtitles::update(float dt) {
    std::lock_guard lock(m_mutex);
    bool gone = false;
    for (Line& l : m_lines) l.remaining -= dt;
    const size_t before = m_lines.size();
    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [](const Line& l) { return l.remaining <= 0.0f; }), m_lines.end());
    gone = m_lines.size() != before;
    if (gone) ++m_revision;
}

std::vector<Subtitles::Line> Subtitles::lines() const {
    std::lock_guard lock(m_mutex);
    const size_t n = m_lines.size();
    const size_t first = n > static_cast<size_t>(m_maxLines) ? n - static_cast<size_t>(m_maxLines) : 0;
    return std::vector<Line>(m_lines.begin() + static_cast<std::ptrdiff_t>(first), m_lines.end());
}

void Subtitles::setEnabled(bool on) {
    std::lock_guard lock(m_mutex);
    if (m_enabled == on) return;
    m_enabled = on;
    if (!on) m_lines.clear();
    ++m_revision;
}

void Subtitles::setMaxLines(int n) {
    std::lock_guard lock(m_mutex);
    m_maxLines = std::clamp(n, 1, 8);
    ++m_revision;
}

} // namespace kke
