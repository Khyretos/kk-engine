#include "kke/net/EnetTransport.h"

#include <enet/enet.h>

#if defined(_WIN32)
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET // MinGW's headers lack it (Windows SDK: mstcpip.h)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

#include <algorithm>
#include <chrono>
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

double steadySeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// Datagrams of a sweep sent per poll(): a /24 tunnel's few thousand go
// out over a few frames, never filling the socket's send buffer.
constexpr size_t kSweepSlice = 256;

// What every socket of ours needs.
void setUp(ENetHost* host) {
    enet_socket_set_option(host->socket, ENET_SOCKOPT_BROADCAST, 1);
#if defined(_WIN32)
    // A sweep asks addresses with no game: Windows would turn each "port
    // unreachable" that comes back into an error on the next receive,
    // and ENet would stop reading for that frame. Like any game server, ignore them.
    BOOL report = FALSE;
    DWORD bytes = 0;
    WSAIoctl(host->socket, SIO_UDP_CONNRESET, &report, sizeof report, nullptr, 0, &bytes, nullptr, nullptr);
#endif
}

} // namespace

bool enetRetain() { return retain(); }
void enetRelease() { release(); }

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
    setUp(m_host);
    registerHost(m_host, this);
    m_announceAt = 0.0; // tell the tunnels right away
    return true;
}

bool EnetTransport::ensureClientHost(std::string* error) {
    if (m_host) return true;
    if (!m_initialized) {
        if (error) *error = "ENet failed to initialize (no sockets on this system?)";
        return false;
    }
    // In the discovery range when there is one, where hosts announce
    // themselves (see the class comment); any free port otherwise.
    for (uint32_t p = m_discoveryFirst; m_discoveryFirst && !m_host && p <= m_discoveryLast; ++p) {
        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = static_cast<enet_uint16>(p);
        m_host = enet_host_create(&address, 1, 2, 0, 0);
    }
    if (!m_host) m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) {
        if (error) *error = "can't open a UDP socket";
        return false;
    }
    setUp(m_host);
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
    if (!m_discoveryInfo.empty() && m_discoveryFirst) {
        const double t = steadySeconds();
        if (t >= m_announceAt) announceNow(t);
    }
    sendQueued();
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

std::string EnetTransport::address(PeerId peer) const {
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return {};
    char ip[64] = {};
    if (enet_address_get_host_ip(&it->second->address, ip, sizeof ip) != 0) return {};
    if (realAddress)
        if (std::string real = realAddress(ip, it->second->address.port); !real.empty()) return real;
    return ip;
}

bool EnetTransport::sendRaw(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) {
    if (!m_host || data.empty()) return false;
    ENetAddress to{};
    to.port = port;
    if (enet_address_set_host_ip(&to, host.c_str()) != 0 && enet_address_set_host(&to, host.c_str()) != 0) return false;
    ENetBuffer buf;
    buf.data = const_cast<uint8_t*>(data.data());
    buf.dataLength = data.size();
    return enet_socket_send(m_host->socket, &to, &buf, 1) == static_cast<int>(data.size());
}

std::string EnetTransport::resolve(const std::string& host) {
    ENetAddress a{};
    if (enet_address_set_host(&a, host.c_str()) != 0) return {};
    char ip[64] = {};
    if (enet_address_get_host_ip(&a, ip, sizeof ip) != 0) return {};
    return ip;
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
    // Gracefully first: what's queued (a goodbye) arrives before the
    // disconnect (disconnect_now alone can make the other side drop it),
    // and each side hears the other's answer. A moment at most.
    for (auto& [id, peer] : m_peers) enet_peer_disconnect_later(peer, 0);
    size_t left = m_peers.size();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    ENetEvent e;
    while (left > 0 && std::chrono::steady_clock::now() < until && enet_host_service(m_host, &e, 10) >= 0) {
        if (e.type == ENET_EVENT_TYPE_DISCONNECT) --left;
        else if (e.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(e.packet);
        e.type = ENET_EVENT_TYPE_NONE;
    }
    for (auto& [id, peer] : m_peers) {
        peer->data = nullptr;
        if (peer->state != ENET_PEER_STATE_DISCONNECTED) enet_peer_disconnect_now(peer, 0); // no answer: tell it right away
    }
    m_peers.clear();
    m_outbox.clear();
    enet_host_flush(m_host);
    unregisterHost(m_host);
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_port = 0;
}

void EnetTransport::setDiscoveryPorts(uint16_t first, uint16_t last) {
    m_discoveryFirst = first;
    m_discoveryLast = std::max(first, last);
}

bool EnetTransport::discover(uint16_t firstPort, uint16_t lastPort) {
    if (!ensureClientHost(nullptr)) return false;
    const double t = steadySeconds();
    std::erase_if(m_lanGames, [t](const LanGame& g) { return t - g.seenAt > kForgetAfter; });
    const std::vector<LocalNetwork> networks = listNetworks ? listNetworks() : std::vector<LocalNetwork>{};
    ENetBuffer buf;
    buf.data = const_cast<char*>(kQuery);
    buf.dataLength = sizeof(kQuery);
    auto sendTo = [&](uint32_t host, uint32_t port) { // host byte order
        ENetAddress to{};
        to.host = ENET_HOST_TO_NET_32(host);
        to.port = static_cast<enet_uint16>(port);
        enet_socket_send(m_host->socket, &to, &buf, 1);
    };
    for (uint32_t port = firstPort; port <= lastPort; ++port) {
        sendTo(0xFFFFFFFFu, port);
        // Each LAN's own broadcast too: the one above leaves by one
        // network only on some systems (Windows), and a PC can be on two.
        for (const LocalNetwork& n : networks)
            if (const uint32_t b = n.broadcast ? directedBroadcast(n) : 0) sendTo(b, port);
        // Broadcasts don't reach this machine's own sockets everywhere
        // (and never on loopback-only setups): ask localhost too.
        if (port != m_port) sendTo(0x7F000001u, port);
    }
    // VPN tunnels carry no broadcast: ask every address on them.
    if (m_outbox.empty()) queueSweep(firstPort, lastPort, false);
    sendQueued();
    return true;
}

void EnetTransport::queueSweep(uint16_t firstPort, uint16_t lastPort, bool announce) {
    const uint32_t last = std::min<uint32_t>(lastPort, firstPort + kSweepPorts - 1u);
    if (!listNetworks) return;
    for (const LocalNetwork& n : listNetworks()) {
        if (n.broadcast) continue;
        for (uint32_t address : sweepAddresses(n))
            for (uint32_t port = firstPort; port <= last; ++port) m_outbox.push_back({ address, static_cast<uint16_t>(port), announce });
    }
    std::reverse(m_outbox.begin(), m_outbox.end()); // sent from the back
}

void EnetTransport::announceNow(double t) {
    m_announceAt = t + kAnnounceEvery;
    if (m_outbox.empty()) queueSweep(m_discoveryFirst, m_discoveryLast, true);
}

void EnetTransport::sendQueued() {
    if (!m_host || m_outbox.empty()) return;
    std::vector<uint8_t> answer(kAnswer, kAnswer + 8);
    answer.insert(answer.end(), m_discoveryInfo.begin(), m_discoveryInfo.begin() + static_cast<std::ptrdiff_t>(std::min(m_discoveryInfo.size(), kMaxInfo)));
    for (size_t i = 0; i < kSweepSlice && !m_outbox.empty(); ++i) {
        const Datagram d = m_outbox.back();
        m_outbox.pop_back();
        if (d.announce && m_discoveryInfo.empty()) continue; // stopped hosting
        ENetBuffer buf;
        buf.data = d.announce ? static_cast<void*>(answer.data()) : const_cast<char*>(kQuery);
        buf.dataLength = d.announce ? answer.size() : sizeof(kQuery);
        ENetAddress to{};
        to.host = ENET_HOST_TO_NET_32(d.host);
        to.port = d.port;
        enet_socket_send(m_host->socket, &to, &buf, 1);
    }
}

int EnetTransport::intercept(ENetHost* host) {
    const size_t n = host->receivedDataLength;
    const uint8_t* d = host->receivedData;
    if (relay::isRelayDatagram(d, n)) {
        if (onRaw) {
            char ip[64] = {};
            if (enet_address_get_host_ip(&host->receivedAddress, ip, sizeof ip) == 0) onRaw(ip, host->receivedAddress.port, std::vector<uint8_t>(d, d + n));
        }
        return 1; // never ENet's
    }
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
        g.seenAt = steadySeconds();
        // The same host can answer twice (broadcast and localhost) and
        // keeps announcing itself: refresh what we have.
        for (LanGame& o : m_lanGames)
            if (o.port == g.port && (o.address == g.address || o.info == g.info)) {
                if (o.address == g.address) o.info = g.info; // the player count changed
                o.seenAt = g.seenAt;
                return 1;
            }
        m_lanGames.push_back(std::move(g));
        return 1;
    }
    return 0;
}

} // namespace kke::net
