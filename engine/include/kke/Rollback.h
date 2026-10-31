#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kke {

// Rollback netcode (ENG-F55, docs/NETWORKING.md "Rollback"): the technique
// fighting games use (GGPO, Skullgirls, Street Fighter 6). Every player runs
// the whole game. Instead of waiting for the other player's input, each
// machine guesses it (the last thing they did) and carries on at once; when
// the real input arrives and the guess was wrong, the machine loads the
// state from before the mistake and re-runs the frames with the right
// input, faster than anyone can see. With a good connection nobody waits.
//
// The library owns no game state. It tells the game what to do, as a list of
// requests: save the state of frame N, load the state of frame N, advance one
// frame with these inputs. RollbackDriver does it for a game that can save
// and load its state to bytes. The game must be deterministic: the same
// inputs from the same state give the same next state, on every machine
// (fixed time step, no wall-clock reads, no unordered-container order, the
// same random numbers; floats are fine on one CPU architecture and compiler).
// RollbackSyncTest checks that for you.
//
// The transport is up to the game: a session produces opaque packets
// (makePacket) to send to every other player, unreliably (UDP is right:
// every packet repeats the inputs the others have not confirmed yet, so a
// lost one costs nothing), and takes what arrives (receive). Packets are
// checked on the way in; garbage is refused.
//
// Unit-tested in tests/test_rollback.cpp, with a simulated network that loses,
// duplicates, delays and reorders packets.

using Bytes = std::vector<uint8_t>;

struct RollbackConfig {
    int players = 2;            // 1 to 8
    int inputSize = 2;          // bytes of input per player per frame (1 to 64)
    int inputDelay = 2;         // frames between pressing a button and the simulation seeing it: more delay, fewer rollbacks
    int maxPrediction = 8;      // how many frames the simulation may run ahead of the last confirmed input; then it waits
    int checksumInterval = 10;  // compare the game's state with the others' every this many frames (0: never)
};

struct RollbackRequest {
    enum class Kind : uint8_t { Save, Load, Advance } kind = Kind::Advance;
    int frame = 0;                   // Save/Load: the frame whose state (the state at the start of that frame); Advance: the frame to simulate
    std::vector<Bytes> inputs;       // Advance: the input of every player for this frame
    std::vector<bool> predicted;     // Advance: whether each player's input is a guess
};

struct RollbackEvent {
    enum class Kind : uint8_t {
        Desync,        // our state at `frame` differs from player `player`'s: the game is not deterministic (or a packet was tampered with)
        Rollback,      // a guess was wrong: the game went back `frame` frames... (`frame` = the frame it went back to, `other` = how many it re-ran)
        Disconnected,  // `player` was disconnected
    } kind = Kind::Desync;
    int frame = 0;
    int player = 0;
    int other = 0;
    uint64_t local = 0, remote = 0; // Desync: the two checksums
};

struct RollbackStats {
    uint64_t framesAdvanced = 0;    // new frames (not re-runs)
    uint64_t rollbacks = 0;
    uint64_t framesResimulated = 0;
    int maxRollbackDepth = 0;
    uint64_t predictedInputs = 0;   // inputs guessed
    uint64_t mispredictions = 0;    // of those, the ones that were wrong
    uint64_t stalls = 0;            // ticks that waited for the other player (maxPrediction reached)
    uint64_t packetsSent = 0, packetsReceived = 0, packetsRefused = 0;
    uint64_t bytesSent = 0;
};

class RollbackSession {
public:
    explicit RollbackSession(const RollbackConfig& config = {});

    const RollbackConfig& config() const { return m_config; }
    // The player this machine plays. Must be called once, before anything else.
    bool setLocalPlayer(int player);
    int localPlayer() const { return m_local; }

    // The local player's input for the frame about to be simulated (it will be
    // seen inputDelay frames later). Call before every advance(); calling again
    // before the advance replaces it.
    bool addLocalInput(const Bytes& input);

    enum class Status : uint8_t {
        Ok,
        PredictionLimit, // too far ahead of the other players' inputs: nothing was advanced, wait and try again
        NoLocalPlayer,
    };
    // One tick. Fills `out` with what the game does, in order: when a guess turned out wrong, a
    // Load of the frame before it and every frame since re-run (each with a Save before it); then
    // a Save of the new frame's state and its Advance.
    Status advance(std::vector<RollbackRequest>& out);
    // Only the correction, without a new frame (to catch up after a burst of packets).
    void resolve(std::vector<RollbackRequest>& out);
    // The game answers a Save request with a checksum of the state it saved (a hash of its bytes).
    void reportChecksum(int frame, uint64_t checksum);

    // ---- the network
    // The packet to send to every other player this tick (every tick is fine: it is small).
    Bytes makePacket();
    // A packet from another player. False for one that is not valid (nothing in it was used).
    bool receive(const uint8_t* data, size_t size);
    bool receive(const Bytes& packet) { return receive(packet.data(), packet.size()); }

    // ---- state
    int frame() const { return m_frame; }
    // The newest frame for which every player's input is known (-1 before any): everything up to it is final.
    int confirmedFrame() const;
    // How many frames this machine is ahead of what it has heard from the others, minus how far ahead
    // they say they are of it, halved: when it is above 0 this machine runs fast and should wait that
    // many frames for the other to catch up (the game skips that many ticks). 0..8.
    int recommendedWait() const;
    // The number of frames of state a game should keep (a ring of this size, by frame).
    int snapshotSlots() const { return m_config.maxPrediction + 2; }
    // The input the simulation used (or will use) for a frame, if still remembered.
    bool inputFor(int player, int frame, Bytes& out, bool* predicted = nullptr) const;
    // A player who left: from `fromFrame` on their input is zeros, confirmed. Every machine must use the
    // same frame (the game agrees it with its own message, say the highest frame all of them confirmed
    // from that player); -1 takes the first frame this machine has no input for from them.
    bool disconnectPlayer(int player, int fromFrame = -1);
    bool disconnected(int player) const;

    std::vector<RollbackEvent> takeEvents();
    const RollbackStats& stats() const { return m_stats; }

    static constexpr size_t kMaxPacket = 4096;

private:
    struct Used {
        Bytes input;
        bool predicted = false;
    };
    struct Player {
        std::map<int, Bytes> confirmed;
        int contiguous = -1;            // every frame up to this one is confirmed
        std::map<int, Used> used;       // what the simulation was given, by frame
        bool disconnected = false;
        int disconnectFrame = 0;
        int remoteFrame = 0;            // the frame they last said they were at
        int remoteAdvantage = 0;        // what they said their advantage was
        int ackedUs = -1;               // the highest of our inputs they confirm having
        std::map<int, uint64_t> checksums; // theirs, by frame
    };
    Bytes inputOf(int player, int frame, bool& predicted) const;
    void confirm(int player, int frame, const Bytes& input);
    void rollback(std::vector<RollbackRequest>& out);
    void simulate(int frame, std::vector<RollbackRequest>& out);
    void checkChecksums();
    int localAdvantage(int remote) const;

    RollbackConfig m_config;
    int m_local = -1;
    int m_frame = 0;
    int m_firstIncorrect;
    Bytes m_pending;
    bool m_hasPending = false;
    std::vector<Player> m_players;
    std::map<int, uint64_t> m_checksums; // ours, by frame
    int m_lastSentChecksum = -1;
    std::vector<RollbackEvent> m_events;
    RollbackStats m_stats;
    std::map<std::pair<int, int>, bool> m_reportedDesync;
};

// Runs a session for a game that can save and load its whole state.
struct RollbackGame {
    std::function<Bytes()> save;                                  // the whole state, to bytes
    std::function<void(const Bytes&)> load;                       // back from bytes
    std::function<void(const std::vector<Bytes>& inputs)> advance; // one frame with every player's input
    // A checksum of a saved state. Default: FNV-1a of its bytes.
    std::function<uint64_t(const Bytes&)> checksum;
};

class RollbackDriver {
public:
    RollbackDriver(RollbackSession& session, RollbackGame game);
    // One tick of the game: the local input in, the simulation advanced, any correction made.
    RollbackSession::Status tick(const Bytes& localInput);
    // Applies a pending correction without a new frame.
    void settle();
    const RollbackSession& session() const { return m_session; }
    RollbackSession& session() { return m_session; }

private:
    void execute(const std::vector<RollbackRequest>& requests);
    RollbackSession& m_session;
    RollbackGame m_game;
    std::map<int, Bytes> m_snapshots;
};

// Does the game replay the same? From each frame it saves, loads the state of
// `depth` frames ago, runs forward with the same inputs and checks that the
// state is the same one it had (GGPO's sync test). `inputs(frame)` gives each
// player's input. False with the first frame that differs and the first byte
// that does.
bool rollbackSyncTest(const RollbackGame& game, int players, int frames, int depth, const std::function<std::vector<Bytes>(int frame)>& inputs,
                      std::string* report = nullptr);

uint64_t fnv1a(const Bytes& bytes);

} // namespace kke
