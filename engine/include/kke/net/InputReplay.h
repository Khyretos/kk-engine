#pragma once

#include "kke/net/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace kke::net {

// Server-side input replay (docs/NETWORKING.md "Input replay", #28), the
// model of competitive shooters: a client sends its inputs, not where its
// player is; the server runs every player's movement from those inputs,
// so its players are exactly as fast as the game allows. The client still
// moves its own player the moment you press (prediction), keeps what it
// did, and when the server's answer for an old input disagrees, it goes
// back to that input, takes the server's position and replays every input
// since (rewind and replay). With a fair client and the same level both
// sides agree and nothing is ever corrected.
//
//   Client: Prediction::tick(input) each fixed tick, NetClient::sendInputs
//           with the unacknowledged ones; NetClient::onInputAck ->
//           Prediction::acknowledge.
//   Server: NetServer::nextInput each fixed tick per player, step that
//           player's movement, NetServer::setPlayerState with the result.
//
// The movement is the game's, behind Rewindable (kke::Locomotion's is
// kke/net/LocomotionReplay.h). Pure logic: tests run it on fakes.

// InputFrame (one tick of what a player asked for) is in Protocol.h.
// Button bits the engine's movement understands (16..128 are the game's).
constexpr uint8_t kButtonFast = 1u << 0;    // sprint
constexpr uint8_t kButtonSlow = 1u << 1;    // walk
constexpr uint8_t kButtonCrouch = 1u << 2;
constexpr uint8_t kButtonUp = 1u << 3;      // pressed this tick: jump / vault / climb
// Presses rather than holds: an input the server has to repeat (the real
// one was lost) never repeats them.
constexpr uint8_t kPressButtons = kButtonUp;

// The input exactly as the other side will see it (the wire's precision):
// the client steps with this, so both sides run the same numbers.
InputFrame quantize(const InputFrame& in);

// A player's movement that can go back in time.
class Rewindable {
public:
    virtual ~Rewindable() = default;
    // One fixed tick of movement from `in`.
    virtual void step(const InputFrame& in, float dt) = 0;
    // Keep the state as it is now in history slot `slot`, or go back to it.
    virtual void save(size_t slot) = 0;
    virtual void load(size_t slot) = 0;
    // What others see of it (and what the client compares).
    virtual NetPlayerState state() const = 0;
    // The server says it was like this (where, how fast, which movement
    // state): take that.
    virtual void correct(const NetPlayerState& server) = 0;
};

// Server: one player's inputs, played in the order they were made, at
// most one per server tick on average. Each server tick (beginTick) earns
// the player one input; an input not here yet is waited for (the player
// stands still on the server a moment, nothing is guessed), and when late
// ones come in a bunch the saved-up ticks play them (up to maxCredit), so
// the server runs exactly the inputs the client predicted with and a
// laggy connection costs latency, not corrections. A client can't get
// more than that however fast it sends: a sped-up clock only fills the
// queue, whose oldest inputs are then skipped (their presses carry over
// to the next, so a jump is never lost). An input lost for good is waited
// for `gapWait` ticks, then skipped.
class InputQueue {
public:
    size_t maxBuffered = 12;  // inputs waiting; past that the oldest are skipped (latency)
    size_t maxCredit = 8;     // ticks a player who fell behind may catch up on
    size_t gapWait = 3;       // server ticks to wait for a missing input before skipping it

    // False: dropped (older than the last played, a duplicate, or not finite).
    bool push(const InputFrame& in);
    // Once per server tick, before next().
    void beginTick();
    // The next input to play now, if one may be: call until false.
    bool next(InputFrame& out);
    bool started() const { return m_started; }
    // The tick of the last input played: what the server's state is "after".
    uint32_t lastPlayed() const { return m_last; }
    size_t buffered() const { return m_queue.size(); }

    size_t played = 0;
    size_t stalled = 0;       // server ticks the player waited for its input
    size_t skipped = 0;       // inputs never played (too many waiting, or lost)
    size_t dropped = 0;       // arrived after they were played or skipped, or twice

private:
    std::deque<InputFrame> m_queue; // ticks ascending, all > m_last
    uint32_t m_last = 0;
    bool m_started = false;
    size_t m_credit = 0;
    size_t m_gapTicks = 0;
    bool m_playedThisTick = true; // no tick before the first to have stalled
};

// Client: runs the local player ahead of the server and puts it right
// when the server disagrees.
class Prediction {
public:
    explicit Prediction(Rewindable& sim, size_t history = 128);

    // One fixed tick: `in` (tick number filled in, quantized) moves the
    // player now. Returns the input as sent.
    InputFrame tick(InputFrame in, float dt);
    // The server's state after our input `tick`. If ours was more than
    // `tolerance` away, or in another movement state (NetPlayerState::
    // state: a vault the server didn't do): back to then, the server's
    // version, and every input since replayed. True when that happened.
    bool acknowledge(uint32_t tick, const NetPlayerState& server);
    // Inputs the server hasn't acknowledged, oldest first, the newest `max`
    // (sent every tick: a lost packet's inputs ride in the next).
    std::vector<InputFrame> unacknowledged(size_t max) const;

    // A correction moves the player at once; drawing it at position +
    // visualOffset() hides the jump (the offset fades over `smoothing` s).
    glm::vec3 visualOffset() const { return m_offset; }
    uint32_t nextTick() const { return m_next; }
    uint32_t lastAcknowledged() const { return m_acked; }

    float tolerance = 0.05f;  // m between ours and the server's before rewinding
    float smoothing = 0.15f;  // s for a correction's visual offset to fade
    size_t corrections = 0;
    size_t replayedTicks = 0;

private:
    struct Entry {
        InputFrame input;
        float dt = 0.0f;
        glm::vec3 position{0.0f}; // after this input
        uint8_t state = 0;
    };
    Rewindable& m_sim;
    std::vector<Entry> m_history;   // ring by tick
    uint32_t m_next = 0;            // tick number of the next input
    uint32_t m_acked = 0;
    bool m_anyAck = false;
    glm::vec3 m_offset{0.0f};
};

} // namespace kke::net
