#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace kke::net {

// What moves bytes between game instances, behind one small interface so
// the replication code (NetSession.h) never knows which one it runs on
// (NETWORKING.md "Layers"):
//   EnetTransport     ENet over UDP (MIT): LAN, several instances on one
//                     PC, host-a-game over the internet with a forwarded
//                     port. The default.
//   LoopbackTransport in-process, on a manual clock, with simulated
//                     latency, jitter, loss and duplication: the tests.
//   ConditionedTransport  wraps any transport and adds the same simulated
//                     conditions (feel 150 ms of lag on a LAN).
// Later backends (yojimbo for dedicated servers, GameNetworkingSockets for
// NAT traversal) implement the same interface.
//
// Two channels, the pair every action game needs:
//   Reliable    ordered, delivered exactly once (joins, events, chat)
//   Unreliable  may be lost, never retried (snapshots, player states:
//               a late one is useless, the next one supersedes it)

using PeerId = uint32_t;
constexpr PeerId kNoPeer = 0;

enum class Channel : uint8_t { Reliable = 0, Unreliable = 1 };

struct NetEvent {
    enum class Type : uint8_t { Connected, Disconnected, Received };
    Type type = Type::Received;
    PeerId peer = kNoPeer;
    Channel channel = Channel::Reliable;
    std::vector<uint8_t> data;
};

struct PeerStats {
    float rttMs = 0.0f;
    float lossPercent = 0.0f;
    uint64_t bytesSent = 0, bytesReceived = 0;
};

// Simulated network conditions (both directions, per packet).
struct LinkConditions {
    float latencyMs = 0.0f;       // one way
    float jitterMs = 0.0f;        // +- uniform, per packet (reorders unreliable packets)
    float lossPercent = 0.0f;     // unreliable packets only; reliable ones arrive late instead
    float duplicatePercent = 0.0f; // unreliable packets only
    bool active() const { return latencyMs > 0.0f || jitterMs > 0.0f || lossPercent > 0.0f || duplicatePercent > 0.0f; }
};

// Largest message a game should send: fits one UDP packet on any path
// (1200 bytes of payload, below every real-world MTU) so nothing is
// fragmented. Bigger reliable messages work but cost fragments.
constexpr size_t kMaxUnreliableBytes = 1200;

class ITransport {
public:
    virtual ~ITransport() = default;
    // Listen for peers on `port` (0 = any free port). False + a reason when
    // the port is taken or the socket can't be made.
    virtual bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) = 0;
    // Start connecting; a Connected (or Disconnected) event follows from
    // poll(). Returns the server's peer id, or kNoPeer on immediate failure.
    virtual PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) = 0;
    virtual void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) = 0;
    virtual void disconnect(PeerId peer) = 0;
    // Non-blocking: sends what's queued, appends what arrived.
    virtual void poll(std::vector<NetEvent>& out) = 0;
    virtual PeerStats stats(PeerId peer) const = 0;
    virtual uint16_t port() const = 0;
    virtual void close() = 0;
    virtual const char* backendName() const = 0;

    void send(PeerId peer, Channel channel, const std::vector<uint8_t>& data) { send(peer, channel, data.data(), data.size()); }
};

// ------------------------------------------------------------------ loopback

// A whole simulated network in one process: transports "bind" ports on it
// and exchange packets through it. Time is manual (advance()), so tests are
// deterministic and fast: a second of lag takes no wall time.
class LoopbackNetwork {
public:
    explicit LoopbackNetwork(uint32_t seed = 1) : m_random(seed) {}
    LinkConditions conditions;
    void advance(double seconds) { m_now += seconds; }
    double now() const { return m_now; }
    size_t packetsDropped() const { return m_dropped; }

    // Internals, used by LoopbackTransport.
    struct Packet {
        double deliverAt;
        uint64_t order;              // FIFO among equal times
        uint16_t toPort, fromPort;
        uint32_t connection;         // which connection the packet belongs to
        NetEvent::Type type;
        Channel channel;
        std::vector<uint8_t> data;
    };
    bool bind(uint16_t port);
    void unbind(uint16_t port);
    uint16_t freePort();
    void post(Packet p);
    void take(uint16_t port, std::vector<Packet>& out);
    uint32_t newConnection() { return ++m_connections; }
    bool bound(uint16_t port) const;

private:
    double deliveryTime(bool reliable, uint64_t linkKey);
    double m_now = 0.0;
    std::mt19937 m_random;
    std::vector<uint16_t> m_ports;
    std::vector<Packet> m_inFlight;
    std::map<uint64_t, double> m_lastReliable; // per link: reliable stays in order
    uint64_t m_order = 0;
    uint32_t m_connections = 0;
    size_t m_dropped = 0;
};

class LoopbackTransport : public ITransport {
public:
    explicit LoopbackTransport(LoopbackNetwork& network) : m_net(network) {}
    ~LoopbackTransport() override { close(); }

    bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) override;
    PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) override;
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    void disconnect(PeerId peer) override;
    void poll(std::vector<NetEvent>& out) override;
    PeerStats stats(PeerId peer) const override;
    uint16_t port() const override { return m_port; }
    void close() override;
    const char* backendName() const override { return "loopback"; }
    using ITransport::send;

private:
    struct Peer { uint16_t port; uint32_t connection; PeerStats stats; bool connected; };
    PeerId peerFor(uint16_t port, uint32_t connection) const;
    LoopbackNetwork& m_net;
    uint16_t m_port = 0;
    size_t m_maxPeers = 0;
    bool m_listening = false;
    std::map<PeerId, Peer> m_peers;
    PeerId m_nextPeer = 1;
};

// ------------------------------------------------------------------ conditioner

// Adds LinkConditions to any transport, on outgoing packets (so two
// conditioned peers see the sum). Real time (steady clock).
class ConditionedTransport : public ITransport {
public:
    explicit ConditionedTransport(std::unique_ptr<ITransport> inner, uint32_t seed = 7);
    LinkConditions conditions;
    ITransport& inner() { return *m_inner; }

    bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) override { return m_inner->host(port, maxPeers, error); }
    PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) override {
        return m_inner->connect(address, port, error);
    }
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    void disconnect(PeerId peer) override { m_inner->disconnect(peer); }
    void poll(std::vector<NetEvent>& out) override;
    PeerStats stats(PeerId peer) const override;
    uint16_t port() const override { return m_inner->port(); }
    void close() override;
    const char* backendName() const override { return m_inner->backendName(); }
    using ITransport::send;

private:
    struct Delayed { double at; uint64_t order; PeerId peer; Channel channel; std::vector<uint8_t> data; };
    std::unique_ptr<ITransport> m_inner;
    std::mt19937 m_random;
    std::vector<Delayed> m_queue;
    std::map<PeerId, double> m_lastReliable;
    uint64_t m_order = 0;
};

} // namespace kke::net
