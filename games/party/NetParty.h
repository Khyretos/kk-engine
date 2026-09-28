#pragma once

// Party online (README.md "Online", docs/NETWORKING.md): what goes over
// the wire, apart from the game code, so both ends read and write it the
// same way.
//
//   Every bean a person plays is a network player (NetModule; a second
//   person on the same screen is a local player in slot 1..; the host's
//   CPU beans are the host's local players too). Its NetPlayerState
//   carries where it is, how it moves and, in `extra`, its look and its
//   minigame score.
//   The host runs the show: it sends each Round (which minigame, the seed
//   that builds the same level everywhere, who plays), and each Phase
//   change (the round card, the countdown, GO, the results). Each machine
//   moves its own beans and says when one finished or went out (Result);
//   the host puts them in order and passes it on. What a minigame shares
//   (a glass pane broke, the bomb was passed) is a Game event.

#include "Bean.h"

#include "kke/net/Protocol.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace party::netparty {

constexpr uint16_t kEventBase = 0x5000;          // Party's own event kinds
constexpr uint16_t kEventRound = kEventBase + 0; // host -> all: the round (Round)
constexpr uint16_t kEventPhase = kEventBase + 1; // host -> all: the show moved on (Phase)
constexpr uint16_t kEventResult = kEventBase + 2; // owner -> host -> all: a bean finished or went out (Result)
constexpr uint16_t kEventGame = kEventBase + 3;  // anyone -> host -> all: the minigame's own (Game)

// A bean as its owner sees it, enough for another machine to draw it.
struct Pose {
    glm::vec3 feet{0.0f}, velocity{0.0f};
    float yaw = 0.0f;
    bool grounded = true, diving = false, stunned = false, hidden = false;
    float tumble = 0.0f;       // 0..1 of a tumble turn
    BeanLook look;
    float score = 0.0f;        // the minigame's (taps, seconds held)
};
kke::net::NetPlayerState toState(const Pose& p);
Pose fromState(const kke::net::NetPlayerState& s);

struct Seat {
    uint8_t player = 0;        // network player id
    bool cpu = false;
    std::string name;
    BeanLook look;
    int32_t points = 0;        // the show so far
};
struct Round {
    uint32_t round = 0;        // +1 each round the host starts (a new level for everyone)
    uint8_t index = 0, rounds = 5; // this round of the show (0-based), how many
    uint32_t seed = 1;
    std::string game;          // Minigame::id
    std::vector<Seat> seats;
};
std::vector<uint8_t> encode(const Round& r);
std::optional<Round> decodeRound(const std::vector<uint8_t>& bytes);

struct Phase {
    uint32_t round = 0;
    uint8_t phase = 0;         // PartyModule::Phase
};
std::vector<uint8_t> encode(const Phase& p);
std::optional<Phase> decodePhase(const std::vector<uint8_t>& bytes);

constexpr uint8_t kResultFinish = 0, kResultOut = 1;
struct Result {
    uint32_t round = 0;
    uint8_t player = 0;
    uint8_t kind = kResultFinish;
    int16_t order = -1;        // the host's: 0 = first over the line / first out (-1: from a player, not ordered yet)
};
std::vector<uint8_t> encode(const Result& r);
std::optional<Result> decodeResult(const std::vector<uint8_t>& bytes);

struct Game {
    uint32_t round = 0;
    uint8_t kind = 0;
    int32_t a = 0, b = 0;      // within ±2^24
};
std::vector<uint8_t> encode(const Game& g);
std::optional<Game> decodeGame(const std::vector<uint8_t>& bytes);

} // namespace party::netparty
