#include "NetRace.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace climb_race::netrace {

namespace {

namespace net = kke::net;

// NetPlayerState::state: kke::Locomotion::State on foot (0..6), or these.
constexpr uint8_t kStateClimbing = 8, kStateMantle = 9;
constexpr uint8_t kFlagFinished = 1u << 0;
// Hands and feet relative to the feet: +-4 m at 0.5 mm, 14 bits an axis
// (4 x 42 bits = 21 of NetPlayerState's 24 extra bytes).
constexpr float kLimbRange = 4.0f, kLimbStep = 1.0f / 2048.0f;
constexpr size_t kMaxSeats = 16;
constexpr size_t kMaxSeatName = kke::net::kMaxNameLength;

template <typename Stream> void serializeLimbs(Stream& s, glm::vec3 (&limbs)[4]) {
    for (glm::vec3& l : limbs) s.vec3(l, kLimbRange, kLimbStep);
}

template <typename Stream> void serialize(Stream& s, Setup& m) {
    s.integer(m.seed, 0, 0x7fffffff);
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.difficulty, 0, 7);
    uint32_t n = static_cast<uint32_t>(std::min(m.seats.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.seats.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.seats.size(); ++i) {
        Seat& seat = m.seats[i];
        s.integer(seat.player, 0, net::kMaxPlayers);
        s.integer(seat.lane, 0, kMaxSeats - 1);
        s.boolean(seat.cpu);
        s.string(seat.name, kMaxSeatName);
        s.vec3(seat.tint, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f / 255.0f);
    }
}
template <typename Stream> void serialize(Stream& s, Finish& m) {
    s.integer(m.player, 0, net::kMaxPlayers);
    s.integer(m.round, 0, 0x7fffffff);
    s.real(m.time, 0.0f, 3600.0f, 0.001f);
}
template <typename Stream> void serialize(Stream& s, Loose& m) {
    s.integer(m.lane, 0, kMaxSeats - 1);
    s.integer(m.hold, 0, 65535);
    s.vec3(m.push, 16.0f, 1.0f / 64.0f);
}

template <typename Stream> void serialize(Stream& s, Ready& m) {
    s.integer(m.round, 0, 0x7fffffff);
    uint32_t n = static_cast<uint32_t>(std::min(m.players.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.players.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.players.size(); ++i) s.integer(m.players[i], 0, net::kMaxPlayers);
}

template <typename Msg> std::vector<uint8_t> write(Msg m) {
    std::vector<uint8_t> out;
    {
        net::WriteStream w(out);
        serialize(w, m);
    }
    return out;
}
template <typename Msg> std::optional<Msg> read(const std::vector<uint8_t>& bytes) {
    net::ReadStream r(bytes.data(), bytes.size());
    Msg m{};
    serialize(r, m);
    if (!r.ok() || r.bitsLeft() >= 8) return std::nullopt;
    return m;
}

} // namespace

net::NetPlayerState toState(const Pose& p) {
    net::NetPlayerState s;
    s.position = p.feet;
    s.velocity = p.velocity;
    s.yaw = std::fmod(std::fmod(p.yaw, 360.0f) + 360.0f, 360.0f);
    s.state = p.mantle ? kStateMantle : p.climbing ? kStateClimbing : std::min<uint8_t>(p.loco, 7);
    s.flags = p.finished ? kFlagFinished : 0;
    s.speed = std::clamp(p.groundSpeed, 0.0f, 20.0f);
    s.progress = std::clamp(p.mantleProgress, 0.0f, 1.0f);
    s.aux = std::clamp(p.fallHeight, 0.0f, 32.0f);
    // Hands and feet whenever the IK may use them: on the rock, and on
    // foot too (a ledge hang puts the hands on the edge).
    glm::vec3 limbs[4] = { p.hand[0] - p.feet, p.hand[1] - p.feet, p.foot[0] - p.feet, p.foot[1] - p.feet };
    for (glm::vec3& l : limbs) l = glm::clamp(l, glm::vec3(-kLimbRange), glm::vec3(kLimbRange));
    {
        net::WriteStream w(s.extra);
        serializeLimbs(w, limbs);
    } // flushed
    return s;
}

Pose fromState(const net::NetPlayerState& s) {
    Pose p;
    p.feet = s.position;
    p.velocity = s.velocity;
    p.yaw = s.yaw;
    p.climbing = s.state == kStateClimbing || s.state == kStateMantle;
    p.mantle = s.state == kStateMantle;
    p.loco = p.climbing ? 0 : s.state;
    p.finished = (s.flags & kFlagFinished) != 0;
    p.groundSpeed = s.speed;
    p.mantleProgress = s.progress;
    p.fallHeight = s.aux;
    glm::vec3 limbs[4]{};
    net::ReadStream r(s.extra.data(), s.extra.size());
    serializeLimbs(r, limbs);
    if (!r.ok()) for (glm::vec3& l : limbs) l = glm::vec3(0.0f, 1.0f, 0.0f); // none sent: out of the way
    p.hand[0] = p.feet + limbs[0];
    p.hand[1] = p.feet + limbs[1];
    p.foot[0] = p.feet + limbs[2];
    p.foot[1] = p.feet + limbs[3];
    return p;
}

std::string tintText(const glm::vec3& tint) {
    char buf[8];
    const glm::ivec3 c = glm::ivec3(glm::round(glm::clamp(tint, 0.0f, 1.0f) * 255.0f));
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

glm::vec3 tintFromText(const std::string& text, const glm::vec3& fallback) {
    unsigned r = 0, g = 0, b = 0;
    if (text.size() != 7 || std::sscanf(text.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3) return fallback;
    return glm::vec3(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)) / 255.0f;
}

std::vector<uint8_t> encode(const Setup& s) { return write(s); }
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes) { return read<Setup>(bytes); }
std::vector<uint8_t> encode(const Finish& f) { return write(f); }
std::optional<Finish> decodeFinish(const std::vector<uint8_t>& bytes) { return read<Finish>(bytes); }
std::vector<uint8_t> encode(const Loose& l) { return write(l); }
std::vector<uint8_t> encode(const Ready& r) { return write(r); }
std::optional<Ready> decodeReady(const std::vector<uint8_t>& bytes) { return read<Ready>(bytes); }
std::optional<Loose> decodeLoose(const std::vector<uint8_t>& bytes) { return read<Loose>(bytes); }

} // namespace climb_race::netrace
