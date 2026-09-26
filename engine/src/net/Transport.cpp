#include "kke/net/Transport.h"

#include <algorithm>
#include <chrono>

namespace kke::net {

// ---------------------------------------------------------------- network

bool LoopbackNetwork::bound(uint16_t port) const {
    return std::find(m_ports.begin(), m_ports.end(), port) != m_ports.end();
}

bool LoopbackNetwork::bind(uint16_t port) {
    if (port == 0 || bound(port)) return false;
    m_ports.push_back(port);
    return true;
}

void LoopbackNetwork::unbind(uint16_t port) {
    std::erase(m_ports, port);
    std::erase_if(m_inFlight, [&](const Packet& p) { return p.toPort == port; });
}

uint16_t LoopbackNetwork::freePort() {
    for (uint32_t p = 50000; p < 65535; ++p)
        if (!bound(static_cast<uint16_t>(p))) return static_cast<uint16_t>(p);
    return 0;
}

double LoopbackNetwork::deliveryTime(bool reliable, uint64_t linkKey) {
    std::uniform_real_distribution<float> jitter(-conditions.jitterMs, conditions.jitterMs);
    double at = m_now + std::max(0.0, static_cast<double>(conditions.latencyMs + (conditions.jitterMs > 0.0f ? jitter(m_random) : 0.0f)) / 1000.0);
    if (reliable) {
        // A reliable stream never reorders: a packet can't overtake the one
        // before it (what ENet's in-order delivery guarantees).
        double& last = m_lastReliable[linkKey];
        at = std::max(at, last);
        last = at;
    }
    return at;
}

void LoopbackNetwork::post(Packet p) {
    if (!bound(p.toPort)) return; // nobody there: lost, like UDP
    const bool reliable = p.type != NetEvent::Type::Received || p.channel == Channel::Reliable;
    std::uniform_real_distribution<float> percent(0.0f, 100.0f);
    if (!reliable && conditions.lossPercent > 0.0f && percent(m_random) < conditions.lossPercent) {
        ++m_dropped;
        return;
    }
    const uint64_t link = (static_cast<uint64_t>(p.fromPort) << 16) | p.toPort;
    p.deliverAt = deliveryTime(reliable, link);
    p.order = ++m_order;
    if (!reliable && conditions.duplicatePercent > 0.0f && percent(m_random) < conditions.duplicatePercent) {
        Packet copy = p;
        copy.deliverAt = deliveryTime(false, link);
        copy.order = ++m_order;
        m_inFlight.push_back(std::move(copy));
    }
    m_inFlight.push_back(std::move(p));
}

void LoopbackNetwork::take(uint16_t port, std::vector<Packet>& out) {
    auto due = std::stable_partition(m_inFlight.begin(), m_inFlight.end(),
                                     [&](const Packet& p) { return !(p.toPort == port && p.deliverAt <= m_now); });
    const size_t first = out.size();
    out.insert(out.end(), std::make_move_iterator(due), std::make_move_iterator(m_inFlight.end()));
    m_inFlight.erase(due, m_inFlight.end());
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
              [](const Packet& a, const Packet& b) { return a.deliverAt != b.deliverAt ? a.deliverAt < b.deliverAt : a.order < b.order; });
}

// ---------------------------------------------------------------- loopback transport

bool LoopbackTransport::host(uint16_t port, size_t maxPeers, std::string* error) {
    if (m_port) {
        if (error) *error = "already open";
        return false;
    }
    if (port == 0) port = m_net.freePort();
    if (!m_net.bind(port)) {
        if (error) *error = "port " + std::to_string(port) + " is taken";
        return false;
    }
    m_port = port;
    m_maxPeers = maxPeers;
    m_listening = true;
    return true;
}

PeerId LoopbackTransport::connect(const std::string& /*address: one network, every port reachable*/, uint16_t port, std::string* error) {
    if (!m_port) {
        m_port = m_net.freePort();
        if (!m_net.bind(m_port)) {
            m_port = 0;
            if (error) *error = "no free port";
            return kNoPeer;
        }
    }
    const uint32_t connection = m_net.newConnection();
    const PeerId id = m_nextPeer++;
    m_peers[id] = { port, connection, {}, false };
    m_net.post({ 0.0, 0, port, m_port, connection, NetEvent::Type::Connected, Channel::Reliable, {} });
    return id;
}

PeerId LoopbackTransport::peerFor(uint16_t port, uint32_t connection) const {
    for (const auto& [id, p] : m_peers)
        if (p.port == port && p.connection == connection) return id;
    return kNoPeer;
}

void LoopbackTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || !it->second.connected) return;
    it->second.stats.bytesSent += size;
    m_net.post({ 0.0, 0, it->second.port, m_port, it->second.connection, NetEvent::Type::Received, channel,
                 std::vector<uint8_t>(data, data + size) });
}

void LoopbackTransport::disconnect(PeerId peer) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return;
    m_net.post({ 0.0, 0, it->second.port, m_port, it->second.connection, NetEvent::Type::Disconnected, Channel::Reliable, {} });
    m_peers.erase(it);
}

void LoopbackTransport::poll(std::vector<NetEvent>& out) {
    if (!m_port) return;
    std::vector<LoopbackNetwork::Packet> packets;
    m_net.take(m_port, packets);
    for (LoopbackNetwork::Packet& p : packets) {
        PeerId id = peerFor(p.fromPort, p.connection);
        switch (p.type) {
        case NetEvent::Type::Connected:
            if (id != kNoPeer) {
                // Our connect() was accepted.
                if (!m_peers[id].connected) {
                    m_peers[id].connected = true;
                    out.push_back({ NetEvent::Type::Connected, id, Channel::Reliable, {} });
                }
            } else if (m_listening) {
                size_t connected = 0;
                for (const auto& kv : m_peers) connected += kv.second.connected ? 1 : 0;
                const bool room = connected < m_maxPeers;
                m_net.post({ 0.0, 0, p.fromPort, m_port, p.connection, room ? NetEvent::Type::Connected : NetEvent::Type::Disconnected,
                             Channel::Reliable, {} });
                if (room) {
                    id = m_nextPeer++;
                    m_peers[id] = { p.fromPort, p.connection, {}, true };
                    out.push_back({ NetEvent::Type::Connected, id, Channel::Reliable, {} });
                }
            }
            break;
        case NetEvent::Type::Disconnected:
            if (id != kNoPeer) {
                m_peers.erase(id);
                out.push_back({ NetEvent::Type::Disconnected, id, Channel::Reliable, {} });
            }
            break;
        case NetEvent::Type::Received:
            if (id != kNoPeer && m_peers[id].connected) {
                m_peers[id].stats.bytesReceived += p.data.size();
                out.push_back({ NetEvent::Type::Received, id, p.channel, std::move(p.data) });
            }
            break;
        }
    }
}

PeerStats LoopbackTransport::stats(PeerId peer) const {
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return {};
    PeerStats s = it->second.stats;
    s.rttMs = 2.0f * m_net.conditions.latencyMs;
    s.lossPercent = m_net.conditions.lossPercent;
    return s;
}

void LoopbackTransport::close() {
    if (!m_port) return;
    std::vector<PeerId> ids;
    for (const auto& kv : m_peers) ids.push_back(kv.first);
    for (PeerId id : ids) disconnect(id);
    m_net.unbind(m_port);
    m_port = 0;
    m_listening = false;
}

// ---------------------------------------------------------------- conditioner

namespace {
double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

ConditionedTransport::ConditionedTransport(std::unique_ptr<ITransport> inner, uint32_t seed) : m_inner(std::move(inner)), m_random(seed) {}

void ConditionedTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    if (!conditions.active()) {
        m_inner->send(peer, channel, data, size);
        return;
    }
    std::uniform_real_distribution<float> percent(0.0f, 100.0f);
    std::uniform_real_distribution<float> jitter(-conditions.jitterMs, conditions.jitterMs);
    const bool reliable = channel == Channel::Reliable;
    if (!reliable && conditions.lossPercent > 0.0f && percent(m_random) < conditions.lossPercent) return;
    const double now = steadySeconds();
    const int copies = !reliable && conditions.duplicatePercent > 0.0f && percent(m_random) < conditions.duplicatePercent ? 2 : 1;
    for (int c = 0; c < copies; ++c) {
        double at = now + std::max(0.0, static_cast<double>(conditions.latencyMs + (conditions.jitterMs > 0.0f ? jitter(m_random) : 0.0f)) / 1000.0);
        if (reliable) {
            double& last = m_lastReliable[peer];
            at = std::max(at, last);
            last = at;
        }
        m_queue.push_back({ at, ++m_order, peer, channel, std::vector<uint8_t>(data, data + size) });
    }
}

void ConditionedTransport::poll(std::vector<NetEvent>& out) {
    if (!m_queue.empty()) {
        const double now = steadySeconds();
        std::sort(m_queue.begin(), m_queue.end(), [](const Delayed& a, const Delayed& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
        size_t n = 0;
        while (n < m_queue.size() && m_queue[n].at <= now) {
            m_inner->send(m_queue[n].peer, m_queue[n].channel, m_queue[n].data);
            ++n;
        }
        m_queue.erase(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(n));
    }
    const size_t first = out.size();
    m_inner->poll(out);
    for (size_t i = first; i < out.size(); ++i)
        if (out[i].type == NetEvent::Type::Disconnected) {
            std::erase_if(m_queue, [&](const Delayed& d) { return d.peer == out[i].peer; });
            m_lastReliable.erase(out[i].peer);
        }
}

PeerStats ConditionedTransport::stats(PeerId peer) const {
    PeerStats s = m_inner->stats(peer);
    s.rttMs += 2.0f * conditions.latencyMs;
    return s;
}

void ConditionedTransport::close() {
    m_queue.clear();
    m_lastReliable.clear();
    m_inner->close();
}

} // namespace kke::net
