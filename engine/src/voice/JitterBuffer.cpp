#include "kke/voice/JitterBuffer.h"

#include <algorithm>
#include <cmath>

namespace kke::voice {

void JitterBuffer::reset() {
    m_frames.clear();
    m_haveBase = false;
    m_playing = false;
    m_concealedRun = 0;
}

void JitterBuffer::push(uint16_t seq, std::vector<uint8_t> data) {
    if (data.empty()) return;
    int64_t u;
    if (!m_haveBase) {
        u = seq;
        m_haveBase = true;
        m_next = u;
    } else {
        u = m_lastUnwrapped + diff(seq, m_lastSeq);
    }
    if (u > m_lastUnwrapped || m_frames.empty()) {
        m_lastUnwrapped = u;
        m_lastSeq = seq;
    }
    if (u < m_next) {
        if (m_playing) { // its turn has passed
            ++m_late;
            return;
        }
        m_next = u; // still gathering the cushion: an earlier frame just came second
    }
    // Far ahead of what plays: the speaker went quiet and started again
    // (or restarted); start from here rather than wait for the gap.
    if (u - m_next > m_settings.maxFrames * 4) {
        m_frames.clear();
        m_next = u;
        m_playing = false;
    }
    m_frames.emplace(u, std::move(data)); // a duplicate keeps the first
    // Too much queued (a burst after a stall): drop the oldest to keep latency.
    while (static_cast<int>(m_frames.size()) > m_settings.maxFrames) {
        m_next = m_frames.begin()->first + 1;
        m_frames.erase(m_frames.begin());
        ++m_skipped;
    }
    if (!m_frames.empty() && m_frames.begin()->first > m_next && !m_playing) m_next = m_frames.begin()->first;
}

JitterBuffer::Out JitterBuffer::pop() {
    Out out;
    if (!m_playing) {
        if (static_cast<int>(m_frames.size()) < m_settings.startFrames) return out;
        m_playing = true;
        m_next = m_frames.begin()->first;
        m_concealedRun = 0;
    }
    auto it = m_frames.find(m_next);
    out.seq = static_cast<uint16_t>(m_next);
    if (it != m_frames.end()) {
        out.kind = Kind::Frame;
        out.data = std::move(it->second);
        m_frames.erase(it);
        m_concealedRun = 0;
        ++m_next;
        return out;
    }
    if (m_frames.empty() || ++m_concealedRun > m_settings.maxConcealed) {
        // The talk spurt is over (or the rest is too far off): wait for a new cushion.
        m_playing = false;
        m_concealedRun = 0;
        out.kind = Kind::Nothing;
        return out;
    }
    out.kind = Kind::Lost;
    ++m_lost;
    auto nx = m_frames.find(m_next + 1);
    if (nx != m_frames.end()) out.next = &nx->second;
    ++m_next;
    return out;
}

bool VoiceActivity::process(const float* samples, size_t count, float seconds) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) sum += double(samples[i]) * double(samples[i]);
    const float rms = count ? float(std::sqrt(sum / double(count))) : 0.0f;
    m_levelDb = 20.0f * std::log10(std::max(rms, 1e-5f));
    // The floor falls fast to quiet and rises slowly (speech doesn't pull it up).
    if (m_levelDb < m_noiseDb) m_noiseDb += (m_levelDb - m_noiseDb) * 0.5f;
    else m_noiseDb += (m_levelDb - m_noiseDb) * std::min(1.0f, seconds * 0.2f);
    const bool loud = m_levelDb > minLevelDb && m_levelDb > m_noiseDb + thresholdDb;
    if (loud) m_open = hangover;
    else m_open = std::max(0.0f, m_open - seconds);
    return loud || m_open > 0.0f;
}

} // namespace kke::voice
