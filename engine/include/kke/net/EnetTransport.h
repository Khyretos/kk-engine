#pragma once

#include "kke/net/Transport.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace kke::net {

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
class EnetTransport : public ITransport {
public:
    EnetTransport();
    ~EnetTransport() override;

    bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) override;
    PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) override;
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    void disconnect(PeerId peer) override;
    void poll(std::vector<NetEvent>& out) override;
    PeerStats stats(PeerId peer) const override;
    uint16_t port() const override { return m_port; }
    void close() override;
    const char* backendName() const override { return "ENet"; }
    using ITransport::send;

    // --- LAN discovery
    struct LanGame {
        std::string address;   // "192.168.1.20"
        uint16_t port = 0;
        std::string info;      // what the host set with setDiscoveryInfo
    };
    // What this host answers discovery queries with (name, players, ...).
    void setDiscoveryInfo(const std::string& info) { m_discoveryInfo = info; }
    // Sends a query to every port in [firstPort, lastPort] on the LAN
    // broadcast address and on localhost. Answers arrive through poll()
    // and collect in lanGames() (cleared by each new search).
    bool discover(uint16_t firstPort, uint16_t lastPort);
    const std::vector<LanGame>& lanGames() const { return m_lanGames; }

    // Internal (ENet's intercept callback).
    int intercept(ENetHost* host);

private:
    bool ensureClientHost(std::string* error);
    ENetHost* m_host = nullptr;
    bool m_initialized = false;
    uint16_t m_port = 0;
    std::map<PeerId, ENetPeer*> m_peers;
    PeerId m_nextPeer = 1;
    std::string m_discoveryInfo;
    std::vector<LanGame> m_lanGames;
};

} // namespace kke::net
