#pragma once

#include "kke/net/Protocol.h"
#include "kke/net/Transport.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke::net {

// Replication for action games: one authoritative server (a dedicated
// process, or the host's game: "host = client + server in one process"),
// any number of clients (docs/NETWORKING.md "Model"). Pure logic over an
// ITransport: no GPU, no physics engine, so tests run a server and clients
// in one process on a LoopbackNetwork with lag and loss.
//
//   Players   each client simulates its own player right away (no input
//             lag) and sends its state 30x a second; the server checks
//             every move against speed limits (MovementLimits) and
//             corrects a client that breaks them, then shares all players
//             in its snapshots. Everyone else's players are drawn
//             interpolated ~100 ms in the past between two snapshots
//             (smooth under jitter and loss).
//   Bodies    the server's physics is the truth: snapshots carry the
//             bodies that matter most for each client (priority
//             accumulator, awake before asleep, one packet's budget), and
//             clients show them interpolated the same way.
//   Events    reliable, game-defined (a shot, a push): clients send them
//             to the server, which applies them and relays them.
//   Spawns    objects the host makes while playing (a server script's
//             bodies and breakables): each client builds its own copy
//             from a small description; late joiners get the ones still
//             there. Their bodies then travel in snapshots like any other.
//   Breaks    which borders of a breakable broke on the host (kke::
//             BreakGraph): clients break their copy along the same
//             borders, so everyone sees the same pieces. Kept for late
//             joiners too.
//
// Why owner-predicted players rather than server-side input replay: the
// character's traversal state machine (kke::Locomotion: vault, climb,
// hang) can't yet be rewound and replayed, and co-op / sandbox games
// don't need it. The server does check every move: speed limits here,
// and through `checkMove` whatever the game adds (NetModule: no walking
// through walls, no flying; kke/net/WorldMoveCheck.h). Input replay for
// competitive games is issue #28.

struct NetConfig {
    std::string gameId = "kke";     // clients of another game are turned away
    uint16_t snapshotHz = 30;       // server -> each client
    uint16_t stateHz = 30;          // client -> server (its player)
    size_t maxPlayers = 8;          // including the host's own player
    size_t snapshotBytes = 1100;    // one UDP packet, whatever the MTU
    double interpolationDelay = 0.1; // seconds behind the newest data
    double maxExtrapolation = 0.25; // past the newest data, then hold
    double helloTimeout = 5.0;      // connected but silent: dropped
    double connectTimeout = 10.0;   // client: no Welcome by then: give up
    size_t maxBadPackets = 20;      // malformed packets before a kick
    size_t maxEventsPerSecond = 60; // per client; more are dropped
};

// How far a player may move between two of its states (server check).
struct MovementLimits {
    float horizontalSpeed = 15.0f;  // m/s (sprint is ~6.5)
    float riseSpeed = 12.0f;        // m/s up (jumps, climbs)
    float fallSpeed = 60.0f;        // m/s down
    float slack = 1.0f;             // metres of tolerance per check
    double teleportCooldown = 2.0;  // seconds between accepted teleports
};

// What the game sees of another player.
struct RemotePlayer {
    uint8_t id = 0;
    std::string name, character;
    NetPlayerState state;           // interpolated for "now"
    bool hasState = false;
};

// Maps a sender's clock onto ours: offset = arrival - sent, following the
// fastest packets (the least-delayed estimate) and drifting up slowly when
// the path gets slower.
class ClockOffset {
public:
    void observe(double senderTime, double localTime);
    bool valid() const { return m_valid; }
    double senderNow(double localTime) const { return localTime - m_offset; }
    void reset() { m_valid = false; }

private:
    double m_offset = 0.0;
    bool m_valid = false;
};

// Time-stamped samples of one thing (a player, a body), sampled between
// the two around the render time, or extrapolated a little past the newest.
template <typename T>
class Timeline {
public:
    void push(double time, const T& value) {
        if (!m_samples.empty() && time <= m_samples.back().first) {
            // Out of order or duplicate: keep order; an older one fills a gap.
            for (auto it = m_samples.begin(); it != m_samples.end(); ++it) {
                if (it->first == time) return;
                if (it->first > time) { m_samples.insert(it, { time, value }); trim(); return; }
            }
            return;
        }
        m_samples.push_back({ time, value });
        trim();
    }
    bool empty() const { return m_samples.empty(); }
    const T& newest() const { return m_samples.back().second; }
    double newestTime() const { return m_samples.back().first; }
    void clear() { m_samples.clear(); }
    // a, b and t with a + (b - a) * t = the value at `time`; t > 1 past the
    // newest (extrapolate), a == b before the oldest.
    bool bracket(double time, const T*& a, const T*& b, double& t, double& dtAB) const {
        if (m_samples.empty()) return false;
        if (time <= m_samples.front().first || m_samples.size() == 1) {
            a = b = &m_samples.front().second;
            t = 0.0;
            dtAB = 0.0;
            if (time > m_samples.front().first) { t = time - m_samples.front().first; dtAB = -1.0; } // seconds past, single sample
            return true;
        }
        for (size_t i = 1; i < m_samples.size(); ++i) {
            if (m_samples[i].first >= time) {
                a = &m_samples[i - 1].second;
                b = &m_samples[i].second;
                dtAB = m_samples[i].first - m_samples[i - 1].first;
                t = dtAB > 0.0 ? (time - m_samples[i - 1].first) / dtAB : 1.0;
                return true;
            }
        }
        a = b = &m_samples.back().second;
        t = time - m_samples.back().first;
        dtAB = -1.0; // past the newest by t seconds
        return true;
    }

private:
    void trim() {
        while (m_samples.size() > 32) m_samples.pop_front();
    }
    std::deque<std::pair<double, T>> m_samples;
};

NetPlayerState interpolate(const Timeline<NetPlayerState>& line, double time, double maxExtrapolation);
NetBodyState interpolate(const Timeline<NetBodyState>& line, double time, double maxExtrapolation);

// Reserved event kinds (games use 0 .. 0xFEFF).
constexpr uint16_t kEventBodiesReset = 0xFF00; // the replicated body set changed: forget old bodies

// ------------------------------------------------------------------ server

class NetServer {
public:
    NetServer(ITransport& transport, const NetConfig& config = {});
    ~NetServer();

    // Opens the transport on `port`; the host's own player is id 0.
    bool start(uint16_t port, const std::string& hostName, const std::string& hostCharacter, std::string* error = nullptr);
    void stop();
    bool running() const { return m_running; }

    void setLocalState(const NetPlayerState& state) { m_local = state; m_hasLocal = true; }
    // Every replicated body as it is now (the server's physics). Ids are
    // the game's own, 0..65535, stable while the body lives.
    void setBodies(const std::vector<NetBodyState>& bodies) { m_bodies = bodies; }
    // Host -> every client (except one), e.g. "a ball was shot".
    void sendEvent(uint16_t kind, const std::vector<uint8_t>& payload, int exceptPlayer = -1);
    // A client's event to everyone else (the game decides what to relay).
    void relayEvent(const GameEventMsg& e);
    void kick(uint8_t playerId, const std::string& reason);

    // Objects made at run time (see Spawns above). `persistent`: also
    // sent to players who join later, until despawn(). Ids are the game's
    // (NetModule uses 0x8000 and up), unique among live spawns.
    void spawn(const SpawnMsg& m, bool persistent = true);
    void despawn(uint16_t id); // also forgets its breaks
    // Borders of breakable `id` that broke here, as (piece, piece) pairs;
    // ones already sent are skipped, so passing the whole set each time
    // is fine. A new `seed` for the same id starts a fresh set.
    void breakBorders(uint16_t id, uint32_t seed, const std::vector<std::pair<uint16_t, uint16_t>>& borders);
    void forgetBreaks(uint16_t id);
    size_t spawnCount() const { return m_spawns.size(); }

    void update(double now);

    // The other players (clients), interpolated for `now`.
    std::vector<RemotePlayer> players(double now) const;
    size_t clientCount() const;
    PeerStats stats(uint8_t playerId) const;
    size_t badPackets() const { return m_badPackets; }
    size_t corrections() const { return m_corrections; }
    size_t refusedMoves() const { return m_refusedMoves; } // by checkMove

    MovementLimits limits;
    // A move that passed the speed limits: may the player go from `from`
    // to `to` in `dt` seconds? false = refused (the client is corrected
    // back to `from`). Not asked for teleports the limits allow, nor for a
    // player's first state.
    std::function<bool(uint8_t id, const NetPlayerState& from, const NetPlayerState& to, double dt)> checkMove;
    std::function<void(const GameEventMsg&)> onEvent;          // from a client
    std::function<void(uint8_t id, bool joined)> onPlayer;

private:
    struct Client {
        PeerId peer = kNoPeer;
        uint8_t id = 0;               // 0 until Hello
        std::string name, character;
        double connectedAt = 0.0;
        ClockOffset clock;
        Timeline<NetPlayerState> states;
        NetPlayerState accepted;      // the last move that passed the checks
        uint32_t acceptedTimeMs = 0;
        double acceptedAt = 0.0;
        bool hasState = false;
        double lastTeleport = -1e9, lastCorrection = -1e9;
        size_t badPackets = 0;
        double eventWindow = 0.0;
        size_t eventsInWindow = 0;
        std::map<uint16_t, float> priority; // body id -> accumulated priority
        std::map<uint16_t, bool> sentSleeping;
    };
    Client* byPeer(PeerId peer);
    Client* byId(uint8_t id);
    void receive(Client& c, const NetEvent& e);
    void handleHello(Client& c, const HelloMsg& m);
    void handleState(Client& c, const PlayerStateMsg& m);
    void bad(Client& c, const char* what);
    void drop(Client& c, const std::string& reason);
    void sendSnapshot(Client& c);
    void broadcastReliable(const std::vector<uint8_t>& data, int exceptPlayer);
    void correct(Client& c);
    void sendBreaks(PeerId peer, uint16_t id, uint32_t seed, const std::vector<std::pair<uint16_t, uint16_t>>& borders, int exceptPlayer);
    uint32_t timeMs() const { return static_cast<uint32_t>((m_now - m_start) * 1000.0); }

    ITransport& m_transport;
    NetConfig m_config;
    struct BreakSet { uint32_t seed = 0; std::set<std::pair<uint16_t, uint16_t>> borders; };
    std::map<uint16_t, SpawnMsg> m_spawns;  // persistent ones, for late joiners
    std::map<uint16_t, BreakSet> m_breaks;
    size_t m_refusedMoves = 0;
    bool m_running = false, m_started = false;
    double m_now = 0.0, m_start = 0.0, m_nextSnapshot = 0.0;
    std::string m_hostName, m_hostCharacter;
    NetPlayerState m_local;
    bool m_hasLocal = false;
    std::vector<NetBodyState> m_bodies;
    std::vector<Client> m_clients;
    size_t m_badPackets = 0, m_corrections = 0;
};

// ------------------------------------------------------------------ client

class NetClient {
public:
    enum class Status { Idle, Connecting, Connected, Rejected, Disconnected };

    NetClient(ITransport& transport, const NetConfig& config = {});
    ~NetClient();

    bool connect(const std::string& address, uint16_t port, const std::string& name, const std::string& character,
                 std::string* error = nullptr);
    void disconnect();
    Status status() const { return m_status; }
    const std::string& statusText() const { return m_statusText; }
    uint8_t playerId() const { return m_playerId; }

    void setLocalState(const NetPlayerState& state) { m_local = state; m_hasLocal = true; }
    void sendEvent(uint16_t kind, const std::vector<uint8_t>& payload);

    void update(double now);

    std::vector<RemotePlayer> players(double now) const;
    // A server body, interpolated for `now`; false if never seen.
    bool body(uint16_t id, double now, NetBodyState& out) const;
    std::vector<uint16_t> bodyIds() const;
    PeerStats stats() const { return m_transport.stats(m_server); }
    size_t badPackets() const { return m_badPackets; }

    std::function<void(const GameEventMsg&)> onEvent;
    std::function<void(const glm::vec3&)> onCorrection;        // the server put us here
    std::function<void(uint8_t id, bool joined)> onPlayer;
    std::function<void(const SpawnMsg&)> onSpawn;              // build your copy
    std::function<void(uint16_t id)> onDespawn;                // remove it
    std::function<void(const BreakMsg&)> onBreak;              // break your copy along these borders

private:
    struct Player { std::string name, character; Timeline<NetPlayerState> states; };
    void receive(const NetEvent& e);
    double renderTime(double now) const;
    uint32_t timeMs() const { return static_cast<uint32_t>((m_now - m_start) * 1000.0); }

    ITransport& m_transport;
    NetConfig m_config;
    Status m_status = Status::Idle;
    std::string m_statusText;
    PeerId m_server = kNoPeer;
    std::string m_name, m_character;
    uint8_t m_playerId = 0;
    double m_now = 0.0, m_start = 0.0, m_connectStarted = 0.0, m_nextSend = 0.0;
    bool m_started = false;
    ClockOffset m_clock;
    uint32_t m_lastSnapshotMs = 0;
    bool m_haveSnapshot = false;
    NetPlayerState m_local;
    bool m_hasLocal = false;
    std::map<uint8_t, Player> m_players;
    std::map<uint16_t, Timeline<NetBodyState>> m_bodies;
    size_t m_badPackets = 0;
};

} // namespace kke::net
