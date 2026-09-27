#pragma once

// Climb Race online (docs/NETWORKING.md, games/climb_race/README.md
// "Online"): what goes over the wire, apart from the game code, so both
// ends read and write it the same way.
//
//   Each racer a person plays is a network player (NetModule; a second
//   person on the same screen is a local player in slot 1..). Its
//   NetPlayerState carries where it is and which way it faces, and in
//   `extra` where its hands and feet are, so everyone else's copy puts
//   them on the same holds.
//   The host decides the race: which mountain, who races on which face,
//   when it starts. Events carry that, and each finish.

#include "kke/net/Protocol.h"

#include "Mountains.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace climb_race::netrace {

constexpr uint16_t kGameEventBase = 0x4300;           // Climb Race's own event kinds
constexpr uint16_t kEventSetup = kGameEventBase + 0;  // host -> all: the race (Setup)
constexpr uint16_t kEventFinish = kGameEventBase + 1; // a racer's owner -> host -> all: topped out (Finish)
constexpr uint16_t kEventLoose = kGameEventBase + 2;  // a racer's owner -> host -> all: a loose hold came off (Loose)
constexpr uint16_t kEventReady = kGameEventBase + 3;  // client -> host: built the race, these racers are at the line (Ready)
constexpr uint16_t kEventGo = kGameEventBase + 4;     // host -> all: everyone's ready, the countdown runs (Ready, no players)

// A racer as its owner sees it, enough for another machine to draw it.
struct Pose {
    glm::vec3 feet{0.0f};
    float yaw = 0.0f;             // degrees, 0 = -Z
    uint8_t loco = 0;             // kke::Locomotion::State on foot
    bool climbing = false;        // on the rock (kke::Climber)
    bool mantle = false;          // ... going over an edge
    bool finished = false;
    float mantleProgress = 0.0f;  // 0..1
    float groundSpeed = 0.0f;     // m/s, for the run blend
    float fallHeight = 0.0f;      // m, for the landing
    glm::vec3 velocity{0.0f};
    // Where the IK puts the limbs (world; on the rock, and the hands on a
    // ledge hang): as ClimbRaceModule::BodyInput has them.
    glm::vec3 grip[2]{}, normal[2]{}, foot[2]{}, hips{0.0f};
    float closed[2]{};
    bool onRock[2]{}, held[2]{};
};

kke::net::NetPlayerState toState(const Pose& p);
Pose fromState(const kke::net::NetPlayerState& s);

// Who races where. The host sends it whenever it changes (someone joined
// or left, a new mountain, a restart): every machine then builds the same
// faces and starts the same countdown.
struct Seat {
    uint8_t player = 0;           // network player id (the host's CPU racers too)
    uint8_t lane = 0;
    bool cpu = false;
    std::string name;
    glm::vec3 tint{1.0f};
};
struct Setup {
    // The mountain, whole (its generator knobs exactly, so every machine
    // builds the same rock even from a mountain file only the host has).
    Mountain mountain;
    uint32_t round = 0;           // +1 each restart: a new countdown for everyone
    uint8_t difficulty = 1;       // the host's CPU racers (display only)
    std::vector<Seat> seats;
};
std::vector<uint8_t> encode(const Setup& s);
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes);

struct Finish {
    uint8_t player = 0;
    uint32_t round = 0;
    float time = 0.0f;            // s
};
std::vector<uint8_t> encode(const Finish& f);
std::optional<Finish> decodeFinish(const std::vector<uint8_t>& bytes);

struct Loose {
    uint8_t lane = 0;
    uint16_t hold = 0;
    glm::vec3 push{0.0f};
};
std::vector<uint8_t> encode(const Loose& l);
std::optional<Loose> decodeLoose(const std::vector<uint8_t>& bytes);

// Loading a race takes each machine its own time: the countdown waits
// until every machine said it's ready (or a few seconds passed).
struct Ready {
    uint32_t round = 0;
    std::vector<uint8_t> players; // the sender's racers
};
std::vector<uint8_t> encode(const Ready& r);
std::optional<Ready> decodeReady(const std::vector<uint8_t>& bytes);

// A climber's colour as the "character" every player joins with ("#5aa6ff").
std::string tintText(const glm::vec3& tint);
glm::vec3 tintFromText(const std::string& text, const glm::vec3& fallback);

} // namespace climb_race::netrace
