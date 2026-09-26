#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::server {

// The directory role: a server list anyone can run (docs/SERVER_HOSTING.md
// "Servers helping each other"). Public servers send it a heartbeat every
// kHeartbeatSeconds; games ask it for the servers of their game. All of it
// is small JSON datagrams on one UDP port, each starting with kMagic.
//
// It is built to be put on the internet:
//   - an entry's address is where its heartbeats come from, never what the
//     packet claims (so nobody can list someone else's machine);
//   - at most kMaxPerAddress entries per address, kMaxEntries in all;
//   - queries are rate-limited per address, and must be padded to
//     kMinQueryBytes so an answer (at most kMaxDatagram) is never much
//     bigger than the question: spoofing a query can't turn the directory
//     into an amplifier;
//   - heartbeats get no answer at all.
struct DirectoryEntry {
    std::string name;
    std::string game;
    std::string address; // filled by the directory: the heartbeat's source
    uint16_t port = 0;   // the game port (heartbeats may come from another)
    uint16_t players = 0, maxPlayers = 0;
    bool password = false;
    std::vector<std::string> roles;
    uint32_t protocol = 0; // kke::net::kProtocolVersion of the server
};

constexpr const char* kDirectoryMagic = "KKEDIR1\n";
constexpr double kHeartbeatSeconds = 10.0;
constexpr double kDirectoryTimeoutSeconds = 30.0;
constexpr size_t kMaxDatagram = 1200;
constexpr size_t kMinQueryBytes = 512;

class DirectoryRegistry {
public:
    static constexpr size_t kMaxPerAddress = 8;
    static constexpr size_t kMaxEntries = 4096;

    // "" when listed (or refreshed); otherwise why not.
    std::string heartbeat(DirectoryEntry e, const std::string& sourceAddress, double now);
    // A server stopping: gone at once (only from its own address).
    bool bye(const std::string& sourceAddress, uint16_t port);
    void expire(double now);
    // Servers of `game` ("" = every game), in a stable order.
    std::vector<DirectoryEntry> list(const std::string& game) const;
    size_t size() const { return m_entries.size(); }

private:
    struct Live { DirectoryEntry entry; double lastSeen = 0; };
    std::map<std::string, Live> m_entries; // "address:port"
};

// Queries allowed per address: a burst of `burst`, then `perSecond`.
class RateLimiter {
public:
    RateLimiter(double burst = 10, double perSecond = 1) : m_burst(burst), m_rate(perSecond) {}
    bool allow(const std::string& address, double now);

private:
    struct Bucket { double tokens = 0, last = 0; };
    std::map<std::string, Bucket> m_buckets;
    double m_burst, m_rate;
};

// ---- datagrams

struct DirectoryQuery {
    std::string game;
    uint32_t from = 0; // first entry wanted (pages: DirectoryList::next)
};
struct DirectoryList {
    std::vector<DirectoryEntry> servers;
    uint32_t next = 0; // 0 = that was all; else ask again with from = next
    uint32_t total = 0;
};
struct DirectoryHeartbeat {
    DirectoryEntry entry;
    bool bye = false;
};

std::vector<uint8_t> encode(const DirectoryHeartbeat& m);
std::vector<uint8_t> encode(const DirectoryQuery& m); // padded to kMinQueryBytes
// Fits as many of `all` from `from` as go in one datagram.
std::vector<uint8_t> encodeList(const std::vector<DirectoryEntry>& all, uint32_t from);

enum class DirectoryMessage { None, Heartbeat, Query, List };
struct DirectoryDecoded {
    DirectoryMessage type = DirectoryMessage::None;
    DirectoryHeartbeat heartbeat;
    DirectoryQuery query;
    DirectoryList list;
};
// None for anything that isn't a well-formed directory datagram
// (including an unpadded query).
DirectoryDecoded decodeDirectory(const uint8_t* data, size_t size);

} // namespace kke::server
