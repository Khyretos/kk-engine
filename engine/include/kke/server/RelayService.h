#pragma once

// The relay role of kke_server (docs/SERVER_HOSTING.md "Join codes",
// issue #44; the protocol is in kke/net/Relay.h). Anyone can run one:
//
//   ./kke_server --roles relay            UDP 27970, plus 64 ports above it
//
// It hands out join codes to servers that register, tells a player where
// a code's server is, introduces the two so they can punch through their
// routers, and passes packets on for the players who can't. Each relayed
// player gets one of its `slots` ports (27971 and up by default), so the
// server sees every relayed player at a port of their own.
//
// What it guards against:
//   - guessing codes: Lookups are rate-limited per address and must be
//     padded, so a spoofed one can't make it an amplifier;
//   - taking a code: only the server that registered it (its secret)
//     can move it or say bye;
//   - being a free proxy: only a player who looked a code up gets a
//     slot, packets go only between that player and that server, and
//     each slot has a bandwidth cap;
//   - reading the game: the traffic is encrypted end to end
//     (kke/net/SecureTransport.h); the relay passes on bytes it can't read.
//
// RelayCore is the logic (tests drive it with fake addresses);
// RelayService puts it on real UDP sockets. Built with KKE_ENABLE_NET.

#include "kke/net/Relay.h"
#include "kke/server/Directory.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kke::server {

class UdpSocket;

class RelayCore {
public:
    struct Limits {
        size_t slots = 64;                 // relayed players at once (one port each)
        size_t serversPerAddress = 8;
        size_t maxServers = 4096;
        size_t sessionsPerServer = 64;
        size_t sessionsPerClient = 4;      // per client IP
        double serverTimeout = 30.0;       // seconds without a Register
        double sessionTimeout = 30.0;      // seconds without traffic (or, before it starts, 15)
        double bytesPerSecond = 512.0 * 1024.0; // per slot and direction
    };

    explicit RelayCore(Limits limits) : m_limits(limits), m_slots(limits.slots) {}
    RelayCore() : RelayCore(Limits{}) {}

    // A datagram arrived on socket `socket` (0 = the relay's port, 1..slots
    // = a player's slot) from host:port.
    void receive(size_t socket, const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now);
    void update(double now); // forgets silent servers and sessions

    // Send `data` from socket `socket` to host:port.
    std::function<void(size_t socket, const std::string& host, uint16_t port, const std::vector<uint8_t>& data)> send;
    // Port of slot i (1-based), told to servers so they can open their router to it.
    std::function<uint16_t(size_t slot)> slotPort;
    std::function<void(const std::string& line)> log;

    size_t servers() const { return m_servers.size(); }
    size_t sessions() const;
    size_t refused() const { return m_refused; }         // lookups over the limit, junk, strangers
    uint64_t bytesRelayed() const { return m_bytes; }
    const Limits& limits() const { return m_limits; }

private:
    struct Server {
        std::string code, game, host;
        uint16_t port = 0;
        net::relay::Token secret{};
        net::relay::Key key{};
        double lastSeen = 0;
    };
    struct Session {
        bool used = false;
        net::relay::Token token{};
        std::string code;
        std::string clientHost, serverHost; // serverHost: where the slot's Open came from ("" until then)
        uint16_t clientPort = 0, serverPort = 0;
        double lastActivity = 0, started = 0;
        double toServerBudget = 0, toClientBudget = 0, budgetAt = 0;
        bool active = false;               // a packet went through
    };
    void onMain(const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now);
    void onSlot(size_t slot, const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now);
    void refuse(const std::string& host, uint16_t port, const std::string& why);
    std::string freshCode() const;
    bool spend(Session& s, bool toServer, size_t bytes, double now);
    void endSession(Session& s);

    Limits m_limits;
    std::map<std::string, Server> m_servers;                  // by code
    std::vector<Session> m_slots;                             // index = slot - 1
    std::map<std::pair<std::string, uint16_t>, size_t> m_byClient; // client address -> slot index
    RateLimiter m_lookups{ 10, 1 };                           // per address: a few tries, then one a second
    RateLimiter m_registers{ 20, 2 };
    size_t m_refused = 0;
    uint64_t m_bytes = 0;
};

class RelayService {
public:
    RelayService();
    ~RelayService();
    // UDP `port` and `slots` ports right above it.
    bool start(uint16_t port, size_t slots, std::string* error = nullptr);
    void stop();
    bool running() const;
    void update(double now);
    const RelayCore* core() const { return m_core.get(); }
    std::function<void(const std::string& line)> log;

private:
    std::unique_ptr<RelayCore> m_core;
    std::vector<std::unique_ptr<UdpSocket>> m_sockets; // [0] the relay's port, then the slots
    uint16_t m_port = 0;
};

} // namespace kke::server
