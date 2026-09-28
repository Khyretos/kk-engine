#pragma once

// The Flying demo online (README.md "Online", docs/NETWORKING.md): what
// goes over the wire, apart from the game code, so both ends read and
// write it the same way (tests/test_flight.cpp checks the round trip).
//
//   Every plane a person flies is a network player (NetModule; a second
//   person at the same screen is a local player in slot 1..). Its
//   NetPlayerState carries where it is and how fast it goes; `extra`
//   carries its attitude (a quaternion), throttle, smoke, and how far it
//   is round the course or how many stunt points it has, so every screen
//   can show the same standings without any more messages.
//   The host decides the flight: which island, which mode, how many laps,
//   who starts where. One Setup event carries that and starts everyone's
//   countdown.
//
// Which controller or flight stick flies which plane never goes over the
// wire: that is each screen's own business (README.md "Controllers").

#include "kke/net/Protocol.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flying::net {

constexpr uint16_t kGameEventBase = 0x4600;           // the Flying demo's own event kinds
constexpr uint16_t kEventSetup = kGameEventBase + 0;  // host -> all: the flight (Setup)

// A plane as its owner sees it, enough for another screen to draw it
// and rank it.
struct Plane {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float throttle = 0.0f;       // 0..1 (the propeller and the engine's note)
    bool smoke = false, crashed = false, onGround = false, finished = false;
    bool teleported = false;     // a respawn: don't slide from where it was
    uint8_t nextRing = 0;        // Race: the ring it's flying to (0..63)
    uint8_t lap = 0;             // Race: laps done (0..7)
    float finishTime = 0.0f;     // Race: s, once finished
    uint32_t score = 0;          // Stunts: points (0..65535)
    uint8_t round = 0;           // the flight it belongs to (the host's round, low byte)
};

kke::net::NetPlayerState toState(const Plane& p);
Plane fromState(const kke::net::NetPlayerState& s);

struct Seat {
    uint8_t player = 0;          // network player id (the host's CPU pilots too)
    uint8_t slot = 0;            // start position
    bool cpu = false;
    uint8_t skill = 1;           // CPU pilots: the lobby's difficulty
    uint8_t livery = 0;          // which paint (0..3)
    std::string name;
    glm::vec3 tint{1.0f};        // smoke and HUD colour
};

struct Setup {
    uint32_t seed = 1;           // the island (and its rings)
    uint32_t round = 0;          // +1 each restart: a new countdown for everyone
    uint8_t mode = 0;            // FlyingModule::Mode
    uint8_t laps = 2;
    uint8_t rings = 10;
    float ringRadius = 14.0f;
    std::string mood;            // the sky
    std::vector<Seat> seats;
};
std::vector<uint8_t> encode(const Setup& s);
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes);

// A colour as the "character" every player joins with ("#5aa6ff").
std::string tintText(const glm::vec3& tint);
glm::vec3 tintFromText(const std::string& text, const glm::vec3& fallback);

} // namespace flying::net
