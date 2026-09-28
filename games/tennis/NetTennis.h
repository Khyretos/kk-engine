#pragma once

// Tennis online (README.md "Online", docs/NETWORKING.md): what goes over
// the wire, apart from the game code, so both ends read and write it the
// same way (tests/test_tennis.cpp round-trips every message).
//
//   Each person is a network player (NetModule; a second person on the
//   same screen is a local player in slot 1..). Each machine runs its own
//   people and sends where they are and how they swing (NetPlayerState,
//   toState/fromState); the host's CPU players, as many as ten courts
//   need, go ten times a second in one event (Cpus).
//   The host is the umpire: its Setup says who plays where, its Serve
//   starts each point and its Point ends it, with the umpire's result,
//   which every machine applies to the same score.
//   The ball's flight is the same maths everywhere (Shot.h), so a hit
//   (the hitter's machine sends Hit) and a toss are all it needs; the
//   host's Ball, a few times a second, puts right any drift.

#include "kke/net/Protocol.h"

#include "Body.h"
#include "Rules.h"
#include "Shot.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tennis::net {

constexpr uint16_t kEventBase = 0x5400;             // Tennis's own event kinds
constexpr uint16_t kEventSetup = kEventBase + 0;    // host -> all: a new match (Setup)
constexpr uint16_t kEventServe = kEventBase + 1;    // host -> all: a point starts (Serve)
constexpr uint16_t kEventPoint = kEventBase + 2;    // host -> all: the umpire's call (Point)
constexpr uint16_t kEventHit = kEventBase + 3;      // hitter's machine -> host -> all (Hit)
constexpr uint16_t kEventToss = kEventBase + 4;     // server's machine -> host -> all (Toss)
constexpr uint16_t kEventBall = kEventBase + 5;     // host -> all, a few times a second: where the ball is (Ball)
constexpr uint16_t kEventEnd = kEventBase + 6;      // host -> all: a match is over or off (End)
constexpr uint16_t kEventCpus = kEventBase + 7;     // host -> all, 10 times a second: where its CPU players are (Cpus)
constexpr uint16_t kEventGate = kEventBase + 8;     // a client -> host: someone at a court's gate (Gate)
constexpr uint16_t kEventBoard = kEventBase + 9;    // host -> all: the sport center's gates and wins (Board)

constexpr size_t kMaxHistory = 1024; // points a Setup replays (a long best of three is ~250)
constexpr size_t kMaxMatches = 16;   // matches in one Cpus event (the sport center has 10 courts)

// A player as its machine sees it, enough for another to draw it.
struct Pose {
    glm::vec3 feet{0.0f};         // world
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, -1.0f}; // world, flat
    SwingPose::Kind swing = SwingPose::Kind::Ready;
    float swingT = -2.0f;         // -1..1 while swinging, < -1 idle
    glm::vec3 contact{0.0f};      // the swing's contact, body frame
    bool tossing = false;         // serving: the ball is up
    bool cheer = true;
    bool celebrating = false;
};
kke::net::NetPlayerState toState(const Pose& p);
Pose fromState(const kke::net::NetPlayerState& s);

struct Seat {
    uint8_t player = 0;           // network player id (0 for a CPU player)
    uint8_t team = 0, slot = 0;
    bool cpu = false;
    std::string name;
    glm::vec3 tint{1.0f};
};
struct Setup {
    uint32_t match = 0;           // +1 each match the host starts
    uint8_t court = 0;
    uint8_t teamSize = 1, gamesPerSet = 4, setsToWin = 1;
    bool center = false;          // part of the sport center (else the one match everyone plays)
    uint16_t point = 0;           // the point it's on (someone joining mid-match)
    std::vector<uint8_t> history; // who won each point so far (0 / 1): the score, replayed
    std::vector<Seat> seats;      // in the match's order (Hit and Toss name players by it)
};
std::vector<uint8_t> encode(const Setup& s);
std::optional<Setup> decodeSetup(const std::vector<uint8_t>& bytes);

// Points are counted from 0 in each match; a Hit or a Toss for another
// point (late, from before the call) is ignored.
struct Serve {
    uint32_t match = 0;
    uint16_t point = 0;
    bool again = false;           // the same point served again (after a fault or a let)
    bool second = false;          // ... as a second serve
};
std::vector<uint8_t> encode(const Serve& s);
std::optional<Serve> decodeServe(const std::vector<uint8_t>& bytes);

struct Point {
    uint32_t match = 0;
    uint16_t point = 0;
    uint8_t result = 0;           // Rally::Result
    std::string call;             // "Out", "Double fault"...
};
std::vector<uint8_t> encode(const Point& p);
std::optional<Point> decodePoint(const std::vector<uint8_t>& bytes);

struct Hit {
    uint32_t match = 0;
    uint16_t point = 0;
    uint8_t player = 0;           // who hit it: their place in the Setup's seats
    uint8_t shot = 0;             // the rally's shot number (0: the serve)
    bool serve = false;
    uint8_t kind = 0;             // ShotKind
    glm::vec3 at{0.0f};           // court space, the ball's centre
    glm::vec3 velocity{0.0f};     // court space
    glm::vec3 spin{0.0f};         // rad/s, court space
    float pull = 0.0f;            // the spin's pull, m/s^2
    float squash = 0.0f;          // 0..1, how hard the strings flatten it
};
std::vector<uint8_t> encode(const Hit& h);
std::optional<Hit> decodeHit(const std::vector<uint8_t>& bytes);

struct Toss {
    uint32_t match = 0;
    uint16_t point = 0;
    uint8_t player = 0;           // the server's place in the Setup's seats
    glm::vec3 at{0.0f};           // court space
    glm::vec3 velocity{0.0f};
};
std::vector<uint8_t> encode(const Toss& t);
std::optional<Toss> decodeToss(const std::vector<uint8_t>& bytes);

struct BallState {
    uint32_t match = 0;
    uint16_t point = 0;
    uint8_t shot = 0;             // the rally's shots so far (a hit on its way here is newer)
    Flight flight;                // court space; gravity includes the spin's pull
    bool rolling = false;
};
std::vector<uint8_t> encode(const BallState& b);
std::optional<BallState> decodeBall(const std::vector<uint8_t>& bytes);

struct End {
    uint32_t match = 0;           // 0: all of them (the host left the sport center)
    std::string why;              // "Juno left"; empty: it was played out
};
std::vector<uint8_t> encode(const End& e);
std::optional<End> decodeEnd(const std::vector<uint8_t>& bytes);

// The host's CPU players, court space (Court.h), a match at a time.
struct CpuPose {
    uint8_t player = 0;           // their place in the Setup's seats
    glm::vec3 feet{0.0f};
    glm::vec2 velocity{0.0f};     // x, z
    float yaw = 0.0f;             // degrees, court space: 0 = +Z
    SwingPose::Kind swing = SwingPose::Kind::Ready;
    float swingT = -2.0f;
    glm::vec3 contact{0.0f};
    bool tossing = false, celebrating = false, cheer = true;
};
struct CpuMatch {
    uint32_t match = 0;
    std::vector<CpuPose> players;
};
struct Cpus {
    std::vector<CpuMatch> matches;
};
std::vector<uint8_t> encode(const Cpus& c);
std::optional<Cpus> decodeCpus(const std::vector<uint8_t>& bytes);

// Someone at a court's gate, on a client: the host keeps the queues.
struct Gate {
    enum Action : uint8_t { Join = 0, Leave = 1, CpuNow = 2 };
    uint8_t court = 0;
    uint8_t player = 0;           // network player id
    uint8_t action = Join;
};
std::vector<uint8_t> encode(const Gate& g);
std::optional<Gate> decodeGate(const std::vector<uint8_t>& bytes);

// The sport center as the host has it: whether it's on, who waits at each
// gate, and the matches won.
struct Board {
    static constexpr size_t kCourts = 10;
    static constexpr size_t kWins = 10;
    bool center = false;
    uint8_t waiting[kCourts] = {};
    float countdown[kCourts] = {}; // s, < 0: none
    std::vector<std::pair<std::string, uint16_t>> wins; // best first, up to kWins
};
std::vector<uint8_t> encode(const Board& b);
std::optional<Board> decodeBoard(const std::vector<uint8_t>& bytes);

// A player's colour as the "character" every player joins with ("#5aa6ff").
std::string tintText(const glm::vec3& tint);
glm::vec3 tintFromText(const std::string& text, const glm::vec3& fallback);

} // namespace tennis::net
