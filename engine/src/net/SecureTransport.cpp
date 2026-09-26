#include "kke/net/SecureTransport.h"

#include "kke/PackSeal.h"
#include "kke/server/ServerFiles.h"

#include <monocypher.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace kke::net {

namespace {

// Handshake messages: magic, version, type. Data packets: type, counter, mac, ciphertext.
constexpr uint8_t kMagic[4] = { 'K', 'K', 'E', 's' };
constexpr uint8_t kVersion = 1;
constexpr uint8_t kClientHello = 1, kServerHello = 2, kData = 3;
constexpr size_t kClientHelloSize = 4 + 1 + 1 + 32;
constexpr size_t kServerHelloSize = 4 + 1 + 1 + 32 + 32 + 16;
constexpr char kLabel[] = "kke-secure-1";

using Key = std::array<uint8_t, 32>;

bool allZero(const uint8_t* p, size_t n) {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= p[i];
    return acc == 0;
}

void nonceFor(uint8_t nonce[24], uint64_t counter, uint8_t channel) {
    std::memset(nonce, 0, 24);
    for (int i = 0; i < 8; ++i) nonce[i] = static_cast<uint8_t>(counter >> (8 * i));
    nonce[8] = channel;
}

// Both directions' keys from the two Diffie-Hellman results and every
// public key in the handshake (so a changed key changes everything).
void deriveKeys(const Key& clientEphemeral, const Key& serverEphemeral, const Key& serverStatic, const uint8_t dh1[32], const uint8_t dh2[32],
                Key& clientToServer, Key& serverToClient) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kLabel), sizeof(kLabel) - 1);
    crypto_blake2b_update(&ctx, clientEphemeral.data(), 32);
    crypto_blake2b_update(&ctx, serverEphemeral.data(), 32);
    crypto_blake2b_update(&ctx, serverStatic.data(), 32);
    crypto_blake2b_update(&ctx, dh1, 32);
    crypto_blake2b_update(&ctx, dh2, 32);
    uint8_t out[64];
    crypto_blake2b_final(&ctx, out);
    std::memcpy(clientToServer.data(), out, 32);
    std::memcpy(serverToClient.data(), out + 32, 32);
    crypto_wipe(out, sizeof out);
}

bool hasMagic(const std::vector<uint8_t>& d, uint8_t type) {
    return d.size() >= 6 && std::memcmp(d.data(), kMagic, 4) == 0 && d[4] == kVersion && d[5] == type;
}

} // namespace

// ------------------------------------------------------------------ identity

bool ServerIdentity::generate(ServerIdentity& out, std::string* error) {
    Key secret{};
    if (!seal::randomBytes(secret.data(), secret.size(), error)) return false;
    out = fromSecret(secret);
    crypto_wipe(secret.data(), secret.size());
    return true;
}

ServerIdentity ServerIdentity::fromSecret(const Key& secret) {
    ServerIdentity id;
    id.secret = secret;
    crypto_x25519_public_key(id.publicKey.data(), id.secret.data());
    return id;
}

bool ServerIdentity::loadOrCreate(const std::string& path, ServerIdentity& out, std::string* error) {
    std::string text;
    bool exists = false;
    if (server::readFile(path, text, &exists)) {
        text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r' || c == ' '; }), text.end());
        Key secret{};
        if (!seal::fromHex(text, secret)) {
            if (error) *error = path + ": not a key (64 hex characters); delete it to make a new one (players who pinned the old key will see a warning)";
            return false;
        }
        out = fromSecret(secret);
        crypto_wipe(secret.data(), secret.size());
        return true;
    }
    if (exists) {
        if (error) *error = path + ": can't read it";
        return false;
    }
    if (!generate(out, error)) return false;
    std::string e;
    if (!server::writeFileAtomic(path, seal::toHex(out.secret) + "\n", &e)) {
        if (error) *error = path + ": " + e;
        return false;
    }
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
    return true;
}

std::string fingerprint(const Key& publicKey) {
    uint8_t h[8];
    crypto_blake2b(h, sizeof h, publicKey.data(), publicKey.size());
    const std::string hex = seal::toHex(h, sizeof h);
    return hex.substr(0, 4) + "-" + hex.substr(4, 4) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4);
}

std::string ServerIdentity::fingerprint() const { return net::fingerprint(publicKey); }

// ------------------------------------------------------------------ transport

SecureTransport::SecureTransport(ITransport& inner) : m_inner(inner) {}

SecureTransport::~SecureTransport() {
    for (auto& [id, p] : m_peers) wipe(p);
    crypto_wipe(m_identity.secret.data(), m_identity.secret.size());
}

void SecureTransport::wipe(Peer& p) {
    crypto_wipe(p.ephemeralSecret.data(), 32);
    crypto_wipe(p.sendKey.data(), 32);
    crypto_wipe(p.receiveKey.data(), 32);
}

bool SecureTransport::host(uint16_t port, size_t maxPeers, std::string* error) {
    if (!m_hasIdentity) {
        if (!ServerIdentity::generate(m_identity, error)) return false;
        m_hasIdentity = true;
    }
    return m_inner.host(port, maxPeers, error);
}

PeerId SecureTransport::connect(const std::string& address, uint16_t port, std::string* error) {
    m_failure.clear();
    const PeerId id = m_inner.connect(address, port, error);
    if (id == kNoPeer) return id;
    Peer p;
    p.state = State::ClientWaiting;
    p.expected = m_expectedKey;
    m_peers[id] = p;
    return id;
}

std::optional<Key> SecureTransport::serverKey(PeerId peer) const {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || it->second.state != State::Ready) return std::nullopt;
    return it->second.serverKey;
}

void SecureTransport::sendClientHello(PeerId id, Peer& p) {
    if (!seal::randomBytes(p.ephemeralSecret.data(), 32, &m_failure)) return;
    crypto_x25519_public_key(p.ephemeralPublic.data(), p.ephemeralSecret.data());
    std::vector<uint8_t> hello(kMagic, kMagic + 4);
    hello.push_back(kVersion);
    hello.push_back(kClientHello);
    hello.insert(hello.end(), p.ephemeralPublic.begin(), p.ephemeralPublic.end());
    m_inner.send(id, Channel::Reliable, hello);
    p.state = State::ClientSentHello;
    p.since = std::chrono::steady_clock::now();
}

bool SecureTransport::serverHandshake(PeerId id, Peer& p, const std::vector<uint8_t>& hello) {
    if (hello.size() != kClientHelloSize || !hasMagic(hello, kClientHello)) return false;
    Key clientEphemeral;
    std::memcpy(clientEphemeral.data(), hello.data() + 6, 32);
    if (!seal::randomBytes(p.ephemeralSecret.data(), 32)) return false;
    crypto_x25519_public_key(p.ephemeralPublic.data(), p.ephemeralSecret.data());
    uint8_t dh1[32], dh2[32];
    crypto_x25519(dh1, p.ephemeralSecret.data(), clientEphemeral.data());
    crypto_x25519(dh2, m_identity.secret.data(), clientEphemeral.data());
    // A low-order key makes an all-zero secret anyone can compute: refuse it.
    const bool weak = allZero(dh1, 32) || allZero(dh2, 32);
    Key c2s, s2c;
    if (!weak) deriveKeys(clientEphemeral, p.ephemeralPublic, m_identity.publicKey, dh1, dh2, c2s, s2c);
    crypto_wipe(dh1, 32);
    crypto_wipe(dh2, 32);
    crypto_wipe(p.ephemeralSecret.data(), 32); // done with it: forward secrecy
    if (weak) return false;
    p.receiveKey = c2s;
    p.sendKey = s2c;
    p.serverKey = m_identity.publicKey;
    // Proof that we hold the identity key: only its owner could derive s2c.
    std::vector<uint8_t> reply(kMagic, kMagic + 4);
    reply.push_back(kVersion);
    reply.push_back(kServerHello);
    reply.insert(reply.end(), p.ephemeralPublic.begin(), p.ephemeralPublic.end());
    reply.insert(reply.end(), m_identity.publicKey.begin(), m_identity.publicKey.end());
    uint8_t nonce[24], mac[16];
    nonceFor(nonce, 0, 0xFF);
    crypto_aead_lock(nullptr, mac, p.sendKey.data(), nonce, reply.data(), reply.size(), nullptr, 0);
    reply.insert(reply.end(), mac, mac + 16);
    m_inner.send(id, Channel::Reliable, reply);
    p.state = State::Ready;
    return true;
}

bool SecureTransport::clientHandshake(Peer& p, const std::vector<uint8_t>& hello) {
    if (hello.size() != kServerHelloSize || !hasMagic(hello, kServerHello)) {
        m_failure = "the server doesn't speak this game's encryption (an older or newer version?)";
        return false;
    }
    Key serverEphemeral, serverStatic;
    std::memcpy(serverEphemeral.data(), hello.data() + 6, 32);
    std::memcpy(serverStatic.data(), hello.data() + 38, 32);
    if (p.expected && crypto_verify32(p.expected->data(), serverStatic.data()) != 0) {
        m_failure = "the server's key (" + fingerprint(serverStatic) + ") isn't the one expected (" + fingerprint(*p.expected) +
                    "): someone may be in between; not connecting";
        return false;
    }
    uint8_t dh1[32], dh2[32];
    crypto_x25519(dh1, p.ephemeralSecret.data(), serverEphemeral.data());
    crypto_x25519(dh2, p.ephemeralSecret.data(), serverStatic.data());
    const bool weak = allZero(dh1, 32) || allZero(dh2, 32);
    Key c2s, s2c;
    if (!weak) deriveKeys(p.ephemeralPublic, serverEphemeral, serverStatic, dh1, dh2, c2s, s2c);
    crypto_wipe(dh1, 32);
    crypto_wipe(dh2, 32);
    crypto_wipe(p.ephemeralSecret.data(), 32);
    if (weak) {
        m_failure = "the server sent a weak key";
        return false;
    }
    uint8_t nonce[24];
    nonceFor(nonce, 0, 0xFF);
    const size_t signedSize = kServerHelloSize - 16;
    if (crypto_aead_unlock(nullptr, hello.data() + signedSize, s2c.data(), nonce, hello.data(), signedSize, nullptr, 0) != 0) {
        m_failure = "the server couldn't prove it holds its key; not connecting";
        return false;
    }
    p.sendKey = c2s;
    p.receiveKey = s2c;
    p.serverKey = serverStatic;
    p.state = State::Ready;
    return true;
}

void SecureTransport::fail(PeerId id, const std::string& why, std::vector<NetEvent>& out, bool tellAbove) {
    auto it = m_peers.find(id);
    if (it == m_peers.end()) return;
    const bool client = it->second.state == State::ClientWaiting || it->second.state == State::ClientSentHello;
    if (client) m_failure = why;
    wipe(it->second);
    m_peers.erase(it);
    m_inner.disconnect(id);
    if (tellAbove) out.push_back({ NetEvent::Type::Disconnected, id, Channel::Reliable, {} });
}

void SecureTransport::onReceived(PeerId id, Peer& p, const NetEvent& e, std::vector<NetEvent>& out) {
    switch (p.state) {
    case State::ServerWaiting:
        if (e.channel != Channel::Reliable || !serverHandshake(id, p, e.data)) {
            ++m_failedHandshakes;
            fail(id, "handshake", out, false); // never shown above: it never connected
            return;
        }
        out.push_back({ NetEvent::Type::Connected, id, Channel::Reliable, {} });
        return;
    case State::ClientSentHello:
        if (!clientHandshake(p, e.data)) {
            ++m_failedHandshakes;
            fail(id, m_failure, out, true);
            return;
        }
        out.push_back({ NetEvent::Type::Connected, id, Channel::Reliable, {} });
        return;
    case State::ClientWaiting:
        return; // nothing comes before our hello
    case State::Ready:
        break;
    }
    const std::vector<uint8_t>& d = e.data;
    const int ch = e.channel == Channel::Reliable ? 0 : 1;
    auto reject = [&] {
        ++m_rejected;
        if (++p.bad > maxBadPackets) fail(id, "too many packets that failed their check", out, true);
    };
    if (d.size() < kOverhead || d[0] != kData) return reject();
    uint64_t counter = 0;
    for (int i = 0; i < 8; ++i) counter |= static_cast<uint64_t>(d[1 + i]) << (8 * i);
    if (counter == 0) return reject();
    // Replays: the reliable channel only ever goes up; the unreliable one
    // may arrive out of order, within a 64-packet window.
    uint64_t& highest = p.receiveHighest[ch];
    uint64_t& window = p.receiveWindow[ch];
    if (ch == 0 && counter <= highest) return reject();
    if (ch == 1 && counter <= highest) {
        const uint64_t back = highest - counter;
        if (back >= 64 || (window >> back) & 1u) return reject();
    }
    uint8_t nonce[24];
    nonceFor(nonce, counter, static_cast<uint8_t>(ch));
    std::vector<uint8_t> plain(d.size() - kOverhead);
    if (crypto_aead_unlock(plain.data(), d.data() + 9, p.receiveKey.data(), nonce, d.data(), 9, d.data() + kOverhead, plain.size()) != 0)
        return reject();
    if (counter > highest) {
        const uint64_t shift = counter - highest;
        window = shift >= 64 ? 1u : (window << shift) | 1u;
        highest = counter;
    } else {
        window |= uint64_t(1) << (highest - counter);
    }
    out.push_back({ NetEvent::Type::Received, id, e.channel, std::move(plain) });
}

void SecureTransport::poll(std::vector<NetEvent>& out) {
    std::vector<NetEvent> in;
    m_inner.poll(in);
    for (NetEvent& e : in) {
        auto it = m_peers.find(e.peer);
        switch (e.type) {
        case NetEvent::Type::Connected:
            if (it == m_peers.end()) {
                m_peers[e.peer] = Peer{}; // someone connected to us: wait for their hello
            } else if (it->second.state == State::ClientWaiting) {
                sendClientHello(e.peer, it->second);
                if (it->second.state != State::ClientSentHello) fail(e.peer, "no secure random numbers on this system: " + m_failure, out, true);
            }
            break;
        case NetEvent::Type::Disconnected:
            if (it == m_peers.end()) break;
            // Only peers the layers above know about are reported (a client always knows its own).
            if (it->second.state != State::ServerWaiting) {
                if (it->second.state != State::Ready && m_failure.empty()) m_failure = "the connection closed during the encryption handshake";
                out.push_back({ NetEvent::Type::Disconnected, e.peer, Channel::Reliable, {} });
            }
            wipe(it->second);
            m_peers.erase(it);
            break;
        case NetEvent::Type::Received:
            if (it != m_peers.end()) onReceived(e.peer, it->second, e, out);
            break;
        }
    }
    // Handshakes that never finish (a scanner, a stuck client) hold no slot for long.
    const auto now = std::chrono::steady_clock::now();
    std::vector<PeerId> late;
    for (const auto& [id, p] : m_peers)
        if ((p.state == State::ServerWaiting || p.state == State::ClientSentHello) && std::chrono::duration<double>(now - p.since).count() > handshakeTimeout)
            late.push_back(id);
    for (PeerId id : late) {
        ++m_failedHandshakes;
        const bool client = m_peers[id].state != State::ServerWaiting;
        fail(id, "the server didn't answer the encryption handshake", out, client);
    }
}

void SecureTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || it->second.state != State::Ready) return;
    Peer& p = it->second;
    const int ch = channel == Channel::Reliable ? 0 : 1;
    const uint64_t counter = ++p.sendCounter[ch];
    std::vector<uint8_t> packet(kOverhead + size);
    packet[0] = kData;
    for (int i = 0; i < 8; ++i) packet[1 + i] = static_cast<uint8_t>(counter >> (8 * i));
    uint8_t nonce[24];
    nonceFor(nonce, counter, static_cast<uint8_t>(ch));
    crypto_aead_lock(packet.data() + kOverhead, packet.data() + 9, p.sendKey.data(), nonce, packet.data(), 9, data, size);
    m_inner.send(peer, channel, packet);
}

void SecureTransport::disconnect(PeerId peer) {
    if (auto it = m_peers.find(peer); it != m_peers.end()) {
        wipe(it->second);
        m_peers.erase(it);
    }
    m_inner.disconnect(peer);
}

void SecureTransport::close() {
    for (auto& [id, p] : m_peers) wipe(p);
    m_peers.clear();
    m_inner.close();
}

} // namespace kke::net
