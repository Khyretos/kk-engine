#pragma once

// The directory role on the network (kke/server/Directory.h has the rules
// and the datagrams). Built with KKE_ENABLE_NET.

#include "kke/server/Directory.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kke::server {

// One non-blocking UDP socket (ENet's portable socket calls).
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port 0 = any free port.
    bool open(uint16_t port, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_socket != kNone; }
    uint16_t port() const { return m_port; }
    // `host`: an IPv4 address or a name (looked up, which can block).
    bool send(const std::string& host, uint16_t port, const std::vector<uint8_t>& data);
    // Every datagram waiting, then returns.
    void receive(const std::function<void(const std::string& address, uint16_t port, const uint8_t* data, size_t size)>& fn);

private:
    static constexpr uint64_t kNone = ~0ull;
    uint64_t m_socket = kNone; // ENetSocket, widened (an int or a SOCKET)
    uint16_t m_port = 0;
    bool m_retained = false;
};

// Runs a directory: keeps public servers' heartbeats, answers queries.
class DirectoryService {
public:
    bool start(uint16_t port, std::string* error = nullptr);
    void stop() { m_socket.close(); }
    bool running() const { return m_socket.isOpen(); }
    void update(double now);
    const DirectoryRegistry& registry() const { return m_registry; }
    size_t refused() const { return m_refused; } // heartbeats turned away, queries over the limit, junk
    std::function<void(const std::string& line)> log;

private:
    UdpSocket m_socket;
    DirectoryRegistry m_registry;
    RateLimiter m_queries;
    RateLimiter m_heartbeats{ 20, 2 };
    double m_lastExpire = 0;
    size_t m_refused = 0;
};

// A public server: heartbeats to each directory, a bye when it stops.
class DirectoryPublisher {
public:
    ~DirectoryPublisher() { stop(); }
    bool start(const std::vector<std::string>& directories, std::string* error = nullptr); // "host:port" each
    void stop(); // says bye
    void setEntry(const DirectoryEntry& e) { m_entry = e; }
    void update(double now);

private:
    UdpSocket m_socket;
    std::vector<std::pair<std::string, uint16_t>> m_directories;
    DirectoryEntry m_entry;
    double m_next = 0;
};

// A game (or a tool) asking a directory for its server list.
class DirectoryBrowser {
public:
    bool query(const std::string& host, uint16_t port, const std::string& game, std::string* error = nullptr);
    void update(); // collects answers, asks for the next page
    const std::vector<DirectoryEntry>& servers() const { return m_servers; }
    bool done() const { return m_done; }

private:
    UdpSocket m_socket;
    std::string m_host, m_game;
    uint16_t m_port = 0;
    std::vector<DirectoryEntry> m_servers;
    bool m_done = false;
};

} // namespace kke::server
