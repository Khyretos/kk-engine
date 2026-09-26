#include "kke/server/RelayService.h"

#include "kke/PackSeal.h"
#include "kke/server/DirectoryNet.h"

#include <monocypher.h>

#include <algorithm>

namespace kke::server {

using namespace net;

namespace {
constexpr double kUnansweredSession = 15.0; // a Lookup nobody followed up
constexpr size_t kMaxRelayedDatagram = 1500;
} // namespace

size_t RelayCore::sessions() const {
    return static_cast<size_t>(std::count_if(m_slots.begin(), m_slots.end(), [](const Session& s) { return s.used; }));
}

void RelayCore::refuse(const std::string& host, uint16_t port, const std::string& why) {
    if (send) send(0, host, port, relay::encode(relay::Refused{ why }));
}

std::string RelayCore::freshCode() const {
    for (int tries = 0; tries < 16; ++tries) {
        const std::string c = newJoinCode();
        if (!c.empty() && !m_servers.count(c)) return c;
    }
    return {};
}

void RelayCore::endSession(Session& s) {
    m_byClient.erase({ s.clientHost, s.clientPort });
    crypto_wipe(s.token.data(), s.token.size());
    s = Session{};
}

bool RelayCore::spend(Session& s, bool toServer, size_t bytes, double now) {
    // A token bucket per direction: a second's worth at most.
    const double rate = m_limits.bytesPerSecond;
    const double dt = std::max(0.0, now - s.budgetAt);
    s.budgetAt = now;
    s.toServerBudget = std::min(rate, s.toServerBudget + dt * rate);
    s.toClientBudget = std::min(rate, s.toClientBudget + dt * rate);
    double& b = toServer ? s.toServerBudget : s.toClientBudget;
    if (b < static_cast<double>(bytes)) return false;
    b -= static_cast<double>(bytes);
    return true;
}

void RelayCore::receive(size_t socket, const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now) {
    if (size == 0 || size > kMaxRelayedDatagram) {
        ++m_refused;
        return;
    }
    if (socket == 0) onMain(host, port, data, size, now);
    else if (socket <= m_slots.size()) onSlot(socket - 1, host, port, data, size, now);
}

void RelayCore::onMain(const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now) {
    if (!relay::isRelayDatagram(data, size)) {
        // A relayed player's game packet: on to their server, from their slot.
        auto it = m_byClient.find({ host, port });
        if (it == m_byClient.end()) {
            ++m_refused; // a stranger: never forwarded anywhere
            return;
        }
        Session& s = m_slots[it->second];
        if (s.serverHost.empty() || !spend(s, true, size, now)) return; // the server hasn't opened its side yet, or over the cap
        s.lastActivity = now;
        s.active = true;
        m_bytes += size;
        if (send) send(it->second + 1, s.serverHost, s.serverPort, std::vector<uint8_t>(data, data + size));
        return;
    }
    const std::vector<uint8_t> d(data, data + size);
    const auto type = relay::typeOf(d);
    if (!type) {
        ++m_refused;
        return;
    }
    switch (*type) {
    case relay::Type::Register: {
        if (!m_registers.allow(host, now)) {
            ++m_refused;
            return;
        }
        const auto m = relay::decodeRegister(d);
        if (!m) {
            ++m_refused;
            return;
        }
        // Its code again (a heartbeat, a restart, a new home address), if the secret matches.
        std::string code;
        if (!m->code.empty()) {
            auto it = m_servers.find(m->code);
            if (it == m_servers.end()) code = m->code; // free: it may have it back
            else if (crypto_verify16(it->second.secret.data(), m->secret.data()) == 0) code = m->code;
        }
        if (code.empty()) {
            // A server re-registering without its code (it lost it): find it by secret and address.
            for (const auto& [c, s] : m_servers)
                if (s.host == host && s.port == port && crypto_verify16(s.secret.data(), m->secret.data()) == 0) code = c;
        }
        const bool fresh = code.empty() || !m_servers.count(code);
        if (fresh) {
            const size_t here = static_cast<size_t>(std::count_if(m_servers.begin(), m_servers.end(), [&](const auto& kv) { return kv.second.host == host; }));
            if (here >= m_limits.serversPerAddress || m_servers.size() >= m_limits.maxServers) {
                ++m_refused;
                refuse(host, port, "this relay is full (too many servers" + std::string(here >= m_limits.serversPerAddress ? " from your address)" : ")"));
                return;
            }
            if (code.empty()) code = freshCode();
            if (code.empty()) return;
        }
        Server& s = m_servers[code];
        s.code = code;
        s.game = m->game;
        s.host = host;
        s.port = port;
        s.secret = m->secret;
        s.key = m->serverKey;
        s.lastSeen = now;
        if (fresh && log) log("relay: server registered as " + formatJoinCode(code) + " (" + m->game + ", " + host + ")");
        if (send) send(0, host, port, relay::encode(relay::Registered{ code, host, port }));
        return;
    }
    case relay::Type::Bye: {
        const auto m = relay::decodeBye(d);
        if (!m) return;
        for (auto it = m_servers.begin(); it != m_servers.end(); ++it) {
            if (it->second.host != host || it->second.port != port || crypto_verify16(it->second.secret.data(), m->secret.data()) != 0) continue;
            for (Session& s : m_slots)
                if (s.used && s.code == it->first) endSession(s);
            if (log) log("relay: " + formatJoinCode(it->first) + " closed");
            m_servers.erase(it);
            return;
        }
        return;
    }
    case relay::Type::Lookup: {
        if (!m_lookups.allow(host, now)) {
            ++m_refused; // silently: an answer is what a guesser wants
            return;
        }
        const auto m = relay::decodeLookup(d);
        if (!m) {
            ++m_refused;
            return;
        }
        auto it = m_servers.find(m->code);
        if (it == m_servers.end() || it->second.game != m->game) {
            refuse(host, port, "no game with that code here (it may have closed; codes change when a server moves relay)");
            return;
        }
        const Server& srv = it->second;
        // One slot per player address: a repeated Lookup (its answer got lost) reuses it.
        size_t slot = m_slots.size();
        if (auto c = m_byClient.find({ host, port }); c != m_byClient.end()) {
            slot = c->second;
            endSession(m_slots[slot]);
        }
        const size_t fromHere = static_cast<size_t>(std::count_if(m_slots.begin(), m_slots.end(), [&](const Session& s) { return s.used && s.clientHost == host; }));
        const size_t forServer = static_cast<size_t>(std::count_if(m_slots.begin(), m_slots.end(), [&](const Session& s) { return s.used && s.code == srv.code; }));
        if (fromHere >= m_limits.sessionsPerClient || forServer >= m_limits.sessionsPerServer) {
            refuse(host, port, "too many joins at once; try again in a minute");
            return;
        }
        if (slot == m_slots.size())
            for (size_t i = 0; i < m_slots.size(); ++i)
                if (!m_slots[i].used) {
                    slot = i;
                    break;
                }
        if (slot == m_slots.size()) {
            refuse(host, port, "this relay is full right now; try again soon");
            return;
        }
        Session& s = m_slots[slot];
        s.used = true;
        if (!seal::randomBytes(s.token.data(), s.token.size())) {
            s = Session{};
            return;
        }
        s.code = srv.code;
        s.clientHost = host;
        s.clientPort = port;
        s.started = s.lastActivity = s.budgetAt = now;
        s.toServerBudget = s.toClientBudget = m_limits.bytesPerSecond;
        m_byClient[{ host, port }] = slot;
        const uint16_t relayPort = slotPort ? slotPort(slot + 1) : 0;
        if (send) {
            send(0, host, port, relay::encode(relay::Found{ s.token, srv.host, srv.port, srv.key }));
            send(0, srv.host, srv.port, relay::encode(relay::Introduce{ s.token, host, port, relayPort }));
        }
        return;
    }
    default:
        ++m_refused; // Found, Introduce, ... are the relay's own: nobody sends them to it
        return;
    }
}

void RelayCore::onSlot(size_t slot, const std::string& host, uint16_t port, const uint8_t* data, size_t size, double now) {
    Session& s = m_slots[slot];
    if (!s.used) {
        ++m_refused;
        return;
    }
    if (relay::isRelayDatagram(data, size)) {
        // The server opening its side: it knows this join's token.
        const auto m = relay::decodeOpen(std::vector<uint8_t>(data, data + size));
        const auto srv = m_servers.find(s.code);
        if (!m || srv == m_servers.end() || srv->second.host != host || crypto_verify16(m->token.data(), s.token.data()) != 0) {
            ++m_refused;
            return;
        }
        s.serverHost = host;
        s.serverPort = port; // its router may have picked another port toward this one
        return;
    }
    if (host != s.serverHost || port != s.serverPort) {
        ++m_refused;
        return;
    }
    if (!spend(s, false, size, now)) return;
    s.lastActivity = now;
    m_bytes += size;
    if (send) send(0, s.clientHost, s.clientPort, std::vector<uint8_t>(data, data + size));
}

void RelayCore::update(double now) {
    for (auto it = m_servers.begin(); it != m_servers.end();) {
        if (now - it->second.lastSeen <= m_limits.serverTimeout) {
            ++it;
            continue;
        }
        for (Session& s : m_slots)
            if (s.used && s.code == it->first) endSession(s);
        if (log) log("relay: " + formatJoinCode(it->first) + " went quiet; forgotten");
        it = m_servers.erase(it);
    }
    for (Session& s : m_slots) {
        if (!s.used) continue;
        const double idle = now - s.lastActivity;
        if (idle > m_limits.sessionTimeout || (!s.active && idle > kUnansweredSession)) endSession(s);
    }
}

// ------------------------------------------------------------------ service

RelayService::RelayService() = default;
RelayService::~RelayService() { stop(); }

bool RelayService::start(uint16_t port, size_t slots, std::string* error) {
    stop();
    if (static_cast<size_t>(port) + slots > 65535) {
        if (error) *error = "relay: port " + std::to_string(port) + " + " + std::to_string(slots) + " slots goes past 65535";
        return false;
    }
    for (size_t i = 0; i <= slots; ++i) {
        auto sock = std::make_unique<UdpSocket>();
        sock->maxDatagram = kMaxRelayedDatagram;
        if (!sock->open(static_cast<uint16_t>(port + i), error)) {
            if (error && i > 0) *error = "relay slot " + std::to_string(i) + ": " + *error + " (the relay needs UDP " + std::to_string(port) + "-" +
                                         std::to_string(port + slots) + ")";
            stop();
            return false;
        }
        m_sockets.push_back(std::move(sock));
    }
    m_port = port;
    RelayCore::Limits limits;
    limits.slots = slots;
    m_core = std::make_unique<RelayCore>(limits);
    m_core->send = [this](size_t socket, const std::string& host, uint16_t p, const std::vector<uint8_t>& data) {
        if (socket < m_sockets.size()) m_sockets[socket]->send(host, p, data);
    };
    m_core->slotPort = [this](size_t slot) { return static_cast<uint16_t>(m_port + slot); };
    m_core->log = [this](const std::string& line) { if (log) log(line); };
    return true;
}

void RelayService::stop() {
    m_sockets.clear();
    m_core.reset();
}

bool RelayService::running() const { return !m_sockets.empty(); }

void RelayService::update(double now) {
    if (!m_core) return;
    for (size_t i = 0; i < m_sockets.size(); ++i)
        m_sockets[i]->receive([&](const std::string& host, uint16_t port, const uint8_t* data, size_t size) { m_core->receive(i, host, port, data, size, now); });
    m_core->update(now);
}

} // namespace kke::server
