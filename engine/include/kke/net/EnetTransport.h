#pragma once

#include "kke/net/LocalNetworks.h"
#include "kke/net/Relay.h"
#include "kke/net/Transport.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace kke::net {

// ENet's process-wide start (WSAStartup on Windows), counted: for other
// code using enet sockets directly (the server directory). Pair them.
bool enetRetain();
void enetRelease();

// ITransport over ENet (MIT, http://enet.bespin.org): reliable ordered and
// unreliable sequenced channels over one UDP socket, with RTT and loss
// measurement and its own keep-alive/timeout. Built when KKE_ENABLE_NET is
// on (the default).
//
// LAN discovery rides on the same socket: a searcher sends a small query
// datagram to the discovery port range (broadcast and localhost), and
// every host's socket answers with its info string. The query can't be
// mistaken for an ENet packet: its first two bytes read as peer id 2891,
// far above any peer count we allow, and ENet's intercept hook sees it
// first. Several hosts on one PC each have their own port in the range,
// so all of them answer.
//
// VPNs need no setup either (docs/NETWORKING.md "Finding games"): a
// tunnel such as WireGuard carries no broadcast, so a search also asks
// every address of each tunnel network (kke/net/LocalNetworks.h), a few
// hundred tiny datagrams spread over the next frames. A host's firewall
// may drop those questions (Windows puts a WireGuard tunnel on the Public
// profile), so a host also announces itself to every tunnel address now
// and then. With discoveryPorts set, a client's socket takes a port in
// that range, the announcements reach it, and each side has sent to the
// other first: both firewalls let the answer and the join through, the
// same way hole punching gets through a NAT.
//
// Join codes (kke/net/Relay.h) ride on the same socket too: relay
// datagrams (sendRaw, onRaw) share the NAT mapping players connect through.
class EnetTransport : public ITransport, public RawSocket {
public:
    EnetTransport();
    ~EnetTransport() override;

    bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) override;
    PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) override;
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    void disconnect(PeerId peer) override;
    void poll(std::vector<NetEvent>& out) override;
    PeerStats stats(PeerId peer) const override;
    std::string address(PeerId peer) const override;
    uint16_t port() const override { return m_port; }
    void close() override;
    const char* backendName() const override { return "ENet"; }
    using ITransport::send;

    // --- relay datagrams (RawSocket)
    // A client's socket before connect(), so a join code's lookup and
    // punches go out from the port the connection will use.
    bool open(std::string* error = nullptr) { return ensureClientHost(error); }
    bool sendRaw(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) override;
    std::string resolve(const std::string& host) override;
    // Where a peer really is when it came through a relay (the relay's
    // port for it -> the player's address): bans and logs use this.
    std::function<std::string(const std::string& host, uint16_t port)> realAddress;

    // --- LAN discovery
    struct LanGame {
        std::string address;   // "192.168.1.20"
        uint16_t port = 0;
        std::string info;      // what the host set with setDiscoveryInfo
        double seenAt = 0.0;   // when it last answered or announced (steady clock, s)
    };
    // What this host answers discovery queries with (name, players, ...).
    void setDiscoveryInfo(const std::string& info) { m_discoveryInfo = info; }
    // The discovery port range. Set before host() or connect(): a client
    // socket then takes the first free port in it, and a host announces
    // itself on tunnels to the first few ports of it (where clients are).
    void setDiscoveryPorts(uint16_t first, uint16_t last);
    // Sends a query to every port in [firstPort, lastPort] on the LAN
    // broadcast addresses and on localhost, and to the first few of
    // those ports on every address of each VPN tunnel. Answers (and
    // hosts' announcements) arrive through poll() and collect in
    // lanGames(); a game that stops answering drops out after a while.
    bool discover(uint16_t firstPort, uint16_t lastPort);
    const std::vector<LanGame>& lanGames() const { return m_lanGames; }
    // Ports asked on each tunnel address (a PC rarely hosts more games).
    static constexpr uint16_t kSweepPorts = 4;
    static constexpr double kAnnounceEvery = 4.0; // s, while hosting
    static constexpr double kForgetAfter = 10.0;  // s without an answer
    // The networks searches and announcements go to (tests point it at
    // a pretend tunnel on loopback).
    std::function<std::vector<LocalNetwork>()> listNetworks = localNetworks;

    // Internal (ENet's intercept callback).
    int intercept(ENetHost* host);

private:
    bool ensureClientHost(std::string* error);
    struct Datagram { uint32_t host = 0; uint16_t port = 0; bool announce = false; }; // host byte order
    void queueSweep(uint16_t firstPort, uint16_t lastPort, bool announce);
    void sendQueued();
    void announceNow(double t);
    uint16_t m_discoveryFirst = 0, m_discoveryLast = 0;
    std::vector<Datagram> m_outbox; // a sweep, sent a slice each poll()
    double m_announceAt = 0.0;
    ENetHost* m_host = nullptr;
    bool m_initialized = false;
    uint16_t m_port = 0;
    std::map<PeerId, ENetPeer*> m_peers;
    PeerId m_nextPeer = 1;
    std::string m_discoveryInfo;
    std::vector<LanGame> m_lanGames;
};

} // namespace kke::net
