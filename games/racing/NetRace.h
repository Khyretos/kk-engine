#pragma once

// Racing online (docs/NETWORKING.md, games/racing/README.md "Online"):
// what goes over the wire, apart from the game code, so both ends read
// and write it the same way.
//
//   Each car a person drives is a network player (NetModule; a second
//   person on the same screen is a local player in slot 1..). Its
//   NetPlayerState carries where the car is, and in `extra` its full
//   rotation, steering, revs, gear, health and race position, so every
//   other machine draws it (and bumps into it) where its driver has it.
//   The host decides the race: the track, the grid, the laps, when it
//   starts. Its CPU cars go to everyone as one event a few times a second.
//   A dent goes to everyone as an event, so every copy of a car has the
//   same crumpled wing.

#include "kke/net/Protocol.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace racing::netrace {

constexpr uint16_t kGameEventBase = 0x5200;           // Racing's own event kinds
constexpr uint16_t kEventSetup = kGameEventBase + 0;  // host -> all: the race (Setup)
constexpr uint16_t kEventFinish = kGameEventBase + 1; // a car's owner -> host -> all: over the line (Finish)
constexpr uint16_t kEventReady = kGameEventBase + 2;  // client -> host: built the race, these cars are on the grid (Ready)
constexpr uint16_t kEventGo = kGameEventBase + 3;     // host -> all: everyone's ready, the lights run (Ready, no cars)
constexpr uint16_t kEventCpu = kGameEventBase + 4;    // host -> all: where the CPU cars are (CpuCars)
constexpr uint16_t kEventHit = kGameEventBase + 5;    // a car's owner -> host -> all: a dent (Hit)

// A car as its driver's machine has it: enough for another to draw it
// and to put a solid copy of it on the track.
struct CarPose {
    glm::vec3 position{0.0f};     // the body's origin (between the wheels, on the road)
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    float speed = 0.0f;           // m/s forward
    float steer = 0.0f;           // -1..1 (right +)
    float rpm = 0.0f;             // 0..1 of the rev range
    int gear = 1;
    float health = 100.0f;        // 0 = totalled
    int lap = 0;
    float progress = 0.0f;        // m round the track (standings)
    bool finished = false, totalled = false, braking = false, handBrake = false;
    uint8_t smoke = 0;            // tyres sliding, one bit per wheel
};

kke::net::NetPlayerState toState(const CarPose& p);
CarPose fromState(const kke::net::NetPlayerState& s);

// Who drives which car. The host sends it for each race: every machine
// builds the same grid and starts the same lights.
struct Seat {
    uint8_t player = 0;           // network player id, or kCpu (the host's CPU cars)
    uint8_t slot = 0;             // grid slot = the car's index in the race
    uint8_t type = 0, kit = 0, paint = 0;
    uint8_t skill = 1;
    std::string name;
};
constexpr uint8_t kCpu = 255;
struct Setup {
    std::string track;            // the track's id (tracks/*.yaml: every machine has the same files)
    uint32_t round = 0;           // +1 each restart: new lights for everyone
    uint8_t laps = 5;
    uint8_t damage = 1;           // 0 off, 1 normal, 2 brutal
    std::vector<Seat> seats;
};
std::vector<uint8_t> encode(const Setup& s);
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes);

struct Finish {
    uint8_t slot = 0;
    uint32_t round = 0;
    float time = 0.0f;            // s since the green
};
std::vector<uint8_t> encode(const Finish& f);
std::optional<Finish> decodeFinish(const std::vector<uint8_t>& bytes);

struct Ready {
    uint32_t round = 0;
    std::vector<uint8_t> slots;   // the sender's cars
};
std::vector<uint8_t> encode(const Ready& r);
std::optional<Ready> decodeReady(const std::vector<uint8_t>& bytes);

// The host's CPU cars, a few times a second (the other machines move
// their copies toward these between messages).
struct CpuCar {
    uint8_t slot = 0;
    CarPose pose;
};
struct CpuCars {
    uint32_t round = 0;
    std::vector<CpuCar> cars;
};
std::vector<uint8_t> encode(const CpuCars& c);
std::optional<CpuCars> decodeCpuCars(const std::vector<uint8_t>& bytes);

// A dent: where on the car (car space), which way it was pushed, how deep.
struct Hit {
    uint8_t slot = 0;
    uint32_t round = 0;
    glm::vec3 point{0.0f}, direction{0.0f};
    float depth = 0.0f;           // m
};
std::vector<uint8_t> encode(const Hit& h);
std::optional<Hit> decodeHit(const std::vector<uint8_t>& bytes);

} // namespace racing::netrace
