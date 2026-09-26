#include "kke/net/InputReplay.h"

#include <algorithm>
#include <cmath>

namespace kke::net {

namespace {
bool finite(const InputFrame& in) { return std::isfinite(in.move.x) && std::isfinite(in.move.y) && std::isfinite(in.yaw); }
} // namespace

InputFrame quantize(const InputFrame& in) {
    InputFrame q = in;
    glm::vec2 m = finite(in) ? in.move : glm::vec2(0.0f);
    const float len = glm::length(m);
    if (len > 1.0f) m /= len;
    // As serialize(InputMsg): each axis in 1/127 steps, yaw in 360/1023.
    q.move = glm::vec2(float(std::lround(m.x * 127.0f)), float(std::lround(m.y * 127.0f))) / 127.0f;
    float yaw = std::isfinite(in.yaw) ? std::fmod(in.yaw, 360.0f) : 0.0f;
    if (yaw < 0.0f) yaw += 360.0f;
    const float step = 360.0f / 1023.0f;
    q.yaw = std::min(360.0f, static_cast<float>(std::min(1023L, std::lround(yaw / step))) * step);
    return q;
}

// ---------------------------------------------------------------- server

bool InputQueue::push(const InputFrame& in) {
    if (!finite(in)) {
        ++dropped;
        return false;
    }
    if (m_started && static_cast<int32_t>(in.tick - m_last) <= 0) {
        ++dropped; // played or skipped already
        return false;
    }
    auto it = std::lower_bound(m_queue.begin(), m_queue.end(), in.tick,
                               [](const InputFrame& a, uint32_t t) { return static_cast<int32_t>(a.tick - t) < 0; });
    if (it != m_queue.end() && it->tick == in.tick) {
        ++dropped; // the same input again (they're resent until acknowledged)
        return false;
    }
    m_queue.insert(it, quantize(in));
    if (!m_started) {
        // Play from the first one that came, as if the one before it had been.
        m_last = m_queue.front().tick - 1;
        m_started = true;
    }
    while (m_queue.size() > maxBuffered) {
        // A press in a skipped input still happens, one tick later.
        const uint8_t presses = m_queue.front().buttons & kPressButtons;
        m_last = m_queue.front().tick;
        m_queue.pop_front();
        m_queue.front().buttons |= presses;
        ++skipped;
    }
    return true;
}

void InputQueue::beginTick() {
    if (!m_started) return;
    if (!m_playedThisTick) ++stalled;
    m_playedThisTick = false;
    m_credit = std::min(m_credit + 1, std::max<size_t>(1, maxCredit));
    // Waiting on a missing input while later ones are here: not forever.
    if (!m_queue.empty() && m_queue.front().tick != m_last + 1) ++m_gapTicks;
    else m_gapTicks = 0;
}

bool InputQueue::next(InputFrame& out) {
    if (!m_started || m_credit == 0 || m_queue.empty()) return false;
    const InputFrame& front = m_queue.front();
    if (front.tick != m_last + 1) {
        if (m_gapTicks <= gapWait) return false; // it may still come
        skipped += front.tick - (m_last + 1);
    }
    out = front;
    m_last = front.tick;
    m_queue.pop_front();
    m_gapTicks = 0;
    --m_credit;
    ++played;
    m_playedThisTick = true;
    return true;
}

// ---------------------------------------------------------------- client

Prediction::Prediction(Rewindable& sim, size_t history) : m_sim(sim), m_history(std::max<size_t>(history, 8)) {}

InputFrame Prediction::tick(InputFrame in, float dt) {
    in.tick = m_next++;
    in = quantize(in);
    m_sim.step(in, dt);
    const size_t slot = in.tick % m_history.size();
    m_sim.save(slot);
    const NetPlayerState now = m_sim.state();
    m_history[slot] = { in, dt, now.position, now.state };
    if (smoothing > 0.0f) m_offset *= std::exp(-dt / smoothing);
    else m_offset = glm::vec3(0.0f);
    return in;
}

bool Prediction::acknowledge(uint32_t tick, const NetPlayerState& server) {
    if (m_anyAck && static_cast<int32_t>(tick - m_acked) <= 0) return false; // old or reordered
    if (static_cast<int32_t>(m_next - tick) <= 0) return false;              // one we never made
    const size_t n = m_history.size();
    if (m_next - tick > n) return false;                                      // fell out of the history
    m_acked = tick;
    m_anyAck = true;
    const Entry& at = m_history[tick % n];
    if (at.input.tick != tick) return false;
    if (glm::length(at.position - server.position) <= tolerance && at.state == server.state) return false;

    // Rewind to the acknowledged tick, take the server's word, replay.
    const glm::vec3 before = m_sim.state().position;
    m_sim.load(tick % n);
    m_sim.correct(server);
    m_sim.save(tick % n);
    m_history[tick % n].position = server.position;
    m_history[tick % n].state = server.state;
    for (uint32_t t = tick + 1; t != m_next; ++t) {
        Entry& e = m_history[t % n];
        m_sim.step(e.input, e.dt);
        m_sim.save(t % n);
        const NetPlayerState s = m_sim.state();
        e.position = s.position;
        e.state = s.state;
        ++replayedTicks;
    }
    m_offset += before - m_sim.state().position;
    ++corrections;
    return true;
}

std::vector<InputFrame> Prediction::unacknowledged(size_t max) const {
    std::vector<InputFrame> out;
    const size_t n = m_history.size();
    uint32_t from = m_anyAck ? m_acked + 1 : 0;
    const uint32_t pending = m_next - from;
    if (pending > max) from = m_next - static_cast<uint32_t>(max);
    if (m_next - from > n) from = m_next - static_cast<uint32_t>(n);
    for (uint32_t t = from; t != m_next; ++t) out.push_back(m_history[t % n].input);
    return out;
}

} // namespace kke::net
