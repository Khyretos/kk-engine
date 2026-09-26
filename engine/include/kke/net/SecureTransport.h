#pragma once

// Encryption for every connection (docs/NETWORKING.md "Encryption",
// issue #44): an ITransport that wraps another (ENet, loopback) and
// encrypts everything the game sends, so nobody on the path (a café's
// Wi-Fi, a relay, the ISP) can read or change a player's password,
// chat, voice or moves.
//
//   Handshake  the client sends a fresh X25519 key; the server answers
//              with its own fresh key and its long-term identity key
//              (ServerIdentity), and proves it holds that key. Both
//              derive two keys (one per direction) with BLAKE2b from the
//              two Diffie-Hellman results. Nothing is sent before it.
//   Packets    XChaCha20-Poly1305 (Monocypher) with a per-channel
//              counter as the nonce: a changed, replayed or injected
//              packet fails its check and is dropped; too many of those
//              and the connection is dropped.
//   Identity   a client that knows which server it wants (a join code:
//              the relay tells it the server's key) refuses any other
//              key, so a man in the middle can't pose as the server. A
//              plain address has no key to check against: still
//              encrypted, like the first visit to a website over https
//              without a certificate.
//
// The layers above never see a peer before its handshake is done:
// Connected comes after it, and a peer that fails it never shows up.
// Forward secrecy: the session keys come from throwaway keys, so a
// server key stolen later doesn't decrypt old traffic.

#include "kke/net/Transport.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::net {

// A server's long-term key pair (X25519). kke_server keeps one in
// saveDir/server.key; a game hosting makes a new one each time.
struct ServerIdentity {
    std::array<uint8_t, 32> secret{};
    std::array<uint8_t, 32> publicKey{};

    // From the OS's secure random source; false when there is none.
    static bool generate(ServerIdentity& out, std::string* error = nullptr);
    static ServerIdentity fromSecret(const std::array<uint8_t, 32>& secret);
    // Loads the key in `path` (64 hex characters), or makes one there
    // (owner-only permissions) when the file doesn't exist.
    static bool loadOrCreate(const std::string& path, ServerIdentity& out, std::string* error = nullptr);
    // Short form to compare by eye: "3f9a-c2e1-77b0-19d4".
    std::string fingerprint() const;
};
std::string fingerprint(const std::array<uint8_t, 32>& publicKey);

class SecureTransport : public ITransport {
public:
    // `inner` must outlive this.
    explicit SecureTransport(ITransport& inner);
    ~SecureTransport() override;

    // Server: who we are (without one, host() makes a throwaway identity).
    void setIdentity(const ServerIdentity& id) { m_identity = id; m_hasIdentity = true; }
    const ServerIdentity& identity() const { return m_identity; }
    // Client: the next connect() only accepts a server with this key
    // (nullopt: any key).
    void expectServerKey(const std::optional<std::array<uint8_t, 32>>& key) { m_expectedKey = key; }
    // Client: the key of the server we're connected to (after Connected).
    std::optional<std::array<uint8_t, 32>> serverKey(PeerId peer) const;
    // Why the last connection failed at this layer ("" = it didn't).
    const std::string& failure() const { return m_failure; }

    size_t rejectedPackets() const { return m_rejected; }   // failed checks (tampered, replayed, junk)
    size_t failedHandshakes() const { return m_failedHandshakes; }
    double handshakeTimeout = 5.0;                         // seconds a peer may take to finish it
    size_t maxBadPackets = 20;                             // per peer, then it's dropped

    bool host(uint16_t port, size_t maxPeers, std::string* error = nullptr) override;
    PeerId connect(const std::string& address, uint16_t port, std::string* error = nullptr) override;
    void send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    void disconnect(PeerId peer) override;
    void poll(std::vector<NetEvent>& out) override;
    PeerStats stats(PeerId peer) const override { return m_inner.stats(peer); }
    std::string address(PeerId peer) const override { return m_inner.address(peer); }
    uint16_t port() const override { return m_inner.port(); }
    void close() override;
    const char* backendName() const override { return m_inner.backendName(); }
    using ITransport::send;

    // Bytes this layer adds to each packet.
    static constexpr size_t kOverhead = 1 + 8 + 16;

private:
    enum class State : uint8_t { ClientWaiting, ClientSentHello, ServerWaiting, Ready };
    struct Peer {
        State state = State::ServerWaiting;
        std::array<uint8_t, 32> ephemeralSecret{}, ephemeralPublic{};
        std::array<uint8_t, 32> sendKey{}, receiveKey{};
        std::array<uint8_t, 32> serverKey{};
        std::optional<std::array<uint8_t, 32>> expected;
        uint64_t sendCounter[2] = { 0, 0 };
        uint64_t receiveHighest[2] = { 0, 0 };
        uint64_t receiveWindow[2] = { 0, 0 };   // unreliable: which of the 64 below the highest arrived
        size_t bad = 0;
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    };
    void onReceived(PeerId id, Peer& p, const NetEvent& e, std::vector<NetEvent>& out);
    bool serverHandshake(PeerId id, Peer& p, const std::vector<uint8_t>& hello);
    bool clientHandshake(Peer& p, const std::vector<uint8_t>& hello);
    void sendClientHello(PeerId id, Peer& p);
    void fail(PeerId id, const std::string& why, std::vector<NetEvent>& out, bool tellAbove);
    static void wipe(Peer& p);

    ITransport& m_inner;
    ServerIdentity m_identity;
    bool m_hasIdentity = false;
    std::optional<std::array<uint8_t, 32>> m_expectedKey;
    std::map<PeerId, Peer> m_peers;
    std::string m_failure;
    size_t m_rejected = 0, m_failedHandshakes = 0;
};

} // namespace kke::net
