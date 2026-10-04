#pragma once

#include "kke/net/Authority.h"
#include "kke/net/InputReplay.h"
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
//   Guests    several players on one connection (split screen online,
//             like Halo): a client adds guests in slots 1..7 (addGuest),
//             the host its own (addLocalGuest). Each is a player with an
//             id, a name and states of its own, checked like any other;
//             they share the connection's events, voice and snapshots.
//
// Players come in two models (docs/NETWORKING.md "Input replay"):
//   owner-predicted (default; co-op, sandbox): the client moves its
//             player and sends where it is; the server checks every move
//             (speed limits here, and through `checkMove` whatever the
//             game adds: NetModule refuses walls and flying,
//             kke/net/WorldMoveCheck.h).
//   input replay (NetConfig::inputReplay; competitive): the client sends
//             its inputs, the server runs everyone's movement from them
//             (nextInput / setPlayerState), the client predicts and
//             rewinds (kke/net/InputReplay.h). Nothing a client says
//             about where it is counts.

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
    std::string password;           // server: required to join ("" = none); client: what it sends
    bool dedicated = false;         // server: no player of its own (kke_server), all slots for clients
    bool inputReplay = false;       // server: players send inputs and the server moves them (the client learns it at Welcome)
    uint16_t tickHz = 60;           // server: input ticks per second under inputReplay
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
    bool hasState = false;          // false too while the server keeps it hidden (fog of war)
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
constexpr uint16_t kEventServerMessage = 0xFF01; // a line from the server to show (its MOTD, "say"): UTF-8 text

// Who hears a voice (docs/NETWORKING.md "Voice"). The server decides, so
// a player never gets voice they shouldn't, nor anyone's address.
struct VoiceRules {
    bool enabled = true;
    float proximityRange = 40.0f;          // m: past it a proximity voice isn't even sent
    bool allowTeam = true, allowAll = true; // which channels players may use
    // A player's team; unset = everyone is on one team.
    std::function<int(uint8_t playerId)> team;
    std::set<uint8_t> muted;               // speakers the server silenced (admin mute)
    size_t maxPacketsPerSecond = 60;       // per speaker; 50 is 20 ms frames
    // `speakerPos`/`listenerPos` null: not known yet (no state), so not near.
    bool reaches(VoiceChannel channel, uint8_t speaker, const glm::vec3* speakerPos, uint8_t listener, const glm::vec3* listenerPos) const;
};

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
    // More players at the host's own screen (split screen), slots 1 ..
    // kMaxLocalPlayers - 1. Returns the player id (0: no room left).
    uint8_t addLocalGuest(uint8_t slot, const std::string& name, const std::string& character);
    void removeLocalGuest(uint8_t slot);
    void setLocalGuestState(uint8_t slot, const NetPlayerState& state);
    uint8_t localGuestId(uint8_t slot) const;
    // The host's own player (slot 0) or one of its guests picked another
    // name or look (a lobby's colour, car, outfit): everyone is told.
    void setLocalProfile(uint8_t slot, const std::string& name, const std::string& character);
    // Every replicated body as it is now (the server's physics). Ids are
    // the game's own, 0..65535, stable while the body lives.
    void setBodies(const std::vector<NetBodyState>& bodies) { m_bodies = bodies; }
    // Host -> every client (except one), e.g. "a ball was shot".
    void sendEvent(uint16_t kind, const std::vector<uint8_t>& payload, int exceptPlayer = -1);
    // A client's event to everyone else (the game decides what to relay).
    void relayEvent(const GameEventMsg& e);
    void kick(uint8_t playerId, const std::string& reason);
    // Host -> one client (a reply: a leaderboard, a chat line to one player).
    void sendEventTo(uint8_t playerId, uint16_t kind, const std::vector<uint8_t>& payload);
    std::string address(uint8_t playerId) const; // where a client is (bans, logs)

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
    // The host's own voice (player 0), routed like a client's.
    void sendVoice(const VoiceMsg& m);
    size_t voiceRelayed() const { return m_voiceRelayed; }
    size_t spawnCount() const { return m_spawns.size(); }

    void update(double now);

    // The other players (clients and their guests), interpolated for `now`.
    std::vector<RemotePlayer> players(double now) const;
    size_t clientCount() const; // players on clients, guests included (the host's own are not)
    PeerStats stats(uint8_t playerId) const;
    size_t badPackets() const { return m_badPackets; }
    size_t corrections() const { return m_corrections; }
    size_t refusedMoves() const { return m_refusedMoves; } // by checkMove
    size_t refusedEvents() const { return m_refusedEvents; } // by authority or checkEvent

    // Who decides what, per player role (kke/net/Authority.h,
    // docs/ANTI_CHEAT.md): a Server-authority move or event from a client
    // is dropped, a Client-authority move skips the checks, Checked is
    // the speed limits + checkMove / checkEvent. Roles are forgotten when
    // a player leaves.
    AuthorityPolicy authority;
    // An event from a player whose events are Checked: may it happen?
    // false = dropped (onEvent never sees it). Unset = allowed.
    std::function<bool(uint8_t id, const GameEventMsg&)> checkEvent;
    // Fog of war (kke/net/Visibility.h, docs/ANTI_CHEAT.md): may player
    // `viewer` be sent player `subject` (0 = the host) in its snapshots?
    // Unset = everyone sees everyone. A client drops a player the moment
    // it's left out (RemotePlayer::hasState false) and starts it fresh
    // when it's back.
    std::function<bool(uint8_t viewer, uint8_t subject)> sendPlayer;

    // --- groups (docs/NETWORKING.md "Groups"): a big game in rooms, like
    // the courts of a sport center. Every player (0 = the host) and every
    // body is in a group, 0 by default. A client is sent the players and
    // bodies of its own group and of the groups it shows (showGroups: a
    // spectator in the lobby watching court 3); the rest are hidden from
    // it. Forgotten when a player leaves.
    void setGroup(uint8_t player, uint16_t group);
    uint16_t group(uint8_t player) const;
    void showGroups(uint8_t player, std::vector<uint16_t> groups);
    void setBodyGroup(uint16_t body, uint16_t group); // a replicated body's (spawned ones too)
    // Solid players push bodies (their capsule is in the world); a
    // spectator shouldn't touch the ball. True by default.
    void setSolid(uint8_t player, bool solid);
    bool solid(uint8_t player) const { return !m_ghosts.count(player); }
    // When a snapshot can't hold every player: how much `subject` matters
    // to `viewer` (added up each snapshot until it's sent). Unset: the
    // same group first, then by distance.
    std::function<float(uint8_t viewer, uint8_t subject)> playerPriority;

    // --- input replay (NetConfig::inputReplay; kke/net/InputReplay.h)
    // Each server tick: inputTick() once, then for each client
    //   while (nextInput(id, in)) step its movement with `in`;
    // and setPlayerState if it moved. Usually one input a tick; none while
    // its next hasn't come; a few when late ones arrive together
    // (InputQueue).
    void inputTick();
    bool nextInput(uint8_t id, InputFrame& out);
    // Client `id`'s player as this server's movement left it after the
    // input from nextInput: what everyone else sees, and (InputAck, with
    // each snapshot) its owner's answer.
    void setPlayerState(uint8_t id, const NetPlayerState& state);
    const InputQueue* inputs(uint8_t id) const; // null if unknown

    MovementLimits limits;
    // A move that passed the speed limits: may the player go from `from`
    // to `to` in `dt` seconds? false = refused (the client is corrected
    // back to `from`). Not asked for teleports the limits allow, nor for a
    // player's first state.
    std::function<bool(uint8_t id, const NetPlayerState& from, const NetPlayerState& to, double dt)> checkMove;
    std::function<void(const GameEventMsg&)> onEvent;          // from a client
    std::function<void(uint8_t id)> onProfile;                 // a client's player has a new name or look (players() has it)
    std::function<void(const VoiceMsg&)> onVoice;              // a client's voice the host should hear
    VoiceRules voice;
    std::function<void(uint8_t id, bool joined)> onPlayer;
    // Asked at every join after version, game and password: "" lets the
    // player in, anything else is the reason they're turned away (a ban,
    // an allow list; kke::ServerAccess).
    std::function<std::string(const std::string& name, const std::string& address)> admit;

private:
    // One per player: a connection's own (slot 0) and each of its guests
    // (same peer, slot 1..). Only slot 0 is sent to, polled, rate limited.
    struct Client {
        PeerId peer = kNoPeer;
        uint8_t slot = 0;             // 0: the connection's own player; 1..: a guest on it
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
        double voiceWindow = 0.0;
        size_t voiceInWindow = 0;
        std::map<uint16_t, float> priority; // body id -> accumulated priority
        std::map<uint8_t, float> playerPriority; // player id -> the same, when not all fit
        std::map<uint16_t, bool> sentSleeping;
        InputQueue inputs;            // input replay
        uint32_t ackTick = 0;         // input replay: the input the state is after
        bool ackPending = false;
    };
    Client* byPeer(PeerId peer);                  // the connection (slot 0)
    Client* guestOf(PeerId peer, uint8_t slot);
    Client* byId(uint8_t id);
    uint8_t freeId() const;
    size_t freeSlots() const;
    void handleGuest(Client& c, const GuestMsg& m);
    void handleProfile(Client& c, const ProfileMsg& m);
    void dropGuest(Client& g, const std::string& reason, bool tellOwner);
    bool mayShow(const Client& viewer, uint8_t subject) const; // fog of war: any player on that screen sees it
    void receive(Client& c, const NetEvent& e);
    void handleHello(Client& c, const HelloMsg& m);
    void handleState(Client& c, const PlayerStateMsg& m);
    void bad(Client& c, const char* what);
    void drop(Client& c, const std::string& reason);
    void sendSnapshot(Client& c);
    void broadcastReliable(const std::vector<uint8_t>& data, int exceptPlayer);
    void correct(Client& c);
    void routeVoice(const VoiceMsg& m, const glm::vec3* speakerPos);
    void sendBreaks(PeerId peer, uint16_t id, uint32_t seed, const std::vector<std::pair<uint16_t, uint16_t>>& borders, int exceptPlayer);
    uint32_t timeMs() const { return static_cast<uint32_t>((m_now - m_start) * 1000.0); }

    ITransport& m_transport;
    NetConfig m_config;
    struct BreakSet { uint32_t seed = 0; std::set<std::pair<uint16_t, uint16_t>> borders; };
    std::map<uint16_t, SpawnMsg> m_spawns;  // persistent ones, for late joiners
    std::map<uint16_t, BreakSet> m_breaks;
    size_t m_refusedMoves = 0, m_refusedEvents = 0;
    bool m_running = false, m_started = false;
    double m_now = 0.0, m_start = 0.0, m_nextSnapshot = 0.0;
    std::string m_hostName, m_hostCharacter;
    NetPlayerState m_local;
    bool m_hasLocal = false;
    struct LocalGuest {
        uint8_t id = 0;
        std::string name, character;
        NetPlayerState state;
        bool hasState = false;
    };
    std::map<uint8_t, LocalGuest> m_localGuests; // slot -> the host's other players
    std::map<uint8_t, uint16_t> m_groups;               // player -> group (absent: 0)
    std::map<uint8_t, std::vector<uint16_t>> m_showing; // player -> other groups it's sent
    std::map<uint16_t, uint16_t> m_bodyGroups;          // body -> group (absent: 0)
    std::set<uint8_t> m_ghosts;                         // players that aren't solid
    bool sees(const Client& viewer, uint16_t group) const; // any player on that screen
    void forgetPlayer(uint8_t id);
    const NetPlayerState* stateOf(uint8_t id) const;    // where a player is now, if known
    std::vector<NetBodyState> m_bodies;
    std::vector<Client> m_clients;
    size_t m_badPackets = 0, m_corrections = 0;
    size_t m_voiceRelayed = 0;
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
    // Rejected after we were in: the host ended the game or kicked us, and
    // said so (statusText has why). Not a lost connection.
    bool endedByServer() const { return m_endedByServer; }
    uint8_t playerId() const { return m_playerId; }

    void setLocalState(const NetPlayerState& state) { m_local = state; m_hasLocal = true; }
    // More players on this screen, slots 1 .. kMaxLocalPlayers - 1 (split
    // screen online). May be called before connecting: they join right
    // after the Welcome. guestId() is 0 until the server gives one
    // (onGuest), and again after it refused or removed that player.
    void addGuest(uint8_t slot, const std::string& name, const std::string& character);
    void removeGuest(uint8_t slot);
    void setGuestState(uint8_t slot, const NetPlayerState& state);
    uint8_t guestId(uint8_t slot) const;
    // Our own player picked another name or look (in the lobby, after
    // joining): sent now if connected, else used by the next connect().
    // A guest's goes through addGuest with the same slot.
    void setProfile(const std::string& name, const std::string& character);
    bool isOurs(uint8_t playerId) const; // our own player or one of our guests
    void sendEvent(uint16_t kind, const std::vector<uint8_t>& payload);
    // Input replay: the server said (at Welcome) it moves our player from
    // our inputs. Then send inputs each tick, not states (setLocalState
    // is ignored), and correct with onInputAck (kke/net/InputReplay.h).
    bool inputReplay() const { return m_inputReplay; }
    uint16_t tickHz() const { return m_tickHz; }
    // Consecutive ticks, oldest first (Prediction::unacknowledged); only
    // the newest kMaxInputsPerMsg go.
    void sendInputs(const std::vector<InputFrame>& frames);
    // Our voice: one Opus frame on `channel` (unreliable; the server decides who hears it).
    void sendVoice(VoiceChannel channel, uint16_t seq, const std::vector<uint8_t>& opusFrame);

    void update(double now);

    std::vector<RemotePlayer> players(double now) const;
    // A server body, interpolated for `now`; false if never seen.
    bool body(uint16_t id, double now, NetBodyState& out) const;
    std::vector<uint16_t> bodyIds() const;
    PeerStats stats() const { return m_transport.stats(m_server); }
    size_t badPackets() const { return m_badPackets; }

    std::function<void(const GameEventMsg&)> onEvent;
    std::function<void(const glm::vec3&)> onCorrection;        // the server put us here
    std::function<void(uint8_t slot, const glm::vec3&)> onGuestCorrection; // ... or that guest
    // A guest got its id (id != 0), or was refused / removed (id 0, reason).
    std::function<void(uint8_t slot, uint8_t id, const std::string& reason)> onGuest;
    std::function<void(uint8_t id, bool joined)> onPlayer;
    std::function<void(uint8_t id)> onPlayerChanged;           // someone already here has a new name or look
    std::function<void(const SpawnMsg&)> onSpawn;              // build your copy
    std::function<void(uint16_t id)> onDespawn;                // remove it
    std::function<void(const BreakMsg&)> onBreak;              // break your copy along these borders
    std::function<void(const VoiceMsg&)> onVoice;              // someone's voice for us (speaker = their id)
    std::function<void(uint32_t tick, const NetPlayerState&)> onInputAck; // input replay: our player after our input `tick`

private:
    struct Player {
        std::string name, character;
        Timeline<NetPlayerState> states;
        uint32_t lastInMs = 0;    // newest snapshot that carried this player
        bool everIn = false;
    };
    void receive(const NetEvent& e);
    double renderTime(double now) const;
    uint32_t timeMs() const { return static_cast<uint32_t>((m_now - m_start) * 1000.0); }

    ITransport& m_transport;
    NetConfig m_config;
    Status m_status = Status::Idle;
    std::string m_statusText;
    bool m_endedByServer = false;
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
    struct Guest {
        std::string name, character;
        uint8_t id = 0;
        NetPlayerState state;
        bool hasState = false, asked = false;
    };
    std::map<uint8_t, Guest> m_guests; // slot -> guest
    void askGuest(uint8_t slot, Guest& g);
    std::map<uint16_t, Timeline<NetBodyState>> m_bodies;
    size_t m_badPackets = 0;
    bool m_inputReplay = false;
    uint16_t m_tickHz = 60;
};

} // namespace kke::net
