#include "NetRace.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace climb_race::netrace {

namespace {

namespace net = kke::net;

// NetPlayerState::state: kke::Locomotion::State on foot (0..6), or these.
constexpr uint8_t kStateClimbing = 8, kStateMantle = 9;
constexpr uint8_t kFlagFinished = 1u << 0;
// NetPlayerState::extra (32 bytes): hands and feet relative to the feet,
// +-4 m at 1 mm (4 x 39 bits); the hips, +-2 m at 2 mm (33 bits); each
// hand's rock normal (octahedral, 2 x 7 bits), grip (4 bits) and whether
// it's on the rock and on a hold (2 bits). 229 bits, 29 bytes.
constexpr float kLimbRange = 4.0f, kLimbStep = 1.0f / 1024.0f;
constexpr float kHipsRange = 2.0f, kHipsStep = 1.0f / 512.0f;
constexpr size_t kMaxSeats = 16;
constexpr size_t kMaxSeatName = kke::net::kMaxNameLength;
constexpr size_t kMaxMountainText = 40;

// A unit vector as a point on the octahedron, unfolded onto a square.
glm::vec2 octEncode(const glm::vec3& n) {
    const float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (l1 < 1e-6f) return glm::vec2(0.0f);
    glm::vec2 p = glm::vec2(n.x, n.z) / l1;
    if (n.y < 0.0f) {
        const glm::vec2 folded = (1.0f - glm::abs(glm::vec2(p.y, p.x))) * glm::vec2(p.x >= 0.0f ? 1.0f : -1.0f, p.y >= 0.0f ? 1.0f : -1.0f);
        p = folded;
    }
    return p;
}
glm::vec3 octDecode(const glm::vec2& p) {
    glm::vec3 n(p.x, 1.0f - std::abs(p.x) - std::abs(p.y), p.y);
    if (n.y < 0.0f) {
        const float x = n.x, z = n.z;
        n.x = (1.0f - std::abs(z)) * (x >= 0.0f ? 1.0f : -1.0f);
        n.z = (1.0f - std::abs(x)) * (z >= 0.0f ? 1.0f : -1.0f);
    }
    const float len = glm::length(n);
    return len > 1e-6f ? n / len : glm::vec3(0.0f, 0.0f, 1.0f);
}

struct Limbs {
    glm::vec3 at[4]{};   // grips, feet: relative to the feet
    glm::vec3 hips{0.0f};
    glm::vec2 normal[2]{};
    float closed[2]{};
    bool onRock[2]{}, held[2]{};
};
template <typename Stream> void serialize(Stream& s, Limbs& m) {
    for (glm::vec3& l : m.at) s.vec3(l, kLimbRange, kLimbStep);
    s.vec3(m.hips, kHipsRange, kHipsStep);
    for (int h = 0; h < 2; ++h) {
        s.real(m.normal[h].x, -1.0f, 1.0f, 1.0f / 63.0f);
        s.real(m.normal[h].y, -1.0f, 1.0f, 1.0f / 63.0f);
        s.real(m.closed[h], 0.0f, 1.0f, 1.0f / 15.0f);
        s.boolean(m.onRock[h]);
        s.boolean(m.held[h]);
    }
}

// A float bit for bit (the rock generator must get the host's numbers exactly).
template <typename Stream> void exact(Stream& s, float& v) {
    uint32_t b = 0;
    if constexpr (Stream::kWriting) std::memcpy(&b, &v, sizeof(b));
    uint32_t lo = b & 0xffffu, hi = b >> 16;
    s.bits(lo, 16);
    s.bits(hi, 16);
    b = lo | (hi << 16);
    if constexpr (Stream::kReading) std::memcpy(&v, &b, sizeof(v));
}
template <typename Stream> void serialize(Stream& s, Mountain& m) {
    s.string(m.id, kMaxMountainText);
    s.string(m.name, kMaxMountainText);
    s.string(m.mood, kMaxMountainText);
    kke::ClimbWallDesc& d = m.desc;
    s.integer(d.seed, 0, 0x7fffffff);
    s.integer(d.ledges, 0, 15);
    for (float* f : { &d.height, &d.maxOverhang, &d.maxSlab, &d.density, &d.jugBias, &d.crimpBias, &d.looseChance, &d.routeStep, &m.medals[0],
                      &m.medals[1], &m.medals[2] })
        exact(s, *f);
}
template <typename Stream> void serialize(Stream& s, Setup& m) {
    serialize(s, m.mountain);
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.difficulty, 0, 7);
    s.integer(m.mode, 0, 7);
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
    Limbs l;
    const glm::vec3 at[4] = { p.grip[0], p.grip[1], p.foot[0], p.foot[1] };
    for (int i = 0; i < 4; ++i) l.at[i] = glm::clamp(at[i] - p.feet, glm::vec3(-kLimbRange), glm::vec3(kLimbRange));
    l.hips = glm::clamp(p.hips - p.feet, glm::vec3(-kHipsRange), glm::vec3(kHipsRange));
    for (int h = 0; h < 2; ++h) {
        l.normal[h] = octEncode(p.normal[h]);
        l.closed[h] = std::clamp(p.closed[h], 0.0f, 1.0f);
        l.onRock[h] = p.onRock[h];
        l.held[h] = p.held[h];
    }
    {
        net::WriteStream w(s.extra);
        serialize(w, l);
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
    Limbs l;
    net::ReadStream r(s.extra.data(), s.extra.size());
    serialize(r, l);
    if (!r.ok()) { // none sent: out of the way
        l = Limbs{};
        for (glm::vec3& a : l.at) a = glm::vec3(0.0f, 1.0f, 0.0f);
        l.hips = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    p.grip[0] = p.feet + l.at[0];
    p.grip[1] = p.feet + l.at[1];
    p.foot[0] = p.feet + l.at[2];
    p.foot[1] = p.feet + l.at[3];
    p.hips = p.feet + l.hips;
    for (int h = 0; h < 2; ++h) {
        p.normal[h] = octDecode(l.normal[h]);
        p.closed[h] = l.closed[h];
        p.onRock[h] = l.onRock[h];
        p.held[h] = l.held[h];
    }
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
