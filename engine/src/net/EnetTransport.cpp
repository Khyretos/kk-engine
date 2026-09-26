#include "kke/net/EnetTransport.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace kke::net {

namespace {

// enet_initialize/deinitialize are process-wide (WSAStartup on Windows):
// counted, so any number of transports can exist.
std::mutex g_initMutex;
int g_initCount = 0;
std::map<ENetHost*, EnetTransport*> g_hosts; // for the intercept callback (ENetHost has no user pointer)

bool retain() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initCount == 0 && enet_initialize() != 0) return false;
    ++g_initCount;
    return true;
}

void release() {
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (--g_initCount == 0) enet_deinitialize();
}

int ENET_CALLBACK interceptThunk(ENetHost* host, ENetEvent*) {
    EnetTransport* t = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_initMutex);
        auto it = g_hosts.find(host);
        if (it != g_hosts.end()) t = it->second;
    }
    return t ? t->intercept(host) : 0;
}

void registerHost(ENetHost* host, EnetTransport* t) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    g_hosts[host] = t;
    host->intercept = interceptThunk;
}

void unregisterHost(ENetHost* host) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    g_hosts.erase(host);
}

// Discovery datagrams. 8-byte magics; see the class comment for why an
// ENet host can't mistake them for its own packets.
constexpr char kQuery[8] = { 'K', 'K', 'E', '?', 'l', 'a', 'n', '1' };
constexpr char kAnswer[8] = { 'K', 'K', 'E', '!', 'l', 'a', 'n', '1' };
constexpr size_t kMaxInfo = 200;

PeerId peerId(const ENetPeer* p) { return static_cast<PeerId>(reinterpret_cast<uintptr_t>(p->data)); }

} // namespace

EnetTransport::EnetTransport() {
    if (!retain()) m_port = 0; // every call then fails with a reason
    else m_initialized = true;
}

EnetTransport::~EnetTransport() {
    close();
    if (m_initialized) release();
}

bool EnetTransport::host(uint16_t port, size_t maxPeers, std::string* error) {
    if (!m_initialized) {
        if (error) *error = "ENet failed to initialize (no sockets on this system?)";
        return false;
    }
    if (m_host) {
        if (error) *error = "already open";
        return false;
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    m_host = enet_host_create(&address, maxPeers, 2, 0, 0);
    if (!m_host) {
        if (error) *error = "can't open UDP port " + std::to_string(port) + " (in use by another program or instance?)";
        return false;
    }
    m_port = m_host->address.port;
    enet_socket_set_option(m_host->socket, ENET_SOCKOPT_BROADCAST, 1);
    registerHost(m_host, this);
    return true;
}

bool EnetTransport::ensureClientHost(std::string* error) {
    if (m_host) return true;
    if (!m_initialized) {
        if (error) *error = "ENet failed to initialize (no sockets on this system?)";
        return false;
    }
    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) {
        if (error) *error = "can't open a UDP socket";
        return false;
    }
    enet_socket_set_option(m_host->socket, ENET_SOCKOPT_BROADCAST, 1);
    registerHost(m_host, this);
    ENetAddress bound{};
    if (enet_socket_get_address(m_host->socket, &bound) == 0) m_port = bound.port;
    return true;
}

PeerId EnetTransport::connect(const std::string& address, uint16_t port, std::string* error) {
    if (!ensureClientHost(error)) return kNoPeer;
    ENetAddress to{};
    if (enet_address_set_host(&to, address.c_str()) != 0) {
        if (error) *error = "unknown address '" + address + "'";
        return kNoPeer;
    }
    to.port = port;
    ENetPeer* peer = enet_host_connect(m_host, &to, 2, 0);
    if (!peer) {
        if (error) *error = "no free connection slot";
        return kNoPeer;
    }
    const PeerId id = m_nextPeer++;
    peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    enet_peer_timeout(peer, 32, 3000, 10000); // give up on a silent peer after 3-10 s
    m_peers[id] = peer;
    return id;
}

void EnetTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !m_host) return;
    // Unreliable = sequenced (ENet drops one that arrives after a newer one).
    const enet_uint32 flags = channel == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(data, size, flags);
    if (!packet) return;
    if (enet_peer_send(it->second, static_cast<enet_uint8>(channel), packet) != 0) enet_packet_destroy(packet);
}

void EnetTransport::disconnect(PeerId peer) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return;
    enet_peer_disconnect_later(it->second, 0); // after what's queued for it
    it->second->data = nullptr;
    m_peers.erase(it);
}

void EnetTransport::poll(std::vector<NetEvent>& out) {
    if (!m_host) return;
    ENetEvent e;
    // Service until nothing is left (0 ms: never blocks the frame).
    while (enet_host_service(m_host, &e, 0) > 0) {
        switch (e.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            PeerId id = peerId(e.peer);
            if (id == kNoPeer) { // someone connected to us
                id = m_nextPeer++;
                e.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
                enet_peer_timeout(e.peer, 32, 3000, 10000);
                m_peers[id] = e.peer;
            }
            out.push_back({ NetEvent::Type::Connected, id, Channel::Reliable, {} });
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            const PeerId id = peerId(e.peer);
            if (id != kNoPeer) {
                m_peers.erase(id);
                e.peer->data = nullptr;
                out.push_back({ NetEvent::Type::Disconnected, id, Channel::Reliable, {} });
            }
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            const PeerId id = peerId(e.peer);
            if (id != kNoPeer && e.channelID < 2)
                out.push_back({ NetEvent::Type::Received, id, static_cast<Channel>(e.channelID),
                                std::vector<uint8_t>(e.packet->data, e.packet->data + e.packet->dataLength) });
            enet_packet_destroy(e.packet);
            break;
        }
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
    enet_host_flush(m_host);
}

PeerStats EnetTransport::stats(PeerId peer) const {
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return {};
    const ENetPeer* p = it->second;
    PeerStats s;
    s.rttMs = static_cast<float>(p->roundTripTime);
    s.lossPercent = 100.0f * static_cast<float>(p->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
    s.bytesSent = p->outgoingDataTotal;
    s.bytesReceived = p->incomingDataTotal;
    return s;
}

void EnetTransport::close() {
    if (!m_host) return;
    for (auto& [id, peer] : m_peers) {
        peer->data = nullptr;
        enet_peer_disconnect_now(peer, 0); // tells the other side right away
    }
    m_peers.clear();
    enet_host_flush(m_host);
    unregisterHost(m_host);
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_port = 0;
}

bool EnetTransport::discover(uint16_t firstPort, uint16_t lastPort) {
    if (!ensureClientHost(nullptr)) return false;
    m_lanGames.clear();
    ENetBuffer buf;
    buf.data = const_cast<char*>(kQuery);
    buf.dataLength = sizeof(kQuery);
    for (uint32_t port = firstPort; port <= lastPort; ++port) {
        if (port == m_port) continue; // not ourselves
        ENetAddress to{};
        to.port = static_cast<enet_uint16>(port);
        to.host = ENET_HOST_BROADCAST;
        enet_socket_send(m_host->socket, &to, &buf, 1);
        // Broadcasts don't reach this machine's own sockets everywhere
        // (and never on loopback-only setups): ask localhost too.
        if (enet_address_set_host_ip(&to, "127.0.0.1") == 0) enet_socket_send(m_host->socket, &to, &buf, 1);
    }
    return true;
}

int EnetTransport::intercept(ENetHost* host) {
    const size_t n = host->receivedDataLength;
    const uint8_t* d = host->receivedData;
    if (n < 8) return 0;
    if (std::memcmp(d, kQuery, 8) == 0) {
        if (m_discoveryInfo.empty()) return 1; // not hosting: swallow, don't answer
        std::vector<uint8_t> reply(kAnswer, kAnswer + 8);
        reply.insert(reply.end(), m_discoveryInfo.begin(), m_discoveryInfo.begin() + static_cast<std::ptrdiff_t>(std::min(m_discoveryInfo.size(), kMaxInfo)));
        ENetBuffer buf;
        buf.data = reply.data();
        buf.dataLength = reply.size();
        enet_socket_send(host->socket, &host->receivedAddress, &buf, 1);
        return 1;
    }
    if (std::memcmp(d, kAnswer, 8) == 0) {
        LanGame g;
        char ip[64] = {};
        if (enet_address_get_host_ip(&host->receivedAddress, ip, sizeof(ip)) == 0) g.address = ip;
        g.port = host->receivedAddress.port;
        g.info.assign(reinterpret_cast<const char*>(d + 8), std::min(n - 8, kMaxInfo));
        // The same host can answer twice (broadcast and localhost).
        for (const LanGame& o : m_lanGames)
            if (o.port == g.port && (o.address == g.address || o.info == g.info)) return 1;
        m_lanGames.push_back(std::move(g));
        return 1;
    }
    return 0;
}

} // namespace kke::net
