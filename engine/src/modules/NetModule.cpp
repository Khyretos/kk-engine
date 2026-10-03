#include "kke/modules/NetModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/net/EnetTransport.h"
#include "kke/Locomotion.h"
#include "kke/server/DirectoryNet.h"
#include "kke/server/ServerConfig.h"
#include "kke/net/Relay.h"
#include "kke/net/SecureTransport.h"
#include "kke/net/LevelSight.h"
#include "kke/net/LocomotionReplay.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kke {

namespace {
float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
// "addr", "addr:port" (the last ':' splits, so plain IPv4 and names work).
void splitAddress(const std::string& s, std::string& address, uint16_t& port) {
    const size_t colon = s.rfind(':');
    address = s;
    if (colon == std::string::npos) return;
    const int p = std::atoi(s.c_str() + colon + 1);
    if (p > 0 && p < 65536) {
        address = s.substr(0, colon);
        port = static_cast<uint16_t>(p);
    }
}
// Remote players' stand-ins in the local world: a capsule the size of a
// character, centred at feet + 0.9 m.
constexpr float kCapsuleRadius = 0.3f, kCapsuleHalfHeight = 0.6f, kCapsuleCentre = 0.9f;
// A replicated body further than this from where the host says it is jumps
// there instead of sweeping (a sweep that far would bulldoze the level).
constexpr float kSnapDistance = 2.0f;
// Client bodies: how hard they're steered toward the host's (per second),
// and how close counts as "the same" for a body the host has asleep.
constexpr float kPositionGain = 6.0f, kSteerRate = 12.0f;
constexpr float kRestError = 0.005f, kRestAngle = 0.01f; // 5 mm, about half a degree
constexpr float kSettleSpeed = 0.3f; // m/s: slower than this counts as settled here too
constexpr float kNearPlayer = 2.0f;  // m: a body this close to our moving player may be ours to push
} // namespace

NetModule::NetModule(const net::NetConfig& config) : m_config(config) {}
NetModule::~NetModule() { leave(); }

std::vector<ModuleDependency> NetModule::dependencies() const {
    return { { typeid(RigidBodyModule), false, "replicated bodies and remote players' capsules live in its world" },
#if KKE_ENABLE_FEMFX
             { typeid(PhysicsModule), false, "FEMFX breakables break the same way for every player" },
#endif
    };
}

double NetModule::now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void NetModule::init(Application& app) {
    m_app = &app;
    m_rigid = app.getModule<RigidBodyModule>();
#if KKE_ENABLE_FEMFX
    m_physics = app.getModule<PhysicsModule>();
#endif
    if (const char* n = std::getenv("KKE_NET_NAME"); n && *n) playerName = n;
    if (const char* p = std::getenv("KKE_NET_PASSWORD"); p && *p) m_config.password = p; // to join, or to require when hosting
    if (const char* r = std::getenv("KKE_NET_RELAY"); r && *r) relay = r;
    std::snprintf(m_passwordInput, sizeof(m_passwordInput), "%s", m_config.password.c_str());
    simulated.latencyMs = envFloat("KKE_NET_LAG", 0.0f);
    simulated.jitterMs = envFloat("KKE_NET_JITTER", 0.0f);
    simulated.lossPercent = envFloat("KKE_NET_LOSS", 0.0f);
    if (const char* r = std::getenv("KKE_NET_REPLAY"); r && *r && *r != '0') inputReplay = true;
    std::snprintf(m_nameInput, sizeof(m_nameInput), "%s", playerName.c_str());
    if (const char* d = std::getenv("KKE_DIRECTORIES"); d && *d) {
        directories.clear();
        std::string list = d;
        for (size_t start = 0; start <= list.size();) {
            const size_t comma = std::min(list.find(',', start), list.size());
            std::string item = list.substr(start, comma - start);
            item.erase(0, item.find_first_not_of(' '));
            item.erase(item.find_last_not_of(' ') + 1);
            if (!item.empty()) directories.push_back(item);
            start = comma + 1;
        }
    }
    if (!directories.empty()) {
        std::snprintf(m_directoryInput, sizeof(m_directoryInput), "%s", directories.front().c_str());
        browseDirectory(directories.front()); // the list is there when the panel opens
    }

    if (const char* mode = std::getenv("KKE_NET"); mode && *mode) m_envMode = mode;
}

void NetModule::frameStart(const UpdateContext&) {
    if (m_envMode.empty()) return;
    const std::string m = std::move(m_envMode);
    m_envMode.clear();
    std::string error;
    if (m == "host" || m.rfind("host:", 0) == 0) {
        uint16_t port = 0;
        if (m.size() > 5) port = static_cast<uint16_t>(std::atoi(m.c_str() + 5));
        if (!host(port, &error)) log::get(name())->error("KKE_NET={}: {}", m, error);
    } else if (m.rfind("join:", 0) == 0) {
        std::string address;
        uint16_t port = kDefaultPort;
        if (net::looksLikeJoinCode(m.substr(5))) address = m.substr(5); // CODE@relay:port: the port is the relay's
        else splitAddress(m.substr(5), address, port);
        if (!join(address, port, &error)) log::get(name())->error("KKE_NET={}: {}", m, error);
    } else {
        log::get(name())->error("KKE_NET='{}': expected host, host:PORT, join:ADDRESS[:PORT] or join:CODE[@RELAY]", m);
    }
}

void NetModule::shutdown() { leave(); }

std::string NetModule::discoveryInfo() const {
    const size_t players = 1 + m_localGuests.size() + (m_server ? m_server->clientCount() : 0);
    return playerName + "|" + std::to_string(players) + "/" + std::to_string(m_config.maxPlayers) + "|" + m_config.gameId;
}

bool NetModule::host(uint16_t port, std::string* error) {
    leave();
    // Port 0: the first free one from kDefaultPort, so a second game on
    // this PC just takes the next port (the LAN search asks the range).
    const uint16_t first = port ? port : kDefaultPort;
    const uint16_t last = port ? port : static_cast<uint16_t>(kDefaultPort + kPortRange - 1);
    std::string lastError;
    for (uint32_t p = first; p <= last; ++p) {
        auto enet = std::make_unique<net::EnetTransport>();
        net::EnetTransport* raw = enet.get();
        auto transport = std::make_unique<net::ConditionedTransport>(std::move(enet));
        transport->conditions = simulated;
        auto secure = std::make_unique<net::SecureTransport>(*transport); // a fresh key each time we host
        net::NetConfig config = m_config;
        config.inputReplay = inputReplay && m_rigid; // the players' movement runs in its world
        auto server = std::make_unique<net::NetServer>(*secure, config);
        if (!server->start(static_cast<uint16_t>(p), playerName, playerCharacter, &lastError)) continue;
        m_transport = std::move(transport);
        m_secure = std::move(secure);
        m_enet = raw;
        m_server = std::move(server);
        m_server->onEvent = [this](const net::GameEventMsg& e) { dispatchEvent(e); };
        m_server->onVoice = [this](const net::VoiceMsg& m) { for (const auto& l : m_voiceListeners) l(m); };
        m_server->voice = voiceRules;
        m_server->limits = movementLimits;
        if (m_rigid) {
            m_moveCheck = std::make_unique<net::WorldMoveCheck>(m_rigid->world(), [this](RigidWorld::BodyId b) {
                return std::any_of(m_capsules.begin(), m_capsules.end(), [b](const auto& kv) { return kv.second == b; });
            });
            m_server->checkMove = [this](uint8_t id, const net::NetPlayerState& from, const net::NetPlayerState& to, double dt) {
                if (!checkMoves || !m_moveCheck) return true;
                m_moveCheck->settings = moveCheckSettings;
                const net::WorldMoveCheck::Verdict v = m_moveCheck->check(id, from, to, dt);
                if (v == net::WorldMoveCheck::Verdict::Ok) return true;
                double& last = m_moveLogAt[id];
                if (now() - last > 5.0) { // one line per player per 5 s, however many states get refused
                    last = now();
                    log::get(name())->warn("Player {} {}: sent back to ({:.1f}, {:.1f}, {:.1f})", id,
                                           v == net::WorldMoveCheck::Verdict::ThroughWall ? "moved through a wall" : "is flying",
                                           from.position.x, from.position.y, from.position.z);
                }
                return false;
            };
            m_visibility = std::make_unique<net::Visibility>(net::levelSight(m_rigid->world()));
            m_server->sendPlayer = [this](uint8_t viewer, uint8_t subject) { return !fogOfWar || !m_visibility || m_visibility->visible(viewer, subject); };
        }
        // Whatever broke before hosting is news to the clients too.
        for (auto& [id, b] : m_breakables) b.sentBorders = 0;
        m_server->onPlayer = [this](uint8_t id, bool joined) {
            if (!joined && m_moveCheck) m_moveCheck->forget(id);
            if (!joined && m_visibility) m_visibility->forget(id);
            if (!joined) m_moveLogAt.erase(id);
            if (!joined) dropReplayed(id);
            log::get(name())->info("Player {} {} ({} connected)", id, joined ? "joined" : "left", m_server->clientCount());
            if (m_enet) m_enet->setDiscoveryInfo(discoveryInfo());
            if (onPlayer) onPlayer(id, joined);
            for (const auto& listener : m_playerListeners) listener(id, joined);
        };
        for (const auto& [slot, g] : m_localGuests)
            if (!m_server->addLocalGuest(static_cast<uint8_t>(slot), g.name, g.character))
                log::get(name())->warn("Local player {} ({}): no room in this game", slot + 1, g.name);
        m_enet->setDiscoveryInfo(discoveryInfo());
        m_role = Role::Host;
        m_hostingReplay = config.inputReplay;
        m_status = "hosting on port " + std::to_string(p);
        if (!relay.empty()) {
            // A join code: friends type it, nobody forwards a port.
            m_relayHost = std::make_unique<net::RelayHost>(*m_enet);
            m_relayHost->log = [this](const std::string& line) { log::get(name())->info("{}", line); };
            const uint16_t hostedPort = static_cast<uint16_t>(p);
            m_relayHost->onCode = [this, hostedPort](const std::string&) {
                m_status = "hosting on port " + std::to_string(hostedPort) + ", join code " + m_relayHost->joinText();
            };
            m_enet->onRaw = [this](const std::string& h, uint16_t port, const std::vector<uint8_t>& d) {
                if (m_relayHost) m_relayHost->onDatagram(h, port, d);
            };
            m_enet->realAddress = [this](const std::string& h, uint16_t port) { return m_relayHost ? m_relayHost->realAddress(h, port) : std::string(); };
            std::string relayError;
            if (!m_relayHost->start({ relay }, m_config.gameId, m_secure->identity().publicKey, nullptr, &relayError)) {
                log::get(name())->warn("No join code: {}", relayError);
                m_relayHost.reset();
            }
        }
        m_search.reset();
        applyFollowers();
        log::get(name())->info("Hosting '{}' on UDP port {} ({}{})", playerName, p, m_transport->backendName(),
                               config.inputReplay ? ", input replay" : "");
        return true;
    }
    if (error) *error = lastError;
    m_status = "can't host: " + lastError;
    return false;
}

bool NetModule::splitTypedAddress(const std::string& typed, std::string& address, uint16_t& port) {
    const size_t first = typed.find_first_not_of(" \t");
    const std::string t = first == std::string::npos ? std::string() : typed.substr(first, typed.find_last_not_of(" \t") - first + 1);
    address = t;
    if (net::looksLikeJoinCode(t)) return true; // CODE or CODE@relay[:port]: NetModule::join reads it
    auto portOf = [&](const std::string& digits) {
        if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos) return false;
        const int p = std::stoi(digits);
        if (p < 1 || p > 65535) return false;
        port = static_cast<uint16_t>(p);
        return true;
    };
    if (!t.empty() && t.front() == '[') {
        // [IPv6] or [IPv6]:port
        const size_t close = t.find(']');
        if (close == std::string::npos) return false;
        address = t.substr(1, close - 1);
        if (close + 1 == t.size()) return true;
        return t[close + 1] == ':' && portOf(t.substr(close + 2));
    }
    const size_t colon = t.find(':');
    if (colon == std::string::npos || t.find(':', colon + 1) != std::string::npos) return true; // a name, IPv4, or a bare IPv6
    address = t.substr(0, colon);
    return portOf(t.substr(colon + 1));
}

bool NetModule::joinTyped(const std::string& typed, uint16_t port, std::string* error) {
    std::string address;
    if (!splitTypedAddress(typed, address, port) || address.empty()) {
        if (error) *error = typed.empty() ? "type an address or a join code first" : "'" + typed + "' isn't an address, address:port or join code";
        return false;
    }
    return join(address, port, error);
}

bool NetModule::join(const std::string& address, uint16_t port, std::string* error) {
    leave();
    auto enet = std::make_unique<net::EnetTransport>();
    net::EnetTransport* raw = enet.get();
    auto transport = std::make_unique<net::ConditionedTransport>(std::move(enet));
    transport->conditions = simulated;
    auto secure = std::make_unique<net::SecureTransport>(*transport);
    auto client = std::make_unique<net::NetClient>(*secure, m_config);
    std::string err;
    std::unique_ptr<net::RelayJoin> byCode;
    if (net::looksLikeJoinCode(address)) {
        // A join code: ask the relay where it is first (updateRelayJoin connects).
        const auto target = net::parseJoinTarget(address, relay, &err);
        if (target && raw->open(&err)) {
            byCode = std::make_unique<net::RelayJoin>(*raw);
            byCode->start(*target, m_config.gameId, now(), &err);
        }
        if (!byCode || byCode->state() == net::RelayJoin::State::Failed) {
            if (error) *error = err;
            m_status = "can't join: " + err;
            return false;
        }
    } else if (!client->connect(address, port, playerName, playerCharacter, &err)) {
        if (error) *error = err;
        m_status = "can't join: " + err;
        return false;
    }
    m_transport = std::move(transport);
    m_secure = std::move(secure);
    m_enet = raw;
    m_relayJoin = std::move(byCode);
    if (m_relayJoin) m_enet->onRaw = [this](const std::string& h, uint16_t p, const std::vector<uint8_t>& d) { if (m_relayJoin) m_relayJoin->onDatagram(h, p, d); };
    m_client = std::move(client);
    m_client->onEvent = [this](const net::GameEventMsg& e) {
        if (e.kind == net::kEventServerMessage) {
            m_serverMessage.assign(e.payload.begin(), e.payload.end());
            log::get(name())->info("Server: {}", m_serverMessage);
        }
        dispatchEvent(e);
    };
    m_client->onCorrection = [this](const glm::vec3& p) { if (onCorrection) onCorrection(p); };
    m_client->onPlayer = [this](uint8_t id, bool joined) {
        if (onPlayer) onPlayer(id, joined);
        for (const auto& listener : m_playerListeners) listener(id, joined);
    };
    m_client->onGuestCorrection = [this](uint8_t slot, const glm::vec3& p) { if (onLocalCorrection) onLocalCorrection(slot, p); };
    m_client->onGuest = [this](uint8_t slot, uint8_t id, const std::string& reason) {
        auto it = m_localGuests.find(slot);
        const std::string who = it != m_localGuests.end() ? it->second.name : std::string("?");
        if (id) log::get(name())->info("Local player {} ({}) joined as player {}", slot + 1, who, id);
        else log::get(name())->warn("Local player {} ({}) is not in the game: {}", slot + 1, who, reason);
    };
    for (const auto& [slot, g] : m_localGuests) m_client->addGuest(static_cast<uint8_t>(slot), g.name, g.character);
    m_client->onSpawn = [this](const net::SpawnMsg& m) { onSpawnMsg(m); };
    m_client->onDespawn = [this](uint16_t id) { onDespawnMsg(id); };
    m_client->onBreak = [this](const net::BreakMsg& m) { applyBreak(m); };
    m_client->onVoice = [this](const net::VoiceMsg& m) { for (const auto& l : m_voiceListeners) l(m); };
    m_client->onInputAck = [this](uint32_t tick, const net::NetPlayerState& s) {
        if (m_prediction) m_prediction->acknowledge(tick, s);
    };
    m_role = Role::Client;
    applyFollowers();
    m_status = m_relayJoin ? m_relayJoin->status() : "joining " + address + ":" + std::to_string(port);
    m_search.reset();
    if (m_relayJoin) log::get(name())->info("Joining {} as '{}'", address, playerName);
    else log::get(name())->info("Joining {}:{} as '{}'", address, port, playerName);
    return true;
}

void NetModule::updateRelayJoin(double t) {
    // No connection yet: service the socket so the relay's answers and the
    // server's punches (intercepted) come in.
    std::vector<net::NetEvent> ignored;
    m_transport->poll(ignored);
    m_relayJoin->update(t);
    m_status = m_relayJoin->status();
    if (!m_relayJoin->done()) return;
    if (m_relayJoin->state() == net::RelayJoin::State::Failed) {
        const std::string why = "can't join: " + m_relayJoin->error();
        log::get(name())->warn("{}", why);
        leave();
        m_status = why;
        return;
    }
    const bool direct = m_relayJoin->state() == net::RelayJoin::State::Direct;
    m_secure->expectServerKey(m_relayJoin->serverKey()); // the key the relay vouched for, or no connection
    std::string err;
    if (!m_client->connect(m_relayJoin->connectHost(), m_relayJoin->connectPort(), playerName, playerCharacter, &err)) {
        leave();
        m_status = "can't join: " + err;
        return;
    }
    log::get(name())->info("Join code found: connecting {} ({}:{})", direct ? "directly" : "through the relay", m_relayJoin->connectHost(),
                           m_relayJoin->connectPort());
    m_status = direct ? "connecting directly" : "connecting through the relay";
    m_relayJoin.reset();
    m_enet->onRaw = nullptr;
}

std::string NetModule::joinCode() const { return m_relayHost ? m_relayHost->joinText() : std::string();
}

void NetModule::leave() {
    if (m_role == Role::Offline && !m_transport) return;
    if (m_client) m_client->disconnect();
    if (m_relayHost) m_relayHost->stop(); // the code frees at once
    m_relayHost.reset();
    m_relayJoin.reset();
    if (m_enet) {
        m_enet->onRaw = nullptr;
        m_enet->realAddress = nullptr;
    }
    if (m_server) m_server->stop();
    // Flush the goodbye before the socket closes.
    if (m_transport) {
        std::vector<net::NetEvent> ignored;
        m_transport->poll(ignored);
        m_transport->close();
    }
    stopPredicting();
    while (!m_replayed.empty()) dropReplayed(m_replayed.begin()->first);
    m_replayClock = 0.0;
    m_hostingReplay = false;
    m_client.reset();
    m_serverMessage.clear();
    m_server.reset();
    m_secure.reset();
    m_transport.reset();
    m_enet = nullptr;
    m_role = Role::Offline;
    m_status = "offline";
    m_remote.clear();
    m_moveCheck.reset();
    m_visibility.reset();
    m_moveLogAt.clear();
    dropSpawned();
    applyFollowers();
    if (m_rigid) {
        RigidWorld& w = m_rigid->world();
        for (auto& [id, body] : m_capsules) w.remove(body);
    }
    m_capsules.clear();
}

bool NetModule::connected() const {
    if (m_role == Role::Host) return true;
    return m_client && m_client->status() == net::NetClient::Status::Connected;
}

uint8_t NetModule::localPlayerId() const { return m_client ? m_client->playerId() : 0; }

// ---------------------------------------------------------------- input replay

void NetModule::setPlayer(Locomotion* locomotion, RigidWorld::CharacterId character) {
    if (locomotion != m_player || character != m_playerCharacter) stopPredicting();
    m_player = locomotion;
    m_playerCharacter = character;
}

void NetModule::stopPredicting() {
    if (m_prediction)
        log::get(name())->info("Input replay: {} ticks predicted, {} corrections ({} ticks replayed)", m_prediction->nextTick(),
                               m_prediction->corrections, m_prediction->replayedTicks);
    m_prediction.reset();
    m_predictedMover.reset(); // hands the character back to RigidWorld::step()
    m_tickClock = 0.0;
    m_pendingUp = false;
    m_loggedCorrections = 0;
}

bool NetModule::stepPlayer(const net::InputFrame& in, float frameDt) {
    const bool predicting = m_client && m_client->status() == net::NetClient::Status::Connected && m_client->inputReplay() && m_rigid && m_player;
    if (!predicting) {
        stopPredicting();
        return false;
    }
    if (!m_prediction) {
        m_predictedMover = std::make_unique<net::LocomotionReplay>(m_rigid->world(), m_playerCharacter, *m_player);
        m_prediction = std::make_unique<net::Prediction>(*m_predictedMover);
        log::get(name())->info("Input replay: the host moves our player, predicted here at {} Hz", m_client->tickHz());
    }
    // Fixed ticks at the host's rate, whatever the frame rate: both sides
    // must step the same dt. A press on a frame between ticks waits for the next.
    const float tickDt = 1.0f / static_cast<float>(std::max<uint16_t>(1, m_client->tickHz()));
    m_pendingUp = m_pendingUp || (in.buttons & net::kButtonUp) != 0;
    m_tickClock += frameDt;
    int ticks = 0;
    while (m_tickClock >= tickDt && ticks < 15) {
        net::InputFrame f = in;
        f.buttons = static_cast<uint8_t>((in.buttons & ~net::kButtonUp) | (m_pendingUp ? net::kButtonUp : 0));
        m_pendingUp = false;
        m_prediction->tick(f, tickDt);
        m_tickClock -= tickDt;
        ++ticks;
    }
    if (ticks == 15) m_tickClock = 0.0; // a long stall: don't try to run it all
    if (ticks) m_client->sendInputs(m_prediction->unacknowledged(net::kMaxInputsPerMsg));
    if (m_prediction->corrections > m_loggedCorrections && now() - m_correctionLogAt > 5.0) {
        m_correctionLogAt = now();
        m_loggedCorrections = m_prediction->corrections;
        log::get(name())->info("Input replay: the host put our player right ({} times in {} ticks)", m_prediction->corrections, m_prediction->nextTick());
    }
    return true;
}

glm::vec3 NetModule::playerDrawOffset() const { return m_prediction ? m_prediction->visualOffset() : glm::vec3(0.0f); }

size_t NetModule::predictionCorrections() const { return m_prediction ? m_prediction->corrections : 0; }

void NetModule::dropReplayed(uint8_t id) {
    auto it = m_replayed.find(id);
    if (it == m_replayed.end()) return;
    it->second.mover.reset();
    it->second.locomotion.reset();
    if (m_rigid) m_rigid->world().removeCharacter(it->second.character);
    m_replayed.erase(it);
}

// Host: each client's inputs, one per tick, run on its own character.
void NetModule::runReplayedPlayers(double frameDt) {
    if (!m_server || !m_rigid || !m_hostingReplay) return;
    RigidWorld& w = m_rigid->world();
    const double tickDt = 1.0 / 60.0; // NetConfig::tickHz, told to clients at Welcome
    m_replayClock = std::min(m_replayClock + frameDt, 15 * tickDt); // a slow frame: catch up, but not forever
    while (m_replayClock >= tickDt) {
        m_replayClock -= tickDt;
        m_server->inputTick();
        for (const net::RemotePlayer& p : m_server->players(now())) {
            net::InputFrame in;
            bool moved = false;
            while (m_server->nextInput(p.id, in)) {
                auto it = m_replayed.find(p.id);
                if (it == m_replayed.end()) {
                    ReplayedPlayer r;
                    RigidWorld::CharacterDesc cd;
                    cd.position = replaySpawn ? replaySpawn(p.id) : (m_hasLocal ? m_local.position : glm::vec3(0.0f)) + glm::vec3(1.5f * p.id, 0.0f, 0.0f);
                    r.character = w.addCharacter(cd);
                    r.locomotion = std::make_unique<Locomotion>(w, r.character);
                    r.mover = std::make_unique<net::LocomotionReplay>(w, r.character, *r.locomotion);
                    it = m_replayed.emplace(p.id, std::move(r)).first;
                    log::get(name())->info("Player {} moves by input replay", p.id);
                }
                it->second.mover->step(in, static_cast<float>(tickDt));
                moved = true;
            }
            if (moved) m_server->setPlayerState(p.id, m_replayed.at(p.id).mover->state());
        }
    }
}

void NetModule::setLocalPlayer(const net::NetPlayerState& state) {
    m_local = state;
    m_hasLocal = true;
}

void NetModule::addLocalPlayer(int slot, const std::string& who, const std::string& character) {
    if (slot <= 0 || slot >= kMaxLocalPlayers) {
        log::get(name())->error("addLocalPlayer: slot {} (1 .. {})", slot, kMaxLocalPlayers - 1);
        return;
    }
    if (auto it = m_localGuests.find(slot); it != m_localGuests.end() && it->second.name == who && it->second.character == character)
        return; // already in
    if (m_server) m_server->removeLocalGuest(static_cast<uint8_t>(slot)); // someone else had that slot
    LocalGuest& g = m_localGuests[slot];
    g.name = who;
    g.character = character;
    g.hasState = false;
    if (m_server) {
        if (m_server->addLocalGuest(static_cast<uint8_t>(slot), g.name, g.character)) log::get(name())->info("Local player {} ({}) joined", slot + 1, g.name);
        else log::get(name())->warn("Local player {} ({}): no room in this game", slot + 1, g.name);
        if (m_enet) m_enet->setDiscoveryInfo(discoveryInfo());
    } else if (m_client) {
        m_client->addGuest(static_cast<uint8_t>(slot), g.name, g.character);
    }
}

void NetModule::removeLocalPlayer(int slot) {
    if (m_localGuests.erase(slot) == 0) return;
    if (m_server) {
        m_server->removeLocalGuest(static_cast<uint8_t>(slot));
        if (m_enet) m_enet->setDiscoveryInfo(discoveryInfo());
    }
    if (m_client) m_client->removeGuest(static_cast<uint8_t>(slot));
}

void NetModule::setLocalPlayer(int slot, const net::NetPlayerState& state) {
    if (slot == 0) return setLocalPlayer(state);
    auto it = m_localGuests.find(slot);
    if (it == m_localGuests.end()) return;
    it->second.state = state;
    it->second.hasState = true;
}

uint8_t NetModule::localPlayerId(int slot) const {
    if (slot == 0) return localPlayerId();
    if (slot < 0 || slot >= kMaxLocalPlayers) return 0;
    if (m_server) return m_server->localGuestId(static_cast<uint8_t>(slot));
    return m_client ? m_client->guestId(static_cast<uint8_t>(slot)) : 0;
}

bool NetModule::isLocalPlayer(uint8_t playerId) const {
    if (m_client) return m_client->isOurs(playerId);
    if (!m_server) return false;
    if (playerId == 0) return true;
    for (const auto& [slot, g] : m_localGuests)
        if (m_server->localGuestId(static_cast<uint8_t>(slot)) == playerId) return true;
    return false;
}

uint16_t NetModule::replicateBody(RigidWorld::BodyId body) {
    if (m_nextLevelBody >= kFirstSpawnId) {
        log::get(name())->error("replicateBody: more than {} level bodies; body {} is not replicated", kFirstSpawnId, body);
        return kFirstSpawnId - 1;
    }
    const uint16_t id = m_nextLevelBody++;
    m_bodies[id] = body;
    return id;
}

void NetModule::clearBodies() {
    std::erase_if(m_bodies, [](const auto& kv) { return kv.first < kFirstSpawnId; });
    m_nextLevelBody = 0;
    if (m_server) {
        m_server->setBodies({});
        m_server->sendEvent(net::kEventBodiesReset, {});
    }
}

// ------------------------------------------------------------------ breakables

uint16_t NetModule::replicateBreakable(uint32_t handle) {
    if (m_nextLevelBreakable >= kFirstSpawnId) {
        log::get(name())->error("replicateBreakable: more than {} level breakables; breakable {} is not replicated", kFirstSpawnId, handle);
        return kFirstSpawnId - 1;
    }
    const uint16_t id = m_nextLevelBreakable++;
    bindSpawnedBreakable(id, handle);
    return id;
}

void NetModule::clearBreakables() {
    for (auto it = m_breakables.begin(); it != m_breakables.end();) {
        if (it->first >= kFirstSpawnId) { ++it; continue; }
        if (m_server) m_server->forgetBreaks(it->first);
        it = m_breakables.erase(it);
    }
    std::erase_if(m_pendingBreaks, [](const auto& kv) { return kv.first < kFirstSpawnId; });
    m_nextLevelBreakable = 0;
}

void NetModule::bindSpawnedBreakable(uint16_t id, uint32_t handle) {
    NetBreakable& b = m_breakables[id];
    b = NetBreakable{ handle, 0, false };
    applyFollowers();
    // Breaks that came before we had it (a late joiner's level loading
    // after the host's messages): apply them now.
    auto pending = m_pendingBreaks.find(id);
    if (pending != m_pendingBreaks.end()) {
        const std::vector<net::BreakMsg> msgs = std::move(pending->second);
        m_pendingBreaks.erase(pending);
        for (const net::BreakMsg& m : msgs) applyBreak(m);
    }
}

void NetModule::applyFollowers() {
#if KKE_ENABLE_FEMFX
    if (!m_physics) return;
    for (const auto& [id, b] : m_breakables) m_physics->setBreakableFollower(b.handle, m_role == Role::Client);
#endif
}

// Host, each frame: a breakable with more broken borders than last time
// sends its borders (the server skips the ones it already sent).
void NetModule::pollBreaks() {
#if KKE_ENABLE_FEMFX
    if (!m_physics || !m_server) return;
    for (auto& [id, b] : m_breakables) {
        const size_t count = m_physics->brokenBorderCount(b.handle);
        if (count == b.sentBorders) continue;
        b.sentBorders = count;
        std::vector<std::pair<uint16_t, uint16_t>> borders;
        for (const auto& [p, q] : m_physics->brokenBorders(b.handle)) {
            if (p > net::kMaxChunkId || q > net::kMaxChunkId) {
                log::get(name())->error("breakable {}: piece id {} doesn't fit the network message; that border isn't sent", b.handle, std::max(p, q));
                continue;
            }
            borders.push_back({ static_cast<uint16_t>(p), static_cast<uint16_t>(q) });
        }
        m_server->breakBorders(id, m_physics->breakableSeed(b.handle), borders);
    }
#endif
}

void NetModule::applyBreak(const net::BreakMsg& m) {
    auto it = m_breakables.find(m.id);
    if (it == m_breakables.end()) {
        // Not ours yet (its spawn or our level comes later): keep it, within reason.
        if (m_pendingBreaks.size() >= 1024 && !m_pendingBreaks.count(m.id)) {
            log::get(name())->error("breaks for breakable {}: 1024 unknown breakables already waiting; dropped", m.id);
            return;
        }
        m_pendingBreaks[m.id].push_back(m);
        return;
    }
#if KKE_ENABLE_FEMFX
    if (!m_physics) return;
    NetBreakable& b = it->second;
    const uint32_t seed = m_physics->breakableSeed(b.handle);
    if (seed != m.seed) {
        // Baked with other pieces (another fracture seed, or another
        // object in that slot): its borders mean nothing here.
        if (!b.seedWarned)
            log::get(name())->error("breakable {} (network {}) was built with fracture seed {}, the host's with {}: it won't follow the host's breaks",
                                    b.handle, m.id, seed, m.seed);
        b.seedWarned = true;
        return;
    }
    std::vector<std::pair<uint32_t, uint32_t>> borders(m.borders.begin(), m.borders.end());
    m_physics->applyBrokenBorders(b.handle, borders);
#endif
}

// ------------------------------------------------------------------ spawned objects

uint16_t NetModule::spawn(uint16_t kind, const std::vector<uint8_t>& desc, bool persistent) {
    if (!m_server) return 0;
    if (desc.size() > net::kMaxSpawnBytes) {
        log::get(name())->error("spawn: a {}-byte description (kind {}) is over the {}-byte limit; not sent", desc.size(), kind, net::kMaxSpawnBytes);
        return 0;
    }
    if (m_spawned.size() >= 65536u - kFirstSpawnId) {
        log::get(name())->error("spawn: all {} spawn ids are in use; kind {} not sent", 65536u - kFirstSpawnId, kind);
        return 0;
    }
    while (m_spawned.count(m_nextSpawn)) m_nextSpawn = m_nextSpawn == 0xFFFF ? kFirstSpawnId : static_cast<uint16_t>(m_nextSpawn + 1);
    const uint16_t id = m_nextSpawn;
    m_nextSpawn = m_nextSpawn == 0xFFFF ? kFirstSpawnId : static_cast<uint16_t>(m_nextSpawn + 1);
    m_spawned.insert(id);
    m_server->spawn(net::SpawnMsg{ id, kind, desc }, persistent);
    return id;
}

void NetModule::despawn(uint16_t id) {
    if (m_server && m_spawned.erase(id)) m_server->despawn(id);
    m_bodies.erase(id);
    m_breakables.erase(id);
}

void NetModule::bindSpawnedBody(uint16_t id, RigidWorld::BodyId body) { m_bodies[id] = body; }

void NetModule::onSpawnMsg(const net::SpawnMsg& m) {
    if (m.id < kFirstSpawnId) {
        log::get(name())->error("the host spawned object {} in the level's id range; ignored", m.id);
        return;
    }
    if (m_bodies.count(m.id) || m_breakables.count(m.id)) onDespawnMsg(m.id); // the id was reused: the old one is gone
    for (const auto& listener : m_spawnListeners) listener(m);
}

void NetModule::onDespawnMsg(uint16_t id) {
    m_bodies.erase(id);
    m_breakables.erase(id);
    m_pendingBreaks.erase(id);
    for (const auto& listener : m_despawnListeners) listener(id);
}

void NetModule::dropSpawned() {
    std::erase_if(m_bodies, [](const auto& kv) { return kv.first >= kFirstSpawnId; });
    std::erase_if(m_breakables, [](const auto& kv) { return kv.first >= kFirstSpawnId; });
    m_pendingBreaks.clear();
    m_spawned.clear();
    m_nextSpawn = kFirstSpawnId;
}

void NetModule::sendEvent(uint16_t kind, const std::vector<uint8_t>& payload) {
    if (m_server) m_server->sendEvent(kind, payload);
    else if (m_client && connected()) m_client->sendEvent(kind, payload);
}

void NetModule::dispatchEvent(const net::GameEventMsg& e) {
    if (onEvent) onEvent(e);
    for (const auto& listener : m_listeners) listener(e);
}

void NetModule::relayEvent(const net::GameEventMsg& e) {
    if (m_server) m_server->relayEvent(e);
}

void NetModule::sendEventTo(uint8_t playerId, uint16_t kind, const std::vector<uint8_t>& payload) {
    if (m_server) m_server->sendEventTo(playerId, kind, payload);
}

void NetModule::setPlayerGroup(uint8_t playerId, uint16_t group) {
    if (m_server) m_server->setGroup(playerId, group);
}

uint16_t NetModule::playerGroup(uint8_t playerId) const { return m_server ? m_server->group(playerId) : 0; }

void NetModule::showGroups(uint8_t playerId, std::vector<uint16_t> groups) {
    if (m_server) m_server->showGroups(playerId, std::move(groups));
}

void NetModule::setBodyGroup(uint16_t netId, uint16_t group) {
    if (m_server) m_server->setBodyGroup(netId, group);
}

void NetModule::setSolid(uint8_t playerId, bool solid) {
    if (m_server) m_server->setSolid(playerId, solid);
}

void NetModule::fixedUpdate(const FixedUpdateContext& ctx) {
    if (m_role == Role::Client) driveClientBodies(ctx.fixedDt);
    syncRemoteCapsules(ctx.fixedDt);
}

// A client simulates its copies of the host's bodies too (so walking into
// a crate pushes it right away, no round trip), and steers each one toward
// where the host has it: the host's state, ~100 ms old, carried forward by
// its velocity to about now. The steering is a velocity blend, not a
// teleport, so local contacts stay stable; far off (a reset, a missed
// tumble) it jumps. A body the host has asleep and we have in the same
// place is left alone, so settled piles sleep on the client as well.
void NetModule::driveClientBodies(float dt) {
    if (!m_rigid || !m_client) return;
    RigidWorld& w = m_rigid->world();
    const double t = now();
    const float lead = static_cast<float>(m_config.interpolationDelay);
    const float blend = 1.0f - std::exp(-kSteerRate * dt);
    net::NetBodyState s;
    for (const auto& [id, b] : m_bodies) {
        if (!m_client->body(id, t, s)) continue;
        const glm::vec3 target = s.sleeping ? s.position : s.position + s.velocity * lead;
        const glm::vec3 err = target - w.position(b);
        const glm::quat q = w.rotation(b);
        glm::quat dq = s.rotation * glm::inverse(q);
        if (dq.w < 0.0f) dq = -dq; // the short way round
        const float angle = 2.0f * std::acos(std::min(1.0f, dq.w));
        if (glm::length(err) > kSnapDistance) {
            w.setTransform(b, target, s.rotation);
            w.setVelocity(b, s.velocity);
            w.setAngularVelocity(b, glm::vec3(0.0f));
            continue;
        }
        if (s.sleeping && glm::length(err) < kRestError && angle < kRestAngle) continue;
        const bool pushing = m_hasLocal && glm::length(w.position(b) - m_local.position) < kNearPlayer &&
                             glm::length(glm::vec2(m_local.velocity.x, m_local.velocity.z)) > kSettleSpeed;
        if (s.sleeping && !pushing && glm::length(w.velocity(b)) < kSettleSpeed) {
            // Settled on the host, (nearly) still here: slide it the last
            // bit into place. Velocity steering alone stalls here, eaten
            // by ground friction a few centimetres short. Not while we're
            // walking into it: that's us starting to push it.
            w.setTransform(b, glm::mix(w.position(b), target, blend), glm::slerp(q, s.rotation, blend));
            w.setVelocity(b, glm::vec3(0.0f));
            w.setAngularVelocity(b, glm::vec3(0.0f));
            continue;
        }
        const glm::vec3 wantV = s.velocity + err * kPositionGain;
        w.setVelocity(b, glm::mix(w.velocity(b), wantV, blend));
        const float sinHalf = std::sqrt(std::max(0.0f, 1.0f - dq.w * dq.w));
        const glm::vec3 axis = sinHalf > 1e-4f ? glm::vec3(dq.x, dq.y, dq.z) / sinHalf : glm::vec3(0.0f);
        w.setAngularVelocity(b, glm::mix(w.angularVelocity(b), axis * (angle * kPositionGain), blend));
    }
}

// Other players push crates and block us through a kinematic capsule each.
void NetModule::syncRemoteCapsules(float dt) {
    if (!m_rigid) return;
    RigidWorld& w = m_rigid->world();
    // Input replay: players don't block each other (the host's characters
    // don't collide with one another), so a client's prediction may not
    // bump into stand-ins the host doesn't have either.
    const bool replay = m_hostingReplay || (m_client && m_client->inputReplay()) || !standIns;
    // A spectator (NetServer::setSolid false) mustn't touch the ball.
    auto ghost = [this](uint8_t id) { return m_server && !m_server->solid(id); };
    for (auto it = m_capsules.begin(); it != m_capsules.end();) {
        const bool present = !replay && !m_replayed.count(it->first) && !ghost(it->first) &&
                             std::any_of(m_remote.begin(), m_remote.end(), [&](const net::RemotePlayer& p) { return p.id == it->first && p.hasState; });
        if (present) { ++it; continue; }
        w.remove(it->second);
        it = m_capsules.erase(it);
    }
    for (const net::RemotePlayer& p : m_remote) {
        if (replay || !p.hasState || ghost(p.id)) continue;
        const glm::vec3 centre = p.state.position + glm::vec3(0.0f, kCapsuleCentre, 0.0f);
        auto it = m_capsules.find(p.id);
        if (it == m_capsules.end()) {
            RigidWorld::BodyDesc d;
            d.shape = RigidWorld::Shape::Capsule;
            d.radius = kCapsuleRadius;
            d.halfHeight = kCapsuleHalfHeight;
            d.motion = RigidWorld::Motion::Kinematic;
            d.position = centre;
            const RigidWorld::BodyId b = w.add(d);
            if (b != RigidWorld::kNoBody) m_capsules[p.id] = b;
            continue;
        }
        const glm::quat upright(1.0f, 0.0f, 0.0f, 0.0f);
        if (glm::length(w.position(it->second) - centre) > kSnapDistance) w.setTransform(it->second, centre, upright);
        else w.moveKinematic(it->second, centre, upright, dt);
    }
}

void NetModule::sendVoice(net::VoiceChannel channel, uint16_t seq, const std::vector<uint8_t>& opusFrame) {
    if (m_server) m_server->sendVoice(net::VoiceMsg{ 0, channel, seq, opusFrame });
    else if (m_client) m_client->sendVoice(channel, seq, opusFrame);
}

void NetModule::update(const UpdateContext& ctx) {
    if (m_browser && !m_browser->done()) {
        m_browser->update();
        if (m_browser->done()) {
            m_browseStatus = std::to_string(m_browser->servers().size()) + (m_browser->servers().size() == 1 ? " server" : " servers");
            log::get(name())->info("Server list: {}", m_browseStatus);
        } else if (now() > m_browseUntil && m_browseStatus.rfind("asking", 0) == 0) {
            m_browseStatus = "no answer (is the address right, and the directory running?)";
            log::get(name())->info("Server list: {}", m_browseStatus);
        }
    }
    if (m_server) m_server->voice = voiceRules; // the host may change them while playing
    const double t = now();
    if (m_transport) m_transport->conditions = simulated;
    if (m_relayHost) m_relayHost->update(t);
    if (m_relayJoin) {
        updateRelayJoin(t);
        if (m_relayJoin) return; // still finding the server
    }
    if (m_server) {
        if (m_hasLocal) m_server->setLocalState(m_local);
        for (const auto& [slot, g] : m_localGuests)
            if (g.hasState) m_server->setLocalGuestState(static_cast<uint8_t>(slot), g.state);
        if (m_rigid) {
            RigidWorld& w = m_rigid->world();
            std::vector<net::NetBodyState> bodies;
            bodies.reserve(m_bodies.size());
            for (const auto& [id, body] : m_bodies) {
                net::NetBodyState s;
                s.id = id;
                s.position = w.position(body);
                s.rotation = w.rotation(body);
                s.velocity = w.velocity(body);
                s.sleeping = !w.isActive(body);
                bodies.push_back(s);
            }
            m_server->setBodies(bodies);
        }
        pollBreaks();
        if (fogOfWar && m_visibility) {
            m_visibility->settings = visibilitySettings;
            std::vector<net::Visibility::Player> players;
            if (m_hasLocal) players.push_back({ 0, m_local.position, m_local.velocity });
            for (const auto& [slot, g] : m_localGuests)
                if (const uint8_t id = m_server->localGuestId(static_cast<uint8_t>(slot)); id && g.hasState)
                    players.push_back({ id, g.state.position, g.state.velocity });
            for (const net::RemotePlayer& p : m_server->players(t))
                if (p.hasState) players.push_back({ p.id, p.state.position, p.state.velocity });
            m_visibility->update(t, players);
        }
        m_server->update(t);
        runReplayedPlayers(ctx.dt);
        m_remote = m_server->players(t);
    } else if (m_client) {
        if (m_hasLocal) m_client->setLocalState(m_local);
        for (const auto& [slot, g] : m_localGuests)
            if (g.hasState) m_client->setGuestState(static_cast<uint8_t>(slot), g.state);
        const auto before = m_client->status();
        m_client->update(t);
        m_remote = m_client->players(t);
        const auto status = m_client->status();
        if (status == net::NetClient::Status::Connected) {
            if (before != status) log::get(name())->info("Connected as player {}", m_client->playerId());
            m_status = "connected as player " + std::to_string(m_client->playerId());
        } else if (status == net::NetClient::Status::Rejected || status == net::NetClient::Status::Disconnected) {
            std::string why = m_client->statusText();
            if (m_secure && !m_secure->failure().empty()) why = m_secure->failure(); // the encryption said why
            if (before != status) {
                if (m_client->endedByServer()) log::get(name())->info("Left the game: {}", why); // the host ended it or kicked us: not a fault
                else log::get(name())->warn("Left the game: {}", why);
            }
            leave();
            m_status = why.empty() ? "disconnected" : why;
        }
    }
    if (m_search) {
        std::vector<net::NetEvent> ignored; // the search socket only gets answers (intercepted)
        m_search->poll(ignored);
        if (t > m_searchUntil + 5.0) m_search.reset();
    }
    // Panel bandwidth, once a second.
    if (t - m_statTime >= 1.0) {
        uint64_t sent = 0, received = 0;
        if (m_client) { const auto s = m_client->stats(); sent = s.bytesSent; received = s.bytesReceived; }
        if (m_server) for (const auto& p : m_remote) { const auto s = m_server->stats(p.id); sent += s.bytesSent; received += s.bytesReceived; }
        const double span = m_statTime > 0.0 ? t - m_statTime : 1.0;
        m_upKbps = sent >= m_lastSent ? static_cast<float>(static_cast<double>(sent - m_lastSent) * 8.0 / 1000.0 / span) : 0.0f;
        m_downKbps = received >= m_lastReceived ? static_cast<float>(static_cast<double>(received - m_lastReceived) * 8.0 / 1000.0 / span) : 0.0f;
        m_lastSent = sent;
        m_lastReceived = received;
        m_statTime = t;
    }
}

void NetModule::searchLan() {
    if (m_role != Role::Offline) return;
    m_search = std::make_unique<net::EnetTransport>();
    m_search->discover(kDefaultPort, static_cast<uint16_t>(kDefaultPort + kPortRange - 1));
    m_searchUntil = now() + 1.0;
}

bool NetModule::searchingLan() const { return m_search && now() < m_searchUntil; }

std::vector<NetModule::LanGame> NetModule::lanGames() const {
    std::vector<LanGame> out;
    if (!m_search) return out;
    for (const auto& g : m_search->lanGames()) {
        // info = "name|players/max|gameId"
        LanGame l;
        l.address = g.address;
        l.port = g.port;
        l.hostName = g.info;
        std::string game;
        if (size_t a = g.info.find('|'); a != std::string::npos) {
            l.hostName = g.info.substr(0, a);
            const size_t b = g.info.find('|', a + 1);
            l.players = g.info.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if (b != std::string::npos) game = g.info.substr(b + 1);
        }
        l.ours = game == m_config.gameId;
        out.push_back(std::move(l));
    }
    std::stable_partition(out.begin(), out.end(), [](const LanGame& g) { return g.ours; });
    return out;
}

void NetModule::lanSearchUi() {
    if (ImGui::Button("Search LAN")) searchLan();
    if (!m_search) return;
    ImGui::SameLine();
    const std::vector<LanGame> games = lanGames();
    ImGui::TextDisabled(searchingLan() ? "searching..." : "%zu found", games.size());
    for (const LanGame& g : games) {
        const std::string& hostName = g.hostName;
        const std::string& players = g.players;
        ImGui::PushID(static_cast<int>(g.port) ^ static_cast<int>(std::hash<std::string>{}(g.address)));
        const bool ours = g.ours;
        ImGui::BeginDisabled(!ours);
        if (ImGui::SmallButton("Join")) {
            const std::string address = g.address;
            const uint16_t port = g.port;
            join(address, port);
            ImGui::EndDisabled();
            ImGui::PopID();
            return; // m_search is gone
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s  %s  %s:%u%s", hostName.c_str(), players.c_str(), g.address.c_str(), g.port, ours ? "" : "  (another game)");
        ImGui::PopID();
    }
}

const std::vector<server::DirectoryEntry>& NetModule::directoryServers() const {
    static const std::vector<server::DirectoryEntry> none;
    return m_browser ? m_browser->servers() : none;
}

bool NetModule::browseDirectory(const std::string& directory, std::string* error) {
    std::string host;
    uint16_t port = 0;
    if (!server::splitHostPort(directory, host, port)) {
        m_browseStatus = "'" + directory + "' isn't host:port";
        if (error) *error = m_browseStatus;
        return false;
    }
    if (!m_browser) m_browser = std::make_unique<server::DirectoryBrowser>();
    std::string e;
    if (!m_browser->query(host, port, m_config.gameId, &e)) {
        m_browseStatus = e;
        if (error) *error = e;
        return false;
    }
    m_browseStatus = "asking " + directory + "...";
    m_browseUntil = now() + 3.0;
    return true;
}

void NetModule::directoryUi() {
    ImGui::TextUnformatted("Internet servers");
    ImGui::InputTextWithHint("##directory", "directory host:port", m_directoryInput, sizeof(m_directoryInput));
    ImGui::SameLine();
    ImGui::BeginDisabled(m_directoryInput[0] == 0);
    if (ImGui::Button("Refresh")) browseDirectory(m_directoryInput);
    ImGui::EndDisabled();
    if (!m_browseStatus.empty()) ImGui::TextDisabled("%s", m_browseStatus.c_str());
    else if (m_directoryInput[0] == 0) ImGui::TextDisabled("No server list set: type a directory's address (docs/SERVER_HOSTING.md)");
    if (!m_browser) return;
    for (const server::DirectoryEntry& e : m_browser->servers()) {
        ImGui::PushID(static_cast<int>(e.port) ^ static_cast<int>(std::hash<std::string>{}(e.address)));
        const bool sameVersion = e.protocol == net::kProtocolVersion;
        const bool full = e.maxPlayers > 0 && e.players >= e.maxPlayers;
        ImGui::BeginDisabled(!sameVersion || full);
        const bool clicked = ImGui::SmallButton("Join");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s  %u/%u%s  %s:%u", e.name.c_str(), e.players, e.maxPlayers, e.password ? "  (password)" : "", e.address.c_str(), e.port);
        if (!sameVersion) {
            ImGui::SameLine();
            ImGui::TextDisabled(e.protocol > net::kProtocolVersion ? "(newer version)" : "(older version)");
        } else if (full) {
            ImGui::SameLine();
            ImGui::TextDisabled("(full)");
        }
        ImGui::PopID();
        if (clicked) {
            if (e.password && m_config.password.empty()) {
                m_browseStatus = "'" + e.name + "' needs a password: type it above, then Join";
                break;
            }
            const std::string address = e.address;
            const uint16_t port = e.port;
            join(address, port);
            break;
        }
    }
}

void NetModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(320 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network")) { ImGui::End(); return; }
    ImGui::TextWrapped("%s", m_status.c_str());
    if (const std::string code = joinCode(); !code.empty()) {
        ImGui::Text("Join code: %s", code.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
    }
    if (m_role == Role::Client && !m_serverMessage.empty()) ImGui::TextWrapped("Server: %s", m_serverMessage.c_str());
    if (m_role == Role::Offline) {
        if (ImGui::InputText("Name", m_nameInput, sizeof(m_nameInput))) playerName = m_nameInput;
        if (ImGui::InputText("Password", m_passwordInput, sizeof(m_passwordInput), ImGuiInputTextFlags_Password)) m_config.password = m_passwordInput;
        if (ImGui::Button("Host")) host(0);
        ImGui::SameLine();
        ImGui::Checkbox("Input replay (competitive)", &inputReplay);
        ImGui::Separator();
        ImGui::InputText("Address or code", m_addressInput, sizeof(m_addressInput));
        ImGui::InputInt("Port", &m_portInput);
        m_portInput = std::clamp(m_portInput, 1, 65535);
        if (ImGui::Button("Join")) join(m_addressInput, static_cast<uint16_t>(m_portInput));
        ImGui::Separator();
        lanSearchUi();
        ImGui::Separator();
        directoryUi();
    } else {
        if (ImGui::Button(m_role == Role::Host ? "Stop hosting" : "Leave")) leave();
        ImGui::Text("Up %.1f kbit/s, down %.1f kbit/s", m_upKbps, m_downKbps);
        for (const auto& [slot, g] : m_localGuests) {
            const uint8_t id = localPlayerId(slot);
            if (id) ImGui::Text("On this screen: %u %s", id, g.name.c_str());
            else ImGui::TextDisabled("On this screen: %s (not in the game)", g.name.c_str());
        }
        if (m_client) {
            const auto st = m_client->stats();
            ImGui::Text("Host: RTT %.0f ms, loss %.1f%%", st.rttMs, st.lossPercent);
            if (m_client->inputReplay()) ImGui::Text("Input replay: %zu corrections", predictionCorrections());
        }
        if (ImGui::BeginTable("peers", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("RTT");
            ImGui::TableSetupColumn("Loss");
            ImGui::TableHeadersRow();
            for (const auto& p : m_remote) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u %s", p.id, p.name.c_str());
                if (m_server) {
                    const auto st = m_server->stats(p.id);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f ms", st.rttMs);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f%%", st.lossPercent);
                } else {
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("-");
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("-");
                }
            }
            ImGui::EndTable();
        }
        if (m_server) {
            ImGui::Text("Corrections sent: %zu, bad packets: %zu", m_server->corrections(), m_server->badPackets());
            if (m_hostingReplay) ImGui::Text("Input replay: %zu players moved here", m_replayed.size());
            ImGui::Checkbox("Check moves (walls, flying)", &checkMoves);
            if (m_moveCheck) ImGui::Text("Refused: %zu through walls, %zu flying", m_moveCheck->throughWalls, m_moveCheck->flying);
            ImGui::Text("Spawned objects: %zu, breakables: %zu", m_spawned.size(), m_breakables.size());
        }
    }
    if (ImGui::CollapsingHeader("Simulate a bad connection")) {
        ImGui::SliderFloat("Lag (ms, one way)", &simulated.latencyMs, 0.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("Jitter (ms)", &simulated.jitterMs, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("Loss (%)", &simulated.lossPercent, 0.0f, 50.0f, "%.0f");
    }
    ImGui::End();
}

} // namespace kke
