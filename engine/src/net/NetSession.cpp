#include "kke/net/NetSession.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kke::net {

// ---------------------------------------------------------------- clock + interpolation

void ClockOffset::observe(double senderTime, double localTime) {
    const double o = localTime - senderTime;
    if (!m_valid) {
        m_offset = o;
        m_valid = true;
    } else if (o < m_offset) {
        m_offset = o; // a faster packet: the path is at least this quick
    } else {
        // Slower packets pull the estimate up slowly (latency rose, or the
        // sender's clock runs a little slow); jitter alone barely moves it.
        m_offset += (o - m_offset) * 0.01;
    }
}

namespace {

float lerpYaw(float a, float b, float t) {
    float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f; // shortest way round
    float y = a + d * t;
    y = std::fmod(y, 360.0f);
    return y < 0.0f ? y + 360.0f : y;
}

bool isTeleport(const NetPlayerState& a, const NetPlayerState& b) {
    return (b.flags & kPlayerTeleported) != 0 || glm::length(b.position - a.position) > 8.0f;
}

} // namespace

NetPlayerState interpolate(const Timeline<NetPlayerState>& line, double time, double maxExtrapolation) {
    const NetPlayerState *a = nullptr, *b = nullptr;
    double t = 0.0, dt = 0.0;
    if (!line.bracket(time, a, b, t, dt)) return {};
    if (dt < 0.0) {
        // Past the newest: carry on at its velocity for a moment, then hold.
        NetPlayerState s = *a;
        s.position += s.velocity * static_cast<float>(std::min(t, maxExtrapolation));
        return s;
    }
    if (a == b || isTeleport(*a, *b)) return t < 1.0 && a != b ? *a : *b;
    const float k = static_cast<float>(std::clamp(t, 0.0, 1.0));
    NetPlayerState s = k < 0.5f ? *a : *b; // discrete fields: the nearer sample
    s.position = glm::mix(a->position, b->position, k);
    s.velocity = glm::mix(a->velocity, b->velocity, k);
    s.yaw = lerpYaw(a->yaw, b->yaw, k);
    s.speed = a->speed + (b->speed - a->speed) * k;
    s.progress = a->state == b->state ? a->progress + (b->progress - a->progress) * k : s.progress;
    return s;
}

NetBodyState interpolate(const Timeline<NetBodyState>& line, double time, double maxExtrapolation) {
    const NetBodyState *a = nullptr, *b = nullptr;
    double t = 0.0, dt = 0.0;
    if (!line.bracket(time, a, b, t, dt)) return {};
    if (dt < 0.0) {
        NetBodyState s = *a;
        if (!s.sleeping) s.position += s.velocity * static_cast<float>(std::min(t, maxExtrapolation));
        return s;
    }
    if (a == b) return *a;
    const float k = static_cast<float>(std::clamp(t, 0.0, 1.0));
    NetBodyState s = *b;
    s.position = glm::mix(a->position, b->position, k);
    s.rotation = glm::slerp(a->rotation, b->rotation, k);
    s.velocity = glm::mix(a->velocity, b->velocity, k);
    s.sleeping = a->sleeping && b->sleeping;
    return s;
}

namespace {

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

template <typename Msg>
void sendMsg(ITransport& t, PeerId peer, Channel ch, MessageType type, Msg msg) {
    t.send(peer, ch, encode(type, msg));
}

} // namespace

// ---------------------------------------------------------------- server

NetServer::NetServer(ITransport& transport, const NetConfig& config) : m_transport(transport), m_config(config) {
    m_config.maxPlayers = std::clamp<size_t>(m_config.maxPlayers, 1, kMaxPlayers);
}

NetServer::~NetServer() { stop(); }

bool NetServer::start(uint16_t port, const std::string& hostName, const std::string& hostCharacter, std::string* error) {
    stop();
    // Room for a few extra connections, so a full server can still say
    // "full" to the next one instead of leaving it to time out.
    if (!m_transport.host(port, m_config.maxPlayers + 3, error)) return false;
    m_hostName = hostName.substr(0, kMaxNameLength);
    m_hostCharacter = hostCharacter.substr(0, kMaxCharacterLength);
    m_running = true;
    m_started = false; // the clock starts at the first update()
    return true;
}

void NetServer::stop() {
    if (!m_running) return;
    m_transport.close();
    m_clients.clear();
    m_running = false;
}

NetServer::Client* NetServer::byPeer(PeerId peer) {
    if (peer == kNoPeer) return nullptr;
    for (Client& c : m_clients) if (c.peer == peer) return &c;
    return nullptr;
}

NetServer::Client* NetServer::byId(uint8_t id) {
    for (Client& c : m_clients) if (c.id == id && id != 0) return &c;
    return nullptr;
}

size_t NetServer::clientCount() const {
    return static_cast<size_t>(std::count_if(m_clients.begin(), m_clients.end(), [](const Client& c) { return c.id != 0; }));
}

PeerStats NetServer::stats(uint8_t playerId) const {
    for (const Client& c : m_clients) if (c.id == playerId && playerId) return m_transport.stats(c.peer);
    return {};
}

void NetServer::bad(Client& c, const char* what) {
    ++m_badPackets;
    if (++c.badPackets >= m_config.maxBadPackets) drop(c, std::string("too many bad packets (last: ") + what + ")");
}

// Tells the client why, closes its connection, and marks it for removal
// (erased at the end of update(), so pointers stay valid meanwhile).
void NetServer::drop(Client& c, const std::string& reason) {
    if (c.peer == kNoPeer) return;
    sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::Reject, RejectMsg{ reason });
    m_transport.disconnect(c.peer);
    c.peer = kNoPeer;
    if (c.id) {
        PlayerInfoMsg left{ c.id, false, c.name, c.character };
        broadcastReliable(encode(MessageType::PlayerInfo, left), c.id);
        if (onPlayer) onPlayer(c.id, false);
    }
}

void NetServer::kick(uint8_t playerId, const std::string& reason) {
    if (Client* c = byId(playerId)) drop(*c, reason);
}

void NetServer::broadcastReliable(const std::vector<uint8_t>& data, int exceptPlayer) {
    for (const Client& c : m_clients)
        if (c.id != 0 && c.peer != kNoPeer && static_cast<int>(c.id) != exceptPlayer) m_transport.send(c.peer, Channel::Reliable, data);
}

void NetServer::sendEvent(uint16_t kind, const std::vector<uint8_t>& payload, int exceptPlayer) {
    GameEventMsg e{ 0, kind, payload };
    if (e.payload.size() > kMaxEventBytes) e.payload.resize(kMaxEventBytes);
    broadcastReliable(encode(MessageType::GameEvent, e), exceptPlayer);
}

void NetServer::relayEvent(const GameEventMsg& e) {
    GameEventMsg copy = e;
    broadcastReliable(encode(MessageType::GameEvent, copy), e.fromPlayer);
}

void NetServer::spawn(const SpawnMsg& m, bool persistent) {
    SpawnMsg copy = m;
    if (copy.desc.size() > kMaxSpawnBytes) copy.desc.resize(kMaxSpawnBytes);
    broadcastReliable(encode(MessageType::Spawn, copy), -1);
    if (persistent) m_spawns[copy.id] = std::move(copy);
    else m_spawns.erase(copy.id);
}

void NetServer::despawn(uint16_t id) {
    m_spawns.erase(id);
    m_breaks.erase(id);
    DespawnMsg m{ id };
    broadcastReliable(encode(MessageType::Despawn, m), -1);
}

// One Break message per kMaxBordersPerBreak borders; to one peer, or
// (kNoPeer) to every client.
void NetServer::sendBreaks(PeerId peer, uint16_t id, uint32_t seed, const std::vector<std::pair<uint16_t, uint16_t>>& borders, int exceptPlayer) {
    for (size_t first = 0; first < borders.size(); first += kMaxBordersPerBreak) {
        BreakMsg m{ id, seed, {} };
        const size_t last = std::min(borders.size(), first + kMaxBordersPerBreak);
        m.borders.assign(borders.begin() + static_cast<std::ptrdiff_t>(first), borders.begin() + static_cast<std::ptrdiff_t>(last));
        const std::vector<uint8_t> data = encode(MessageType::Break, m);
        if (peer != kNoPeer) m_transport.send(peer, Channel::Reliable, data);
        else broadcastReliable(data, exceptPlayer);
    }
}

void NetServer::breakBorders(uint16_t id, uint32_t seed, const std::vector<std::pair<uint16_t, uint16_t>>& borders) {
    BreakSet& set = m_breaks[id];
    if (set.seed != seed) set = BreakSet{ seed, {} };
    std::vector<std::pair<uint16_t, uint16_t>> fresh;
    for (auto [a, b] : borders) {
        if (a == b) continue; // not a border
        if (a > b) std::swap(a, b);
        if (set.borders.insert({ a, b }).second) fresh.push_back({ a, b });
    }
    if (!fresh.empty()) sendBreaks(kNoPeer, id, seed, fresh, -1);
}

void NetServer::forgetBreaks(uint16_t id) { m_breaks.erase(id); }

void NetServer::handleHello(Client& c, const HelloMsg& m) {
    std::string reason;
    if (c.id != 0) return; // a second Hello: ignore
    if (m.version != kProtocolVersion)
        reason = "version mismatch: server speaks protocol " + std::to_string(kProtocolVersion) + ", you " + std::to_string(m.version);
    else if (m.gameId != m_config.gameId)
        reason = "this server runs '" + m_config.gameId + "', not '" + m.gameId + "'";
    else if (clientCount() + 1 >= m_config.maxPlayers)
        reason = "server is full (" + std::to_string(m_config.maxPlayers) + " players)";
    if (!reason.empty()) return drop(c, reason);
    uint8_t id = 1;
    while (byId(id)) ++id;
    c.id = id;
    c.name = m.name.empty() ? "Player " + std::to_string(id) : m.name;
    c.character = m.character;
    WelcomeMsg w{ id, static_cast<uint8_t>(m_config.maxPlayers), m_config.snapshotHz, timeMs() };
    sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::Welcome, w);
    // Who's here already (the host, then the other clients), then tell
    // them about the new one.
    sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::PlayerInfo, PlayerInfoMsg{ 0, true, m_hostName, m_hostCharacter });
    for (const Client& o : m_clients)
        if (o.id && o.id != id) sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::PlayerInfo, PlayerInfoMsg{ o.id, true, o.name, o.character });
    PlayerInfoMsg joined{ id, true, c.name, c.character };
    broadcastReliable(encode(MessageType::PlayerInfo, joined), id);
    // What was made and broken before they came (reliable and ordered:
    // a spawn arrives before the breaks of the breakable it makes).
    for (auto& [sid, spawnMsg] : m_spawns) sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::Spawn, spawnMsg);
    for (const auto& [bid, set] : m_breaks)
        sendBreaks(c.peer, bid, set.seed, std::vector<std::pair<uint16_t, uint16_t>>(set.borders.begin(), set.borders.end()), -1);
    // A newcomer gets every body soon: high starting priority.
    for (const NetBodyState& b : m_bodies) c.priority[b.id] = 10.0f;
    if (onPlayer) onPlayer(id, true);
}

void NetServer::handleState(Client& c, const PlayerStateMsg& m) {
    const NetPlayerState& s = m.state;
    if (!finite(s.position) || !finite(s.velocity)) return bad(c, "non-finite state");
    if (c.hasState) {
        const int32_t delta = static_cast<int32_t>(m.timeMs - c.acceptedTimeMs);
        if (delta <= 0) return; // old or duplicate (reordered): the newer one is already in
        // The client's clock may not run faster than ours (+ slack): it
        // can't buy itself more distance by claiming more time passed.
        const double dt = std::min(delta / 1000.0, (m_now - c.acceptedAt) + 0.25);
        const glm::vec3 d = s.position - c.accepted.position;
        const float horizontal = glm::length(glm::vec2(d.x, d.z));
        const bool teleport = (s.flags & kPlayerTeleported) != 0 && m_now - c.lastTeleport >= limits.teleportCooldown;
        const bool ok = teleport || (horizontal <= limits.horizontalSpeed * dt + limits.slack &&
                                     d.y <= limits.riseSpeed * dt + limits.slack && -d.y <= limits.fallSpeed * dt + limits.slack);
        if (!ok) return correct(c);
        if (!teleport && checkMove && !checkMove(c.id, c.accepted, s, dt)) {
            ++m_refusedMoves;
            return correct(c);
        }
        if (teleport) c.lastTeleport = m_now;
    }
    c.clock.observe(m.timeMs / 1000.0, m_now);
    c.states.push(m.timeMs / 1000.0, s);
    c.accepted = s;
    c.acceptedTimeMs = m.timeMs;
    c.acceptedAt = m_now;
    c.hasState = true;
}

// Back to the last move that passed (at most twice a second: the states
// already on their way from before it arrives are refused too).
void NetServer::correct(Client& c) {
    if (m_now - c.lastCorrection <= 0.5) return;
    sendMsg(m_transport, c.peer, Channel::Reliable, MessageType::Correction, CorrectionMsg{ c.accepted.position });
    c.lastCorrection = m_now;
    ++m_corrections;
}

void NetServer::receive(Client& c, const NetEvent& e) {
    const std::optional<MessageType> type = peekType(e.data.data(), e.data.size());
    if (!type) return bad(c, "unknown message");
    if (c.id == 0 && *type != MessageType::Hello) return bad(c, "message before hello");
    switch (*type) {
    case MessageType::Hello:
        if (auto m = decode<HelloMsg>(*type, e.data.data(), e.data.size()); m && e.channel == Channel::Reliable) handleHello(c, *m);
        else bad(c, "hello");
        break;
    case MessageType::PlayerState:
        if (auto m = decode<PlayerStateMsg>(*type, e.data.data(), e.data.size())) handleState(c, *m);
        else bad(c, "player state");
        break;
    case MessageType::GameEvent:
        if (auto m = decode<GameEventMsg>(*type, e.data.data(), e.data.size())) {
            if (m_now - c.eventWindow >= 1.0) { c.eventWindow = m_now; c.eventsInWindow = 0; }
            if (++c.eventsInWindow > m_config.maxEventsPerSecond) return; // flooding: dropped
            if (m->kind >= kEventBodiesReset) return bad(c, "reserved event");
            m->fromPlayer = c.id; // whatever it claimed
            if (onEvent) onEvent(*m);
        } else {
            bad(c, "event");
        }
        break;
    default:
        bad(c, "server-only message from a client");
        break;
    }
}

void NetServer::update(double now) {
    m_now = now;
    if (!m_running) return;
    if (!m_started) {
        m_start = now;
        m_nextSnapshot = now;
        m_started = true;
    }
    std::vector<NetEvent> events;
    m_transport.poll(events);
    for (NetEvent& e : events) {
        switch (e.type) {
        case NetEvent::Type::Connected: {
            Client c;
            c.peer = e.peer;
            c.connectedAt = now;
            m_clients.push_back(std::move(c));
            break;
        }
        case NetEvent::Type::Disconnected:
            if (Client* c = byPeer(e.peer)) {
                c->peer = kNoPeer; // gone: nothing more is sent to it
                const uint8_t id = c->id;
                PlayerInfoMsg left{ id, false, c->name, c->character };
                c->id = 0; // erased with the others below
                if (id) {
                    broadcastReliable(encode(MessageType::PlayerInfo, left), -1);
                    if (onPlayer) onPlayer(id, false);
                }
            }
            break;
        case NetEvent::Type::Received:
            if (Client* c = byPeer(e.peer)) receive(*c, e);
            break;
        }
    }
    // Rejected or kicked in the loop above.
    std::erase_if(m_clients, [](const Client& c) { return c.peer == kNoPeer; });
    // Connected but never said hello: drop.
    for (Client& c : m_clients)
        if (c.id == 0 && c.peer != kNoPeer && now - c.connectedAt > m_config.helloTimeout) drop(c, "no hello");
    std::erase_if(m_clients, [](const Client& c) { return c.peer == kNoPeer; });

    if (now >= m_nextSnapshot) {
        const double interval = 1.0 / std::max<uint16_t>(1, m_config.snapshotHz);
        m_nextSnapshot = std::max(m_nextSnapshot + interval, now);
        for (Client& c : m_clients)
            if (c.id && c.peer != kNoPeer) sendSnapshot(c);
    }
}

// Players always; then bodies by accumulated priority until the packet's
// budget is used. Awake bodies gain 1 per snapshot, sleeping ones 0.1, a
// body that just fell asleep gets a boost so its resting pose arrives.
void NetServer::sendSnapshot(Client& c) {
    SnapshotMsg s;
    s.serverTimeMs = timeMs();
    if (m_hasLocal) s.players.push_back({ 0, m_local });
    for (const Client& o : m_clients)
        if (o.id && o.id != c.id && o.hasState) s.players.push_back({ o.id, o.accepted });
    // Bits: header 5+32+6+8, player ~120, body ~130 asleep / ~175 awake.
    int64_t budget = static_cast<int64_t>(m_config.snapshotBytes) * 8 - 51 - static_cast<int64_t>(s.players.size()) * 120;
    std::vector<std::pair<float, const NetBodyState*>> order;
    order.reserve(m_bodies.size());
    for (const NetBodyState& b : m_bodies) {
        float& p = c.priority[b.id];
        p += b.sleeping ? 0.1f : 1.0f;
        auto sent = c.sentSleeping.find(b.id);
        if (b.sleeping && (sent == c.sentSleeping.end() || !sent->second)) p += 5.0f;
        order.push_back({ p, &b });
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [p, b] : order) {
        const int64_t cost = b->sleeping ? 130 : 175;
        if (budget < cost || s.bodies.size() >= kMaxBodiesPerSnapshot) break;
        s.bodies.push_back(*b);
        budget -= cost;
        c.priority[b->id] = 0.0f;
        c.sentSleeping[b->id] = b->sleeping;
    }
    m_transport.send(c.peer, Channel::Unreliable, encode(MessageType::Snapshot, s));
}

std::vector<RemotePlayer> NetServer::players(double now) const {
    std::vector<RemotePlayer> out;
    for (const Client& c : m_clients) {
        if (!c.id) continue;
        RemotePlayer r;
        r.id = c.id;
        r.name = c.name;
        r.character = c.character;
        if (c.hasState && c.clock.valid()) {
            r.state = interpolate(c.states, c.clock.senderNow(now) - m_config.interpolationDelay, m_config.maxExtrapolation);
            r.hasState = true;
        }
        out.push_back(std::move(r));
    }
    return out;
}

// ---------------------------------------------------------------- client

NetClient::NetClient(ITransport& transport, const NetConfig& config) : m_transport(transport), m_config(config) {}

NetClient::~NetClient() { disconnect(); }

bool NetClient::connect(const std::string& address, uint16_t port, const std::string& name, const std::string& character, std::string* error) {
    disconnect();
    m_server = m_transport.connect(address, port, error);
    if (m_server == kNoPeer) {
        m_status = Status::Disconnected;
        m_statusText = error ? *error : "can't connect";
        return false;
    }
    m_name = name.substr(0, kMaxNameLength);
    m_character = character.substr(0, kMaxCharacterLength);
    m_status = Status::Connecting;
    m_statusText = "connecting to " + address + ":" + std::to_string(port);
    m_started = false; // timers start at the next update()
    return true;
}

void NetClient::disconnect() {
    if (m_server != kNoPeer) {
        m_transport.disconnect(m_server);
        std::vector<NetEvent> flush;
        m_transport.poll(flush); // sends the goodbye
        m_transport.close();
    }
    m_server = kNoPeer;
    if (m_status == Status::Connecting || m_status == Status::Connected) {
        m_status = Status::Disconnected;
        m_statusText = "left";
    }
    m_players.clear();
    m_bodies.clear();
    m_clock.reset();
    m_haveSnapshot = false;
}

void NetClient::sendEvent(uint16_t kind, const std::vector<uint8_t>& payload) {
    if (m_status != Status::Connected) return;
    GameEventMsg e{ 0, kind, payload };
    if (e.payload.size() > kMaxEventBytes) e.payload.resize(kMaxEventBytes);
    m_transport.send(m_server, Channel::Reliable, encode(MessageType::GameEvent, e));
}

void NetClient::receive(const NetEvent& e) {
    const std::optional<MessageType> type = peekType(e.data.data(), e.data.size());
    if (!type) { ++m_badPackets; return; }
    const uint8_t* d = e.data.data();
    const size_t n = e.data.size();
    switch (*type) {
    case MessageType::Welcome:
        if (auto m = decode<WelcomeMsg>(*type, d, n)) {
            m_playerId = m->playerId;
            m_status = Status::Connected;
            m_statusText = "connected as player " + std::to_string(m->playerId);
            m_clock.observe(m->serverTimeMs / 1000.0, m_now);
        } else ++m_badPackets;
        break;
    case MessageType::Reject:
        if (auto m = decode<RejectMsg>(*type, d, n)) {
            m_status = Status::Rejected;
            m_statusText = m->reason;
        } else ++m_badPackets;
        break;
    case MessageType::PlayerInfo:
        if (auto m = decode<PlayerInfoMsg>(*type, d, n)) {
            if (m->playerId == m_playerId && m_status == Status::Connected) break; // ourselves
            if (m->present) {
                Player& p = m_players[m->playerId];
                p.name = m->name;
                p.character = m->character;
            } else {
                m_players.erase(m->playerId);
            }
            if (onPlayer) onPlayer(m->playerId, m->present);
        } else ++m_badPackets;
        break;
    case MessageType::Correction:
        if (auto m = decode<CorrectionMsg>(*type, d, n)) {
            if (onCorrection) onCorrection(m->position);
        } else ++m_badPackets;
        break;
    case MessageType::GameEvent:
        if (auto m = decode<GameEventMsg>(*type, d, n)) {
            if (m->kind == kEventBodiesReset) m_bodies.clear();
            else if (onEvent) onEvent(*m);
        } else ++m_badPackets;
        break;
    case MessageType::Spawn:
        if (auto m = decode<SpawnMsg>(*type, d, n); m && e.channel == Channel::Reliable) {
            if (onSpawn) onSpawn(*m);
        } else ++m_badPackets;
        break;
    case MessageType::Despawn:
        if (auto m = decode<DespawnMsg>(*type, d, n); m && e.channel == Channel::Reliable) {
            m_bodies.erase(m->id);
            if (onDespawn) onDespawn(m->id);
        } else ++m_badPackets;
        break;
    case MessageType::Break:
        if (auto m = decode<BreakMsg>(*type, d, n); m && e.channel == Channel::Reliable) {
            if (onBreak) onBreak(*m);
        } else ++m_badPackets;
        break;
    case MessageType::Snapshot:
        if (auto m = decode<SnapshotMsg>(*type, d, n)) {
            // Unreliable packets can arrive late: an older snapshot still
            // fills in its time slot (Timeline keeps order).
            const double t = m->serverTimeMs / 1000.0;
            m_clock.observe(t, m_now);
            if (!m_haveSnapshot || static_cast<int32_t>(m->serverTimeMs - m_lastSnapshotMs) > 0) m_lastSnapshotMs = m->serverTimeMs;
            m_haveSnapshot = true;
            for (const SnapshotMsg::Player& p : m->players)
                if (p.id != m_playerId) m_players[p.id].states.push(t, p.state);
            for (const NetBodyState& b : m->bodies) m_bodies[b.id].push(t, b);
        } else ++m_badPackets;
        break;
    default:
        ++m_badPackets;
        break;
    }
}

void NetClient::update(double now) {
    m_now = now;
    if (m_server == kNoPeer) return;
    if (!m_started) {
        m_start = now;
        m_connectStarted = now;
        m_nextSend = now;
        m_started = true;
    }
    std::vector<NetEvent> events;
    m_transport.poll(events);
    for (NetEvent& e : events) {
        if (e.peer != m_server) continue;
        switch (e.type) {
        case NetEvent::Type::Connected: {
            HelloMsg h{ kProtocolVersion, m_config.gameId, m_name, m_character };
            sendMsg(m_transport, m_server, Channel::Reliable, MessageType::Hello, h);
            m_statusText = "saying hello";
            break;
        }
        case NetEvent::Type::Disconnected:
            if (m_status != Status::Rejected) {
                m_statusText = m_status == Status::Connecting ? "no answer (is the address and port right, and the host running?)"
                                                              : "the server closed the connection";
                m_status = Status::Disconnected;
            }
            m_server = kNoPeer;
            m_players.clear();
            return;
        case NetEvent::Type::Received:
            receive(e);
            break;
        }
    }
    if (m_status == Status::Rejected) {
        disconnect();
        m_status = Status::Rejected;
        return;
    }
    if (m_status == Status::Connecting && now - m_connectStarted > m_config.connectTimeout) {
        disconnect();
        m_status = Status::Disconnected;
        m_statusText = "no answer from the server";
        return;
    }
    if (m_status == Status::Connected && m_hasLocal && now >= m_nextSend) {
        m_nextSend = std::max(m_nextSend + 1.0 / std::max<uint16_t>(1, m_config.stateHz), now);
        PlayerStateMsg m{ timeMs(), m_local };
        sendMsg(m_transport, m_server, Channel::Unreliable, MessageType::PlayerState, m);
        m_local.flags &= static_cast<uint8_t>(~kPlayerTeleported); // sent once
    }
}

double NetClient::renderTime(double now) const {
    return m_clock.senderNow(now) - m_config.interpolationDelay;
}

std::vector<RemotePlayer> NetClient::players(double now) const {
    std::vector<RemotePlayer> out;
    for (const auto& [id, p] : m_players) {
        RemotePlayer r;
        r.id = id;
        r.name = p.name;
        r.character = p.character;
        if (!p.states.empty() && m_clock.valid()) {
            r.state = interpolate(p.states, renderTime(now), m_config.maxExtrapolation);
            r.hasState = true;
        }
        out.push_back(std::move(r));
    }
    return out;
}

bool NetClient::body(uint16_t id, double now, NetBodyState& out) const {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end() || it->second.empty() || !m_clock.valid()) return false;
    out = interpolate(it->second, renderTime(now), m_config.maxExtrapolation);
    return true;
}

std::vector<uint16_t> NetClient::bodyIds() const {
    std::vector<uint16_t> ids;
    for (const auto& kv : m_bodies) ids.push_back(kv.first);
    return ids;
}

} // namespace kke::net
