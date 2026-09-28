#include "FlyNet.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace flying::net {

namespace {

namespace kn = kke::net;

constexpr uint8_t kFlagSmoke = 1u << 0, kFlagCrashed = 1u << 1, kFlagGround = 1u << 2, kFlagFinished = 1u << 3;
constexpr size_t kMaxSeats = 32;
constexpr size_t kMaxMood = 32;

// NetPlayerState::extra: the attitude (smallest three, 35 bits), throttle
// (6), next ring (6), lap (3), finish time (to 1/100 s, 19), score (16),
// round (8). 93 bits, 12 bytes.
struct Extra {
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float throttle = 0.0f;
    uint8_t nextRing = 0, lap = 0, round = 0;
    float finishTime = 0.0f;
    uint32_t score = 0;
};
template <typename Stream> void serialize(Stream& s, Extra& e) {
    s.quat(e.rotation);
    s.real(e.throttle, 0.0f, 1.0f, 1.0f / 63.0f);
    s.integer(e.nextRing, 0, 63);
    s.integer(e.lap, 0, 7);
    s.real(e.finishTime, 0.0f, 5000.0f, 0.01f);
    s.integer(e.score, 0, 65535);
    s.integer(e.round, 0, 255);
}

template <typename Stream> void serialize(Stream& s, Seat& seat) {
    s.integer(seat.player, 0, kn::kMaxPlayers);
    s.integer(seat.slot, 0, 63);
    s.boolean(seat.cpu);
    s.integer(seat.skill, 0, 7);
    s.integer(seat.livery, 0, 7);
    s.string(seat.name, kn::kMaxNameLength);
    s.real(seat.tint.r, 0.0f, 1.0f, 1.0f / 255.0f);
    s.real(seat.tint.g, 0.0f, 1.0f, 1.0f / 255.0f);
    s.real(seat.tint.b, 0.0f, 1.0f, 1.0f / 255.0f);
}

template <typename Stream> void serialize(Stream& s, Setup& m) {
    s.integer(m.seed, 0, 0x7fffffff);
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.mode, 0, 7);
    s.integer(m.laps, 1, 7);
    s.integer(m.rings, 3, 63);
    s.real(m.ringRadius, 4.0f, 40.0f, 0.25f);
    s.string(m.mood, kMaxMood);
    uint32_t n = static_cast<uint32_t>(std::min(m.seats.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.seats.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.seats.size(); ++i) serialize(s, m.seats[i]);
}

} // namespace

kke::net::NetPlayerState toState(const Plane& p) {
    kn::NetPlayerState s;
    s.position = p.position;
    s.velocity = glm::clamp(p.velocity, glm::vec3(-kn::kMaxVelocity), glm::vec3(kn::kMaxVelocity));
    const glm::vec3 fwd = p.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    float yaw = glm::degrees(std::atan2(fwd.x, -fwd.z));
    if (yaw < 0.0f) yaw += 360.0f;
    s.yaw = yaw;
    s.speed = std::min(glm::length(p.velocity) / 5.0f, 20.0f); // m/s / 5: the field tops out at 20
    s.flags = static_cast<uint8_t>((p.smoke ? kFlagSmoke : 0) | (p.crashed ? kFlagCrashed : 0) | (p.onGround ? kFlagGround : 0) |
                                   (p.finished ? kFlagFinished : 0) | (p.teleported ? kn::kPlayerTeleported : 0));
    Extra e;
    e.rotation = glm::normalize(p.rotation);
    e.throttle = std::clamp(p.throttle, 0.0f, 1.0f);
    e.nextRing = std::min<uint8_t>(p.nextRing, 63);
    e.lap = std::min<uint8_t>(p.lap, 7);
    e.finishTime = std::clamp(p.finishTime, 0.0f, 5000.0f);
    e.score = std::min<uint32_t>(p.score, 65535);
    e.round = p.round;
    {
        kn::WriteStream w(s.extra);
        serialize(w, e);
    } // flushed
    return s;
}

Plane fromState(const kke::net::NetPlayerState& s) {
    Plane p;
    p.position = s.position;
    p.velocity = s.velocity;
    p.smoke = (s.flags & kFlagSmoke) != 0;
    p.crashed = (s.flags & kFlagCrashed) != 0;
    p.onGround = (s.flags & kFlagGround) != 0;
    p.finished = (s.flags & kFlagFinished) != 0;
    p.teleported = (s.flags & kn::kPlayerTeleported) != 0;
    Extra e;
    kn::ReadStream r(s.extra.data(), s.extra.size());
    serialize(r, e);
    if (!r.ok()) {
        // Nothing sent yet: level, the way it's heading.
        e = Extra{};
        e.rotation = glm::angleAxis(glm::radians(-s.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    p.rotation = e.rotation;
    p.throttle = e.throttle;
    p.nextRing = e.nextRing;
    p.lap = e.lap;
    p.finishTime = e.finishTime;
    p.score = e.score;
    p.round = e.round;
    return p;
}

std::vector<uint8_t> encode(const Setup& s) {
    Setup copy = s;
    std::vector<uint8_t> out;
    {
        kn::WriteStream w(out);
        serialize(w, copy);
    }
    return out;
}

std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes) {
    kn::ReadStream r(bytes.data(), bytes.size());
    Setup m;
    serialize(r, m);
    if (!r.ok() || r.bitsLeft() >= 8) return std::nullopt;
    return m;
}

std::string tintText(const glm::vec3& tint) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(tint.r), b(tint.g), b(tint.b));
    return buf;
}

glm::vec3 tintFromText(const std::string& text, const glm::vec3& fallback) {
    if (text.size() != 7 || text[0] != '#') return fallback;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str() + 1, &end, 16);
    if (!end || *end != '\0') return fallback;
    return glm::vec3(static_cast<float>((v >> 16) & 0xff), static_cast<float>((v >> 8) & 0xff), static_cast<float>(v & 0xff)) / 255.0f;
}

} // namespace flying::net
