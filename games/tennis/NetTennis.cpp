#include "NetTennis.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tennis::net {

namespace {

namespace kn = kke::net;

constexpr size_t kMaxSeats = 8;
constexpr size_t kMaxCall = 40;
// Court space: a court's fence is inside +-18 m; 1 mm steps.
constexpr float kCourtRange = 32.0f, kCourtStep = 1.0f / 1024.0f;
constexpr float kSpeedRange = 80.0f, kSpeedStep = 1.0f / 512.0f;
constexpr float kSpinRange = 256.0f, kSpinStep = 1.0f / 32.0f;
// NetPlayerState::extra: the swing's contact in the body frame, +-4 m at 2 mm.
constexpr float kContactRange = 4.0f, kContactStep = 1.0f / 512.0f;
constexpr uint8_t kBackhand = 1u << 7; // NetPlayerState::state: the stroke, this bit a backhand
constexpr uint8_t kFlagSwinging = 1u << 0, kFlagTossing = 1u << 1, kFlagCheer = 1u << 2, kFlagCelebrating = 1u << 3;

template <typename Stream> void serialize(Stream& s, Seat& m) {
    s.integer(m.player, 0, 255);
    s.integer(m.team, 0, 1);
    s.integer(m.slot, 0, 1);
    s.boolean(m.cpu);
    s.string(m.name, kn::kMaxNameLength);
    s.vec3(m.tint, glm::vec3(0.0f), glm::vec3(2.0f), 1.0f / 255.0f);
}
template <typename Stream> void serialize(Stream& s, Setup& m) {
    s.bits(m.match, 32);
    s.integer(m.court, 0, 15);
    s.integer(m.teamSize, 1, 2);
    s.integer(m.gamesPerSet, 1, 12);
    s.integer(m.setsToWin, 1, 3);
    s.boolean(m.center);
    s.integer(m.point, 0, 65535);
    uint32_t points = static_cast<uint32_t>(std::min(m.history.size(), kMaxHistory));
    s.integer(points, 0, static_cast<int64_t>(kMaxHistory));
    if constexpr (Stream::kReading) m.history.resize(s.ok() ? points : 0);
    for (uint8_t& p : m.history) s.integer(p, 0, 1);
    uint32_t n = static_cast<uint32_t>(std::min(m.seats.size(), kMaxSeats));
    s.integer(n, 0, static_cast<int64_t>(kMaxSeats));
    if constexpr (Stream::kReading) m.seats.resize(s.ok() ? n : 0);
    for (Seat& seat : m.seats) serialize(s, seat);
}
template <typename Stream> void serialize(Stream& s, CpuPose& m) {
    s.integer(m.player, 0, 7);
    s.vec3(m.feet, kCourtRange, kCourtStep);
    s.real(m.velocity.x, -16.0f, 16.0f, 1.0f / 64.0f);
    s.real(m.velocity.y, -16.0f, 16.0f, 1.0f / 64.0f);
    s.real(m.yaw, -180.0f, 180.0f, 360.0f / 256.0f);
    uint8_t stroke = static_cast<uint8_t>(m.stroke);
    s.integer(stroke, 0, static_cast<int64_t>(Stroke::Count) - 1);
    if constexpr (Stream::kReading) m.stroke = static_cast<Stroke>(stroke);
    s.boolean(m.backhand);
    s.real(m.swingT, kSwingIdle, 1.0f, 4.0f / 255.0f);
    s.vec3(m.contact, kContactRange, 1.0f / 128.0f);
    s.boolean(m.tossing);
    s.boolean(m.celebrating);
    s.boolean(m.cheer);
}
template <typename Stream> void serialize(Stream& s, Cpus& m) {
    uint32_t n = static_cast<uint32_t>(std::min(m.matches.size(), kMaxMatches));
    s.integer(n, 0, static_cast<int64_t>(kMaxMatches));
    if constexpr (Stream::kReading) m.matches.resize(s.ok() ? n : 0);
    for (CpuMatch& cm : m.matches) {
        s.bits(cm.match, 32);
        uint32_t k = static_cast<uint32_t>(std::min(cm.players.size(), size_t{ 4 }));
        s.integer(k, 0, 4);
        if constexpr (Stream::kReading) cm.players.resize(s.ok() ? k : 0);
        for (CpuPose& p : cm.players) serialize(s, p);
    }
}
template <typename Stream> void serialize(Stream& s, Gate& m) {
    s.integer(m.court, 0, 15);
    s.integer(m.player, 0, 255);
    s.integer(m.action, 0, 2);
}
template <typename Stream> void serialize(Stream& s, Board& m) {
    s.boolean(m.center);
    for (size_t c = 0; c < Board::kCourts; ++c) {
        s.integer(m.waiting[c], 0, 7);
        // 0: none, else tenths of a second + 1.
        uint32_t cd = m.countdown[c] < 0.0f ? 0u : static_cast<uint32_t>(std::clamp(m.countdown[c], 0.0f, 12.0f) * 10.0f + 1.5f);
        s.integer(cd, 0, 121);
        if constexpr (Stream::kReading) m.countdown[c] = cd == 0 ? -1.0f : static_cast<float>(cd - 1) / 10.0f;
    }
    uint32_t n = static_cast<uint32_t>(std::min(m.wins.size(), Board::kWins));
    s.integer(n, 0, static_cast<int64_t>(Board::kWins));
    if constexpr (Stream::kReading) m.wins.resize(s.ok() ? n : 0);
    for (auto& w : m.wins) {
        s.string(w.first, kn::kMaxNameLength);
        s.integer(w.second, 0, 65535);
    }
}
template <typename Stream> void serialize(Stream& s, Serve& m) {
    s.bits(m.match, 32);
    s.integer(m.point, 0, 65535);
    s.boolean(m.again);
    s.boolean(m.second);
}
template <typename Stream> void serialize(Stream& s, Point& m) {
    s.bits(m.match, 32);
    s.integer(m.point, 0, 65535);
    s.integer(m.result, 0, 4);
    s.string(m.call, kMaxCall);
}
template <typename Stream> void serialize(Stream& s, Hit& m) {
    s.bits(m.match, 32);
    s.integer(m.point, 0, 65535);
    s.integer(m.player, 0, 255);
    s.integer(m.shot, 0, 255);
    s.boolean(m.serve);
    s.integer(m.kind, 0, 5);
    s.vec3(m.at, kCourtRange, kCourtStep);
    s.vec3(m.velocity, kSpeedRange, kSpeedStep);
    s.vec3(m.spin, kSpinRange, kSpinStep);
    s.real(m.pull, -16.0f, 16.0f, 1.0f / 256.0f);
    s.real(m.squash, 0.0f, 1.0f, 1.0f / 255.0f);
}
template <typename Stream> void serialize(Stream& s, Toss& m) {
    s.bits(m.match, 32);
    s.integer(m.point, 0, 65535);
    s.integer(m.player, 0, 255);
    s.vec3(m.at, kCourtRange, kCourtStep);
    s.vec3(m.velocity, kSpeedRange, kSpeedStep);
}
template <typename Stream> void serialize(Stream& s, BallState& m) {
    s.bits(m.match, 32);
    s.integer(m.point, 0, 65535);
    s.integer(m.shot, 0, 255);
    s.vec3(m.flight.pos, kCourtRange, kCourtStep);
    s.vec3(m.flight.vel, kSpeedRange, kSpeedStep);
    s.real(m.flight.gravity, 0.0f, 32.0f, 1.0f / 256.0f);
    s.boolean(m.rolling);
    if constexpr (Stream::kReading) m.flight.drag = kDrag;
}
template <typename Stream> void serialize(Stream& s, End& m) {
    s.bits(m.match, 32);
    s.string(m.why, kMaxCall + kn::kMaxNameLength);
}

template <typename Msg> std::vector<uint8_t> write(Msg m) {
    std::vector<uint8_t> out;
    {
        kn::WriteStream w(out);
        serialize(w, m);
    } // flushed
    return out;
}
template <typename Msg> std::optional<Msg> read(const std::vector<uint8_t>& bytes) {
    kn::ReadStream r(bytes.data(), bytes.size());
    Msg m{};
    serialize(r, m);
    if (!r.ok() || r.bitsLeft() >= 8) return std::nullopt;
    return m;
}

float yawOf(const glm::vec3& facing) {
    // Degrees 0..360, 0 = -Z, turning toward +X (the engine's convention).
    float deg = glm::degrees(std::atan2(facing.x, -facing.z));
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

} // namespace

kn::NetPlayerState toState(const Pose& p) {
    kn::NetPlayerState s;
    s.position = p.feet;
    s.velocity = p.velocity;
    s.yaw = yawOf(p.facing);
    s.state = static_cast<uint8_t>(static_cast<uint8_t>(p.stroke) | (p.backhand ? kBackhand : 0u));
    s.flags = static_cast<uint8_t>((p.swingT > kSwingIdle + 0.5f ? kFlagSwinging : 0) | (p.tossing ? kFlagTossing : 0) | (p.cheer ? kFlagCheer : 0) |
                                   (p.celebrating ? kFlagCelebrating : 0));
    s.speed = std::clamp(glm::length(glm::vec2(p.velocity.x, p.velocity.z)), 0.0f, 20.0f);
    s.progress = std::clamp((p.swingT + 2.0f) / 3.0f, 0.0f, 1.0f); // the takeback to the finish
    glm::vec3 contact = glm::clamp(p.contact, glm::vec3(-kContactRange), glm::vec3(kContactRange));
    {
        kn::WriteStream w(s.extra);
        w.vec3(contact, kContactRange, kContactStep);
    } // flushed
    return s;
}

Pose fromState(const kn::NetPlayerState& s) {
    Pose p;
    p.feet = s.position;
    p.velocity = s.velocity;
    const float yaw = glm::radians(s.yaw);
    p.facing = glm::vec3(std::sin(yaw), 0.0f, -std::cos(yaw));
    p.stroke = static_cast<Stroke>(std::min<uint8_t>(static_cast<uint8_t>(s.state & ~kBackhand), static_cast<uint8_t>(Stroke::Count) - 1));
    p.backhand = (s.state & kBackhand) != 0;
    p.swingT = (s.flags & kFlagSwinging) ? s.progress * 3.0f - 2.0f : kSwingIdle;
    p.tossing = (s.flags & kFlagTossing) != 0;
    p.cheer = (s.flags & kFlagCheer) != 0;
    p.celebrating = (s.flags & kFlagCelebrating) != 0;
    kn::ReadStream r(s.extra.data(), s.extra.size());
    r.vec3(p.contact, kContactRange, kContactStep);
    if (!r.ok()) p.contact = glm::vec3(0.6f, 1.0f, 0.4f);
    return p;
}

std::vector<uint8_t> encode(const Setup& s) { return write(s); }
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes) { return read<Setup>(bytes); }
std::vector<uint8_t> encode(const Serve& s) { return write(s); }
std::optional<Serve> decodeServe(const std::vector<uint8_t>& bytes) { return read<Serve>(bytes); }
std::vector<uint8_t> encode(const Point& p) { return write(p); }
std::optional<Point> decodePoint(const std::vector<uint8_t>& bytes) { return read<Point>(bytes); }
std::vector<uint8_t> encode(const Hit& h) { return write(h); }
std::optional<Hit> decodeHit(const std::vector<uint8_t>& bytes) { return read<Hit>(bytes); }
std::vector<uint8_t> encode(const Toss& t) { return write(t); }
std::optional<Toss> decodeToss(const std::vector<uint8_t>& bytes) { return read<Toss>(bytes); }
std::vector<uint8_t> encode(const BallState& b) { return write(b); }
std::optional<BallState> decodeBall(const std::vector<uint8_t>& bytes) { return read<BallState>(bytes); }
std::vector<uint8_t> encode(const End& e) { return write(e); }
std::optional<End> decodeEnd(const std::vector<uint8_t>& bytes) { return read<End>(bytes); }
std::vector<uint8_t> encode(const Cpus& c) { return write(c); }
std::optional<Cpus> decodeCpus(const std::vector<uint8_t>& bytes) { return read<Cpus>(bytes); }
std::vector<uint8_t> encode(const Gate& g) { return write(g); }
std::optional<Gate> decodeGate(const std::vector<uint8_t>& bytes) { return read<Gate>(bytes); }
std::vector<uint8_t> encode(const Board& b) { return write(b); }
std::optional<Board> decodeBoard(const std::vector<uint8_t>& bytes) { return read<Board>(bytes); }

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

} // namespace tennis::net
