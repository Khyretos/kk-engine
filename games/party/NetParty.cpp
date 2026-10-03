#include "NetParty.h"

#include "kke/net/BitStream.h"

#include <algorithm>

namespace party::netparty {

namespace {

namespace net = kke::net;

constexpr size_t kMaxSeats = 12;    // the party's players online (main.cpp: NetConfig::maxPlayers)
constexpr size_t kMaxSeatName = 16; // a Round fits one event (kMaxEventBytes) with every seat
constexpr size_t kMaxGameId = 24;
constexpr uint8_t kGrounded = 1u << 0, kDiving = 1u << 1, kStunned = 1u << 2, kHidden = 1u << 3;

template <typename Stream> void serialize(Stream& s, BeanLook& l) {
    s.integer(l.colour, 0, 31);
    s.integer(l.pattern, 0, 15);
    s.integer(l.face, 0, 15);
    s.integer(l.hat, 0, 31);
    s.integer(l.body, 0, 15);
}

struct Extra {
    BeanLook look;
    float score = 0.0f;
};
template <typename Stream> void serialize(Stream& s, Extra& e) {
    serialize(s, e.look);
    s.real(e.score, 0.0f, 10000.0f, 0.01f);
}

template <typename Stream> void serialize(Stream& s, Round& m) {
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.index, 0, 255);
    s.integer(m.rounds, 1, 255);
    uint32_t lo = m.seed & 0xffffu, hi = m.seed >> 16;
    s.bits(lo, 16);
    s.bits(hi, 16);
    m.seed = lo | (hi << 16);
    s.string(m.game, kMaxGameId);
    uint32_t n = static_cast<uint32_t>(std::min(m.seats.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.seats.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.seats.size(); ++i) {
        Seat& seat = m.seats[i];
        s.integer(seat.player, 0, net::kMaxPlayers);
        s.boolean(seat.cpu);
        s.string(seat.name, kMaxSeatName);
        serialize(s, seat.look);
        s.integer(seat.points, 0, 100000);
    }
}
template <typename Stream> void serialize(Stream& s, Phase& m) {
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.phase, 0, 15);
}
template <typename Stream> void serialize(Stream& s, Result& m) {
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.player, 0, net::kMaxPlayers);
    s.integer(m.kind, 0, 1);
    s.integer(m.order, -1, 255);
}
template <typename Stream> void serialize(Stream& s, Game& m) {
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.kind, 0, 255);
    s.integer(m.a, -(1 << 24), 1 << 24);
    s.integer(m.b, -(1 << 24), 1 << 24);
}
template <typename Stream> void serialize(Stream& s, Ballot& m) {
    s.integer(m.index, 0, 255);
    s.integer(m.player, 0, net::kMaxPlayers);
    s.integer(m.choice, -1, 3);
}
template <typename Stream> void serialize(Stream& s, Vote& m) {
    s.integer(m.index, 0, 255);
    uint32_t games = static_cast<uint32_t>(std::min<size_t>(m.games.size(), 4));
    s.integer(games, 0, 4);
    if constexpr (Stream::kReading) m.games.resize(s.ok() ? games : 0);
    for (size_t i = 0; i < games && i < m.games.size(); ++i) s.string(m.games[i], kMaxGameId);
    uint32_t n = static_cast<uint32_t>(std::min(m.ballots.size(), kMaxSeats));
    s.integer(n, 0, kMaxSeats);
    if constexpr (Stream::kReading) m.ballots.resize(s.ok() ? n : 0);
    for (size_t i = 0; i < n && i < m.ballots.size(); ++i) {
        m.ballots[i].index = m.index;
        s.integer(m.ballots[i].player, 0, net::kMaxPlayers);
        s.integer(m.ballots[i].choice, -1, 3);
    }
    s.integer(m.secondsLeft, 0, 60);
    s.integer(m.winner, -1, 3);
}
template <typename Stream> void serialize(Stream& s, Knock& m) {
    s.integer(m.round, 0, 0x7fffffff);
    s.integer(m.player, 0, net::kMaxPlayers);
    s.real(m.velocity.x, -40.0f, 40.0f, 0.01f);
    s.real(m.velocity.y, -40.0f, 40.0f, 0.01f);
    s.real(m.velocity.z, -40.0f, 40.0f, 0.01f);
    s.real(m.stun, 0.0f, 3.0f, 0.01f);
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
    s.yaw = p.yaw;
    s.state = 0;
    s.flags = static_cast<uint8_t>((p.grounded ? kGrounded : 0) | (p.diving ? kDiving : 0) | (p.stunned ? kStunned : 0) | (p.hidden ? kHidden : 0));
    s.speed = std::clamp(glm::length(glm::vec2(p.velocity.x, p.velocity.z)), 0.0f, 20.0f);
    s.progress = std::clamp(p.tumble, 0.0f, 1.0f);
    Extra e{ p.look, std::clamp(p.score, 0.0f, 10000.0f) };
    s.extra = write(e);
    return s;
}

Pose fromState(const net::NetPlayerState& s) {
    Pose p;
    p.feet = s.position;
    p.velocity = s.velocity;
    p.yaw = s.yaw;
    p.grounded = (s.flags & kGrounded) != 0;
    p.diving = (s.flags & kDiving) != 0;
    p.stunned = (s.flags & kStunned) != 0;
    p.hidden = (s.flags & kHidden) != 0;
    p.tumble = s.progress;
    if (const std::optional<Extra> e = read<Extra>(s.extra)) {
        p.look = e->look;
        p.score = e->score;
    }
    return p;
}

std::vector<uint8_t> encode(const Round& r) { return write(r); }
std::optional<Round> decodeRound(const std::vector<uint8_t>& bytes) { return read<Round>(bytes); }
std::vector<uint8_t> encode(const Phase& p) { return write(p); }
std::optional<Phase> decodePhase(const std::vector<uint8_t>& bytes) { return read<Phase>(bytes); }
std::vector<uint8_t> encode(const Result& r) { return write(r); }
std::optional<Result> decodeResult(const std::vector<uint8_t>& bytes) { return read<Result>(bytes); }
std::vector<uint8_t> encode(const Game& g) { return write(g); }
std::optional<Game> decodeGame(const std::vector<uint8_t>& bytes) { return read<Game>(bytes); }

std::vector<uint8_t> encode(const Vote& v) { return write(v); }
std::optional<Vote> decodeVote(const std::vector<uint8_t>& bytes) { return read<Vote>(bytes); }
std::vector<uint8_t> encode(const Ballot& b) { return write(b); }
std::vector<uint8_t> encode(const Knock& k) { return write(k); }
std::optional<Knock> decodeKnock(const std::vector<uint8_t>& bytes) { return read<Knock>(bytes); }
std::optional<Ballot> decodeBallot(const std::vector<uint8_t>& bytes) { return read<Ballot>(bytes); }

} // namespace party::netparty
