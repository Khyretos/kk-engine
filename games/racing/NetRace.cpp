#include "NetRace.h"

#include "kke/net/BitStream.h"

#include <algorithm>
#include <cmath>

namespace racing::netrace {

namespace {

namespace net = kke::net;

// NetPlayerState::state: the gear + 1 (0 reverse, 1 neutral, 2.. forward).
// flags: these bits.
constexpr uint8_t kFlagFinished = 1u << 0, kFlagTotalled = 1u << 1, kFlagBraking = 1u << 2, kFlagHandBrake = 1u << 3;
constexpr size_t kMaxSeats = 32;
constexpr size_t kMaxTrackId = 40;
constexpr size_t kMaxCars = 32;
constexpr float kMaxProgress = 65536.0f; // m: 40 laps of a 1.6 km oval

// NetPlayerState::extra (32 bytes): the rotation (smallest three, 35
// bits), forward speed (0..128 m/s, 1/16), steering (7 bits), revs (7),
// health (7), lap (7), progress round the track (1/8 m, 19 bits), which
// tyres smoke (4), wheels torn off (4). 119 bits, 15 bytes.
struct Extra {
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float speed = 0.0f, steer = 0.0f, rpm = 0.0f, health = 100.0f, progress = 0.0f;
    uint32_t lap = 0, smoke = 0, detached = 0;
};
template <typename Stream> void serialize(Stream& s, Extra& m) {
    s.quat(m.rotation);
    s.real(m.speed, -16.0f, 112.0f, 1.0f / 16.0f);
    s.real(m.steer, -1.0f, 1.0f, 1.0f / 63.0f);
    s.real(m.rpm, 0.0f, 1.0f, 1.0f / 127.0f);
    s.real(m.health, 0.0f, 100.0f, 100.0f / 127.0f);
    s.integer(m.lap, 0, 127);
    s.real(m.progress, 0.0f, kMaxProgress, 1.0f / 8.0f);
    s.integer(m.smoke, 0, 15);
    s.integer(m.detached, 0, 15);
}

template <typename Stream> void serialize(Stream& s, CarPose& p) {
    // The CPU cars' event: the same numbers as a player's state.
    s.vec3(p.position, glm::vec3(-net::kWorldXZ, net::kWorldYMin, -net::kWorldXZ), glm::vec3(net::kWorldXZ, net::kWorldYMax, net::kWorldXZ),
           net::kPositionStep);
    s.vec3(p.velocity, 96.0f, 1.0f / 64.0f);
    Extra e{ p.rotation, p.speed, p.steer, p.rpm, p.health, p.progress, static_cast<uint32_t>(std::clamp(p.lap, 0, 127)), p.smoke, p.detached };
    serialize(s, e);
    uint32_t gear = static_cast<uint32_t>(std::clamp(p.gear + 1, 0, 15));
    s.integer(gear, 0, 15);
    s.boolean(p.finished);
    s.boolean(p.totalled);
    s.boolean(p.braking);
    s.boolean(p.handBrake);
    if constexpr (Stream::kReading) {
        p.rotation = e.rotation;
        p.speed = e.speed;
        p.steer = e.steer;
        p.rpm = e.rpm;
        p.health = e.health;
        p.progress = e.progress;
        p.lap = static_cast<int>(e.lap);
        p.smoke = static_cast<uint8_t>(e.smoke);
        p.detached = static_cast<uint8_t>(e.detached);
        p.gear = static_cast<int>(gear) - 1;
    }
}

template <typename Stream> void serialize(Stream& s, Setup& m) {
    s.string(m.track, kMaxTrackId);
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.laps, 1, 99);
    s.integer(m.damage, 0, 3);
    uint32_t n = static_cast<uint32_t>(std::min(m.seats.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.seats.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.seats.size(); ++i) {
        Seat& seat = m.seats[i];
        s.integer(seat.player, 0, 255);
        s.integer(seat.slot, 0, kMaxSeats - 1);
        s.integer(seat.type, 0, 15);
        s.integer(seat.kit, 0, 15);
        s.integer(seat.paint, 0, 63);
        s.integer(seat.skill, 0, 7);
        s.string(seat.name, net::kMaxNameLength);
    }
}
template <typename Stream> void serialize(Stream& s, Finish& m) {
    s.integer(m.slot, 0, kMaxSeats - 1);
    s.integer(m.round, 0, 0x7fffffff);
    s.real(m.time, 0.0f, 3600.0f, 0.001f);
}
template <typename Stream> void serialize(Stream& s, Ready& m) {
    s.integer(m.round, 0, 0x7fffffff);
    uint32_t n = static_cast<uint32_t>(std::min(m.slots.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.slots.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.slots.size(); ++i) s.integer(m.slots[i], 0, kMaxSeats - 1);
}
template <typename Stream> void serialize(Stream& s, CpuCars& m) {
    s.integer(m.round, 0, 0x7fffffff);
    uint32_t n = static_cast<uint32_t>(std::min(m.cars.size(), kMaxCars));
    s.integer(n, 0, kMaxCars);
    if constexpr (Stream::kReading) m.cars.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.cars.size(); ++i) {
        s.integer(m.cars[i].slot, 0, kMaxSeats - 1);
        serialize(s, m.cars[i].pose);
    }
}
template <typename Stream> void serialize(Stream& s, Hit& m) {
    s.integer(m.slot, 0, kMaxSeats - 1);
    s.integer(m.round, 0, 0x7fffffff);
    s.vec3(m.point, 4.0f, 1.0f / 256.0f);
    s.vec3(m.direction, 1.0f, 1.0f / 127.0f);
    s.real(m.depth, 0.0f, 0.5f, 1.0f / 1024.0f);
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

net::NetPlayerState toState(const CarPose& p) {
    net::NetPlayerState s;
    s.position = p.position;
    s.velocity = glm::clamp(p.velocity, glm::vec3(-net::kMaxVelocity), glm::vec3(net::kMaxVelocity));
    const glm::vec3 fwd = p.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    const float yaw = glm::degrees(std::atan2(fwd.x, fwd.z));
    s.yaw = std::fmod(yaw + 360.0f, 360.0f);
    s.state = static_cast<uint8_t>(std::clamp(p.gear + 1, 0, 15));
    s.flags = static_cast<uint8_t>((p.finished ? kFlagFinished : 0) | (p.totalled ? kFlagTotalled : 0) | (p.braking ? kFlagBraking : 0) |
                                   (p.handBrake ? kFlagHandBrake : 0));
    s.speed = std::clamp(std::fabs(p.speed), 0.0f, 20.0f);
    s.progress = std::clamp(p.rpm, 0.0f, 1.0f);
    Extra e{ p.rotation, p.speed, p.steer, p.rpm, p.health, std::clamp(p.progress, 0.0f, kMaxProgress),
             static_cast<uint32_t>(std::clamp(p.lap, 0, 127)), p.smoke, p.detached };
    {
        net::WriteStream w(s.extra);
        serialize(w, e);
    } // flushed
    return s;
}

CarPose fromState(const net::NetPlayerState& s) {
    CarPose p;
    p.position = s.position;
    p.velocity = s.velocity;
    p.gear = static_cast<int>(s.state) - 1;
    p.finished = (s.flags & kFlagFinished) != 0;
    p.totalled = (s.flags & kFlagTotalled) != 0;
    p.braking = (s.flags & kFlagBraking) != 0;
    p.handBrake = (s.flags & kFlagHandBrake) != 0;
    Extra e;
    net::ReadStream r(s.extra.data(), s.extra.size());
    serialize(r, e);
    if (!r.ok()) { // none sent: level, facing the way it moves
        e = Extra{};
        e.rotation = glm::angleAxis(glm::radians(s.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        e.speed = s.speed;
        e.rpm = s.progress;
    }
    p.rotation = e.rotation;
    p.speed = e.speed;
    p.steer = e.steer;
    p.rpm = e.rpm;
    p.health = e.health;
    p.lap = static_cast<int>(e.lap);
    p.progress = e.progress;
    p.smoke = static_cast<uint8_t>(e.smoke);
    p.detached = static_cast<uint8_t>(e.detached);
    return p;
}

std::vector<uint8_t> encode(const Setup& s) { return write(s); }
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes) { return read<Setup>(bytes); }
std::vector<uint8_t> encode(const Finish& f) { return write(f); }
std::optional<Finish> decodeFinish(const std::vector<uint8_t>& bytes) { return read<Finish>(bytes); }
std::vector<uint8_t> encode(const Ready& r) { return write(r); }
std::optional<Ready> decodeReady(const std::vector<uint8_t>& bytes) { return read<Ready>(bytes); }
std::vector<uint8_t> encode(const CpuCars& c) { return write(c); }
std::optional<CpuCars> decodeCpuCars(const std::vector<uint8_t>& bytes) { return read<CpuCars>(bytes); }
std::vector<uint8_t> encode(const Hit& h) { return write(h); }
std::optional<Hit> decodeHit(const std::vector<uint8_t>& bytes) { return read<Hit>(bytes); }

} // namespace racing::netrace
