#pragma once

// Join codes: play with friends without opening a port on anyone's
// router (docs/SERVER_HOSTING.md "Join codes", issue #44). What Core
// Keeper's Game ID and Valheim's crossplay do, without Steam or PlayFab:
// the relay is a program anyone can run (kke_server --roles relay).
//
//   1. The server (kke_server, or a game hosting) sends Register to a
//      relay from its game socket, every 10 s. The relay answers with a
//      short code, "K7M-Q2P", and the address it saw (the router's).
//   2. A player types the code. Their game sends Lookup from its game
//      socket; the relay answers Found (the server's address and its
//      encryption key) and tells the server (Introduce) who is coming.
//   3. Both sides send Punch to each other at once: most home routers
//      then let the two talk directly (a UDP hole punch).
//   4. When that fails (strict routers, some mobile networks), the
//      player connects to the relay instead, which passes every packet
//      on, through a port it set aside for this player.
//
// The relay never sees inside: the game traffic is encrypted end to end
// (kke/net/SecureTransport.h) with the server's key, which the player
// checks against the one the relay gave. A code is only good while its
// server is registered, and every Lookup is rate-limited, so codes can't
// be guessed through the relay.
//
// Pure logic over a RawSocket (datagrams on the game's own UDP socket:
// EnetTransport has one), so tests run it on a fake network too.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::net {

constexpr uint16_t kDefaultRelayPort = 27970;

// ---- join codes

// Six characters of Crockford's base 32 (no I, L, O, U): 1 billion codes,
// easy to read out over voice chat. Shown as "K7M-Q2P".
constexpr size_t kJoinCodeLength = 6;
std::string newJoinCode(); // random ("" when the system has no secure random source)
// What a player typed -> the code: case, dashes and spaces don't matter;
// O reads as 0, I and L as 1. nullopt when it isn't a code.
std::optional<std::string> normalizeJoinCode(const std::string& typed);
std::string formatJoinCode(const std::string& code); // "K7MQ2P" -> "K7M-Q2P"

// "K7M-Q2P" (the game's default relay) or "K7M-Q2P@relay.example.org[:port]".
struct JoinTarget {
    std::string code;
    std::string relayHost;
    uint16_t relayPort = kDefaultRelayPort;
};
// `defaultRelay` "host[:port]", used when the text names none.
std::optional<JoinTarget> parseJoinTarget(const std::string& text, const std::string& defaultRelay, std::string* error = nullptr);
// A code (with or without a relay) rather than an address: for one box
// that takes either.
bool looksLikeJoinCode(const std::string& text);

// ---- the game socket's side door

// Datagrams sent and received on the game's own UDP socket, next to its
// connections: the NAT mapping the relay sees is the one players use.
class RawSocket {
public:
    virtual ~RawSocket() = default;
    virtual bool sendRaw(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) = 0;
    // A name to the address datagrams from it will carry ("relay.example.org"
    // -> "203.0.113.7"); "" when it can't be found. May block (DNS).
    virtual std::string resolve(const std::string& host) { return host; }
    // Relay datagrams (isRelayDatagram) that arrived on the socket.
    std::function<void(const std::string& host, uint16_t port, const std::vector<uint8_t>& data)> onRaw;
};

// ---- datagrams (docs/SERVER_HOSTING.md has the table)

namespace relay {

using Token = std::array<uint8_t, 16>;
using Key = std::array<uint8_t, 32>;

enum class Type : uint8_t { Register = 1, Registered, Lookup, Found, Refused, Introduce, Punch, Open, Bye };

// A Lookup is padded to this, and every answer to it is smaller: a
// spoofed Lookup can't turn a relay into a traffic amplifier.
constexpr size_t kLookupSize = 200;

struct Register {
    std::string game;
    Token secret{};          // proves it's the same server re-registering (keeps its code)
    std::string code;        // the code it had ("" = any): kept across restarts
    Key serverKey{};         // its encryption key, handed to players who look it up
};
struct Registered {
    std::string code;
    std::string publicHost;  // where the relay sees the server (its router)
    uint16_t publicPort = 0;
};
struct Lookup {
    std::string code, game;
};
struct Found {
    Token token{};           // this join: punches and the relayed path carry it
    std::string serverHost;
    uint16_t serverPort = 0;
    Key serverKey{};
};
struct Refused {
    std::string reason;
};
struct Introduce {
    Token token{};
    std::string clientHost;
    uint16_t clientPort = 0;
    uint16_t relayPort = 0;  // the port set aside on the relay for this player
};
struct Punch {
    Token token{};
    bool answer = false;     // "I heard yours"
};
struct Open {
    Token token{};           // server -> relayPort: opens its router toward that port
};
struct Bye {
    Token secret{};
};

bool isRelayDatagram(const uint8_t* data, size_t size);
std::optional<Type> typeOf(const std::vector<uint8_t>& d);

std::vector<uint8_t> encode(const Register& m);
std::vector<uint8_t> encode(const Registered& m);
std::vector<uint8_t> encode(const Lookup& m);
std::vector<uint8_t> encode(const Found& m);
std::vector<uint8_t> encode(const Refused& m);
std::vector<uint8_t> encode(const Introduce& m);
std::vector<uint8_t> encode(const Punch& m);
std::vector<uint8_t> encode(const Open& m);
std::vector<uint8_t> encode(const Bye& m);
// nullopt: not that message, damaged, or out of range (it came off the network).
std::optional<Register> decodeRegister(const std::vector<uint8_t>& d);
std::optional<Registered> decodeRegistered(const std::vector<uint8_t>& d);
std::optional<Lookup> decodeLookup(const std::vector<uint8_t>& d);
std::optional<Found> decodeFound(const std::vector<uint8_t>& d);
std::optional<Refused> decodeRefused(const std::vector<uint8_t>& d);
std::optional<Introduce> decodeIntroduce(const std::vector<uint8_t>& d);
std::optional<Punch> decodePunch(const std::vector<uint8_t>& d);
std::optional<Open> decodeOpen(const std::vector<uint8_t>& d);
std::optional<Bye> decodeBye(const std::vector<uint8_t>& d);

} // namespace relay

// ---- a server with a join code

class RelayHost {
public:
    explicit RelayHost(RawSocket& socket) : m_socket(socket) {}
    ~RelayHost() { stop(); }

    // `relays`: "host[:port]" each. `saved`: the code and secret from
    // last time (keepCode) so players' saved codes keep working.
    struct Saved {
        std::string code;
        relay::Token secret{};
    };
    bool start(const std::vector<std::string>& relays, const std::string& game, const relay::Key& serverKey, const Saved* saved = nullptr,
               std::string* error = nullptr);
    void stop(); // says bye, so the code frees at once
    void update(double now);
    // Relay datagrams from the socket (RawSocket::onRaw).
    void onDatagram(const std::string& host, uint16_t port, const std::vector<uint8_t>& data);

    bool registered() const { return m_confirmed; }
    const std::string& code() const { return m_code; } // once registered()
    // What players type: "K7M-Q2P" plus "@relay" when it isn't the only relay they'd know.
    std::string joinText() const;
    Saved saved() const { return { m_code, m_secret }; }
    // A relayed player's real address (bans, logs), by where their
    // packets come from (the relay's port for them); "" = not relayed.
    std::string realAddress(const std::string& host, uint16_t port) const;

    std::function<void(const std::string& line)> log;
    std::function<void(const std::string& code)> onCode; // registered, or the code changed

private:
    struct Relay { std::string name, host; uint16_t port = 0; double lastAnswer = -1; }; // host: `name` resolved
    RawSocket& m_socket;
    std::vector<Relay> m_relays;
    std::string m_game, m_code;
    relay::Token m_secret{};
    relay::Key m_key{};
    double m_nextRegister = 0, m_now = 0;
    bool m_warnedSilent = false, m_confirmed = false;
    struct Relayed { std::string relayHost; uint16_t relayPort; std::string clientHost; double since; };
    std::vector<Relayed> m_relayed;
};

// ---- a player joining by code

class RelayJoin {
public:
    enum class State { Idle, LookingUp, Punching, Direct, Relayed, Failed };

    explicit RelayJoin(RawSocket& socket) : m_socket(socket) {}
    bool start(const JoinTarget& target, const std::string& game, double now, std::string* error = nullptr);
    void update(double now);
    void onDatagram(const std::string& host, uint16_t port, const std::vector<uint8_t>& data);

    State state() const { return m_state; }
    bool done() const { return m_state == State::Direct || m_state == State::Relayed || m_state == State::Failed; }
    const std::string& error() const { return m_error; }
    const std::string& status() const { return m_status; } // for the UI: "asking the relay", "trying a direct connection"
    // Once Direct or Relayed: connect there, and expect this server key.
    const std::string& connectHost() const { return m_connectHost; }
    uint16_t connectPort() const { return m_connectPort; }
    const relay::Key& serverKey() const { return m_found.serverKey; }

    bool forceRelay = false;      // skip the hole punch (tests; networks known to block it)
    double lookupTimeout = 5.0;   // seconds without an answer from the relay
    double punchTime = 1.5;       // seconds to try a direct connection

private:
    void fail(const std::string& why);
    RawSocket& m_socket;
    State m_state = State::Idle;
    JoinTarget m_target;
    std::string m_game, m_error, m_status, m_connectHost;
    uint16_t m_connectPort = 0;
    relay::Found m_found;
    double m_started = 0, m_punchStarted = 0, m_nextSend = 0;
};

} // namespace kke::net
