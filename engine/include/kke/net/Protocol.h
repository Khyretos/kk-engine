#pragma once

#include "kke/net/BitStream.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kke::net {

// The engine's wire protocol, version kProtocolVersion (docs/NETWORKING.md
// "Messages"). Every message starts with its MessageType (5 bits) and has
// one serialize() for both directions (BitStream.h). A peer with another
// version is turned away at the Hello, never mis-decoded.
//
// Units and ranges (quantized; what the game sees after a round trip):
//   position  x, z in +-4096 m, y in -512..1536 m, 1/1024 m steps (~1 mm)
//   velocity  +-64 m/s per axis, 1/128 m/s steps
//   rotation  smallest-three quaternion, ~0.001 per component
//   yaw       0..360 degrees, 1024 steps (0.35 degrees)
constexpr uint16_t kProtocolVersion = 2; // 2: Spawn, Despawn, Break (networking v2, #28)
constexpr size_t kMaxPlayers = 32;
constexpr size_t kMaxNameLength = 24;
constexpr size_t kMaxGameIdLength = 32;
constexpr size_t kMaxCharacterLength = 64;
constexpr size_t kMaxReasonLength = 128;
constexpr size_t kMaxEventBytes = 512;
constexpr size_t kMaxBodiesPerSnapshot = 255;
constexpr uint32_t kMaxBodyId = 65535;
constexpr size_t kMaxSpawnBytes = 256;    // a spawned object's description (game-defined)
constexpr size_t kMaxBordersPerBreak = 1024; // more go in several Break messages
constexpr uint32_t kMaxChunkId = 65535;

constexpr float kWorldXZ = 4096.0f;
constexpr float kWorldYMin = -512.0f, kWorldYMax = 1536.0f;
constexpr float kPositionStep = 1.0f / 1024.0f;
constexpr float kMaxVelocity = 64.0f;
constexpr float kVelocityStep = 1.0f / 128.0f;

enum class MessageType : uint8_t {
    Hello = 1,       // client -> server (reliable): who I am
    Welcome,         // server -> client (reliable): you're in, your id
    Reject,          // server -> client (reliable): not in, and why
    PlayerInfo,      // server -> clients (reliable): someone joined / left
    Correction,      // server -> one client (reliable): you're here, not there
    GameEvent,       // either way (reliable): game-defined, relayed by the server
    PlayerState,     // client -> server (unreliable): my player now
    Snapshot,        // server -> client (unreliable): everyone and the bodies now
    Spawn,           // server -> clients (reliable): an object the host made (a script's body)
    Despawn,         // server -> clients (reliable): it's gone
    Break,           // server -> clients (reliable): these borders of a breakable broke
    Count
};

// A player as the game describes it: where, how fast, which way, and what
// it's doing (the game's own state machine, e.g. kke::Locomotion::State),
// enough for a remote copy to animate the same.
struct NetPlayerState {
    glm::vec3 position{0.0f};  // feet
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;          // degrees
    uint8_t state = 0;         // 0..15, game-defined
    uint8_t flags = 0;         // 8 game-defined bits (crouch, jumped, ...)
    float speed = 0.0f;        // 0..20 m/s, for the animation blend
    float progress = 0.0f;     // 0..1, e.g. how far into a vault
    float aux = 0.0f;          // 0..32, game-defined (fall height, ...)
};
// Flag bits the engine itself understands (the rest are the game's).
constexpr uint8_t kPlayerTeleported = 1u << 7; // a legitimate jump in position (spawn, scene change)

// A rigid body in a snapshot (server-authoritative physics).
struct NetBodyState {
    uint16_t id = 0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    bool sleeping = false;
};

struct HelloMsg {
    uint16_t version = kProtocolVersion;
    std::string gameId, name, character;
};
struct WelcomeMsg {
    uint8_t playerId = 0;
    uint8_t maxPlayers = 0;
    uint16_t snapshotHz = 0;
    uint32_t serverTimeMs = 0;
};
struct RejectMsg { std::string reason; };
struct PlayerInfoMsg {
    uint8_t playerId = 0;
    bool present = true;       // false: left
    std::string name, character;
};
struct CorrectionMsg { glm::vec3 position{0.0f}; };
struct GameEventMsg {
    uint8_t fromPlayer = 0;    // filled in by the server, never trusted from a client
    uint16_t kind = 0;
    std::vector<uint8_t> payload;
};
// An object the host created at run time (a server script's body or
// breakable): `id` is its network id (a body's id in snapshots, or a
// breakable's in Break), `kind` says who builds it on a client, `desc` is
// that builder's own serialized description. Persistent spawns are
// repeated to late joiners; transient ones (a thrown ball) are not.
struct SpawnMsg {
    uint16_t id = 0;
    uint16_t kind = 0;
    std::vector<uint8_t> desc;
};
struct DespawnMsg { uint16_t id = 0; };
// Pieces of a breakable (kke::BreakGraph) that came apart on the host:
// each border is the pair of piece (chunk) ids either side of it. The same
// baked pieces and the same broken borders give the same pieces on every
// machine; `seed` is the breakable's fracture seed, so a client whose copy
// was baked differently notices instead of breaking it wrongly.
struct BreakMsg {
    uint16_t id = 0;
    uint32_t seed = 0;
    std::vector<std::pair<uint16_t, uint16_t>> borders;
};
struct PlayerStateMsg {
    uint32_t timeMs = 0;       // sender's clock
    NetPlayerState state;
};
struct SnapshotMsg {
    uint32_t serverTimeMs = 0;
    struct Player { uint8_t id; NetPlayerState state; };
    std::vector<Player> players;
    std::vector<NetBodyState> bodies;
};

template <typename Stream>
void serializePosition(Stream& s, glm::vec3& p) {
    s.vec3(p, glm::vec3(-kWorldXZ, kWorldYMin, -kWorldXZ), glm::vec3(kWorldXZ, kWorldYMax, kWorldXZ), kPositionStep);
}

template <typename Stream>
void serialize(Stream& s, NetPlayerState& p) {
    serializePosition(s, p.position);
    s.vec3(p.velocity, kMaxVelocity, kVelocityStep);
    s.real(p.yaw, 0.0f, 360.0f, 360.0f / 1023.0f);
    s.integer(p.state, 0, 15);
    s.integer(p.flags, 0, 255);
    s.real(p.speed, 0.0f, 20.0f, 20.0f / 255.0f);
    s.real(p.progress, 0.0f, 1.0f, 1.0f / 63.0f);
    s.real(p.aux, 0.0f, 32.0f, 32.0f / 255.0f);
}

template <typename Stream>
void serialize(Stream& s, NetBodyState& b) {
    s.integer(b.id, 0, kMaxBodyId);
    s.boolean(b.sleeping);
    serializePosition(s, b.position);
    s.quat(b.rotation);
    if (!b.sleeping) s.vec3(b.velocity, kMaxVelocity, kVelocityStep);
    else if constexpr (Stream::kReading) b.velocity = glm::vec3(0.0f);
}

template <typename Stream> void serialize(Stream& s, HelloMsg& m) {
    s.integer(m.version, 0, 65535);
    s.string(m.gameId, kMaxGameIdLength);
    s.string(m.name, kMaxNameLength);
    s.string(m.character, kMaxCharacterLength);
}
template <typename Stream> void serialize(Stream& s, WelcomeMsg& m) {
    s.integer(m.playerId, 0, kMaxPlayers);
    s.integer(m.maxPlayers, 1, kMaxPlayers);
    s.integer(m.snapshotHz, 1, 120);
    s.bits(m.serverTimeMs, 32);
}
template <typename Stream> void serialize(Stream& s, RejectMsg& m) { s.string(m.reason, kMaxReasonLength); }
template <typename Stream> void serialize(Stream& s, PlayerInfoMsg& m) {
    s.integer(m.playerId, 0, kMaxPlayers);
    s.boolean(m.present);
    s.string(m.name, kMaxNameLength);
    s.string(m.character, kMaxCharacterLength);
}
template <typename Stream> void serialize(Stream& s, CorrectionMsg& m) { serializePosition(s, m.position); }
template <typename Stream> void serialize(Stream& s, GameEventMsg& m) {
    s.integer(m.fromPlayer, 0, kMaxPlayers);
    s.integer(m.kind, 0, 65535);
    s.bytes(m.payload, kMaxEventBytes);
}
template <typename Stream> void serialize(Stream& s, SpawnMsg& m) {
    s.integer(m.id, 0, kMaxBodyId);
    s.integer(m.kind, 0, 65535);
    s.bytes(m.desc, kMaxSpawnBytes);
}
template <typename Stream> void serialize(Stream& s, DespawnMsg& m) { s.integer(m.id, 0, kMaxBodyId); }
template <typename Stream> void serialize(Stream& s, BreakMsg& m) {
    s.integer(m.id, 0, kMaxBodyId);
    s.bits(m.seed, 32);
    uint32_t count = static_cast<uint32_t>(m.borders.size());
    s.integer(count, 0, kMaxBordersPerBreak);
    // 32 bits per border: a count the packet can't hold is refused first.
    if constexpr (Stream::kReading) {
        if (static_cast<size_t>(count) * 32 > s.bitsLeft()) s.fail();
        m.borders.resize(s.ok() ? count : 0);
    }
    for (auto& [a, b] : m.borders) {
        s.integer(a, 0, kMaxChunkId);
        s.integer(b, 0, kMaxChunkId);
    }
}
template <typename Stream> void serialize(Stream& s, PlayerStateMsg& m) {
    s.bits(m.timeMs, 32);
    serialize(s, m.state);
}
template <typename Stream> void serialize(Stream& s, SnapshotMsg& m) {
    s.bits(m.serverTimeMs, 32);
    uint32_t players = static_cast<uint32_t>(m.players.size());
    s.integer(players, 0, kMaxPlayers);
    if constexpr (Stream::kReading) m.players.resize(s.ok() ? players : 0);
    for (SnapshotMsg::Player& p : m.players) {
        s.integer(p.id, 0, kMaxPlayers);
        serialize(s, p.state);
    }
    uint32_t bodies = static_cast<uint32_t>(m.bodies.size());
    s.integer(bodies, 0, kMaxBodiesPerSnapshot);
    // Each body is at least ~60 bits: a count the packet can't hold is a
    // lie, refused before allocating anything.
    if constexpr (Stream::kReading) {
        if (static_cast<size_t>(bodies) * 60 > s.bitsLeft()) s.fail();
        m.bodies.resize(s.ok() ? bodies : 0);
    }
    for (NetBodyState& b : m.bodies) serialize(s, b);
}

// Whole messages: the type, then the body. decode() returns nullopt for
// anything malformed (wrong type, truncated, out of range, trailing junk
// longer than the final byte's padding).
template <typename Msg>
std::vector<uint8_t> encode(MessageType type, Msg& msg) {
    std::vector<uint8_t> out;
    {
        WriteStream w(out);
        w.bits(static_cast<uint32_t>(type), 5);
        serialize(w, msg);
    }
    return out;
}

// The type of a packet, or nullopt if it has none we know.
std::optional<MessageType> peekType(const uint8_t* data, size_t size);

template <typename Msg>
std::optional<Msg> decode(MessageType expected, const uint8_t* data, size_t size) {
    ReadStream r(data, size);
    uint32_t type = r.readBits(5);
    if (!r.ok() || type != static_cast<uint32_t>(expected)) return std::nullopt;
    Msg msg{};
    serialize(r, msg);
    if (!r.ok() || r.bitsLeft() >= 8) return std::nullopt;
    return msg;
}

} // namespace kke::net
