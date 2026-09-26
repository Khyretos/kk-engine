#include "kke/server/DirectoryNet.h"

#include "kke/net/EnetTransport.h"
#include "kke/server/ServerConfig.h"

#include <enet/enet.h>

namespace kke::server {

namespace {
ENetSocket sock(uint64_t s) { return static_cast<ENetSocket>(s); }
} // namespace

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port, std::string* error) {
    close();
    if (!net::enetRetain()) {
        if (error) *error = "networking couldn't start";
        return false;
    }
    m_retained = true;
    const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) {
        if (error) *error = "couldn't make a UDP socket";
        close();
        return false;
    }
    m_socket = static_cast<uint64_t>(s);
    ENetAddress a{};
    a.host = ENET_HOST_ANY;
    a.port = port;
    if (enet_socket_bind(s, &a) != 0) {
        if (error) *error = "UDP port " + std::to_string(port) + " is taken (another server on it?)";
        close();
        return false;
    }
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
    ENetAddress bound{};
    m_port = enet_socket_get_address(s, &bound) == 0 ? bound.port : port;
    return true;
}

void UdpSocket::close() {
    if (m_socket != kNone) enet_socket_destroy(sock(m_socket));
    m_socket = kNone;
    m_port = 0;
    if (m_retained) net::enetRelease();
    m_retained = false;
}

bool UdpSocket::send(const std::string& host, uint16_t port, const std::vector<uint8_t>& data) {
    if (!isOpen() || data.empty()) return false;
    ENetAddress to{};
    to.port = port;
    if (enet_address_set_host_ip(&to, host.c_str()) != 0 && enet_address_set_host(&to, host.c_str()) != 0) return false;
    ENetBuffer buf;
    buf.data = const_cast<uint8_t*>(data.data());
    buf.dataLength = data.size();
    return enet_socket_send(sock(m_socket), &to, &buf, 1) == static_cast<int>(data.size());
}

void UdpSocket::receive(const std::function<void(const std::string&, uint16_t, const uint8_t*, size_t)>& fn) {
    if (!isOpen()) return;
    uint8_t data[kMaxDatagram + 1];
    for (int i = 0; i < 1024; ++i) { // a flood can't keep us here forever
        ENetAddress from{};
        ENetBuffer buf;
        buf.data = data;
        buf.dataLength = sizeof data;
        const int n = enet_socket_receive(sock(m_socket), &from, &buf, 1);
        if (n <= 0) return; // nothing waiting (0) or an error on a UDP socket (an ICMP echo): try next frame
        char ip[64] = {};
        if (enet_address_get_host_ip(&from, ip, sizeof ip) != 0) continue;
        fn(ip, from.port, data, static_cast<size_t>(n));
    }
}

// ---- service

bool DirectoryService::start(uint16_t port, std::string* error) { return m_socket.open(port, error); }

void DirectoryService::update(double now) {
    m_socket.receive([&](const std::string& address, uint16_t port, const uint8_t* data, size_t size) {
        const DirectoryDecoded d = decodeDirectory(data, size);
        switch (d.type) {
        case DirectoryMessage::Heartbeat: {
            if (!m_heartbeats.allow(address, now)) {
                ++m_refused;
                return;
            }
            if (d.heartbeat.bye) {
                if (m_registry.bye(address, d.heartbeat.entry.port) && log) log("directory: '" + d.heartbeat.entry.name + "' at " + address + " stopped");
                return;
            }
            const size_t before = m_registry.size();
            const std::string why = m_registry.heartbeat(d.heartbeat.entry, address, now);
            if (!why.empty()) {
                ++m_refused;
                if (log) log("directory: turned away '" + d.heartbeat.entry.name + "' from " + address + ": " + why);
            } else if (m_registry.size() > before && log) {
                log("directory: listed '" + d.heartbeat.entry.name + "' (" + d.heartbeat.entry.game + ") at " + address + ":" + std::to_string(d.heartbeat.entry.port));
            }
            return;
        }
        case DirectoryMessage::Query:
            if (!m_queries.allow(address, now)) {
                ++m_refused;
                return;
            }
            m_socket.send(address, port, encodeList(m_registry.list(d.query.game), d.query.from));
            return;
        case DirectoryMessage::List:
        case DirectoryMessage::None:
            ++m_refused; // not for a directory, or junk
            return;
        }
    });
    if (now - m_lastExpire >= 1.0) {
        m_registry.expire(now);
        m_lastExpire = now;
    }
}

// ---- publisher

bool DirectoryPublisher::start(const std::vector<std::string>& directories, std::string* error) {
    m_directories.clear();
    for (const std::string& d : directories) {
        std::string host;
        uint16_t port = 0;
        if (!splitHostPort(d, host, port)) {
            if (error) *error = "'" + d + "' isn't host:port";
            return false;
        }
        m_directories.emplace_back(host, port);
    }
    m_next = 0;
    return m_socket.open(0, error);
}

void DirectoryPublisher::stop() {
    if (!m_socket.isOpen()) return;
    DirectoryHeartbeat bye{ m_entry, true };
    for (const auto& [host, port] : m_directories) m_socket.send(host, port, encode(bye));
    m_socket.close();
}

void DirectoryPublisher::update(double now) {
    if (!m_socket.isOpen() || now < m_next) return;
    m_next = now + kHeartbeatSeconds;
    const std::vector<uint8_t> hb = encode(DirectoryHeartbeat{ m_entry, false });
    for (const auto& [host, port] : m_directories) m_socket.send(host, port, hb);
    m_socket.receive([](const std::string&, uint16_t, const uint8_t*, size_t) {}); // directories never answer; drop strays
}

// ---- browser

bool DirectoryBrowser::query(const std::string& host, uint16_t port, const std::string& game, std::string* error) {
    m_servers.clear();
    m_done = false;
    m_host = host;
    m_port = port;
    m_game = game;
    if (!m_socket.isOpen() && !m_socket.open(0, error)) return false;
    if (!m_socket.send(host, port, encode(DirectoryQuery{ game, 0 }))) {
        if (error) *error = "couldn't reach " + host + " (is the name right?)";
        return false;
    }
    return true;
}

void DirectoryBrowser::update() {
    m_socket.receive([&](const std::string&, uint16_t from, const uint8_t* data, size_t size) {
        if (from != m_port) return; // not the directory we asked
        const DirectoryDecoded d = decodeDirectory(data, size);
        if (d.type != DirectoryMessage::List || m_done) return;
        for (const DirectoryEntry& e : d.list.servers)
            if (m_game.empty() || e.game == m_game) m_servers.push_back(e);
        if (d.list.next && d.list.next < d.list.total && m_servers.size() < DirectoryRegistry::kMaxEntries)
            m_socket.send(m_host, m_port, encode(DirectoryQuery{ m_game, d.list.next }));
        else
            m_done = true;
    });
}

} // namespace kke::server
