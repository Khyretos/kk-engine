#include "kke/modules/NetModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/net/EnetTransport.h"

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
    return { { typeid(RigidBodyModule), false, "replicated bodies and remote players' capsules live in its world" } };
}

double NetModule::now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void NetModule::init(Application& app) {
    m_app = &app;
    m_rigid = app.getModule<RigidBodyModule>();
    if (const char* n = std::getenv("KKE_NET_NAME"); n && *n) playerName = n;
    simulated.latencyMs = envFloat("KKE_NET_LAG", 0.0f);
    simulated.jitterMs = envFloat("KKE_NET_JITTER", 0.0f);
    simulated.lossPercent = envFloat("KKE_NET_LOSS", 0.0f);
    std::snprintf(m_nameInput, sizeof(m_nameInput), "%s", playerName.c_str());

    const char* mode = std::getenv("KKE_NET");
    if (!mode || !*mode) return;
    const std::string m = mode;
    std::string error;
    if (m == "host" || m.rfind("host:", 0) == 0) {
        uint16_t port = 0;
        if (m.size() > 5) port = static_cast<uint16_t>(std::atoi(m.c_str() + 5));
        if (!host(port, &error)) log::get(name())->error("KKE_NET={}: {}", m, error);
    } else if (m.rfind("join:", 0) == 0) {
        std::string address;
        uint16_t port = kDefaultPort;
        splitAddress(m.substr(5), address, port);
        if (!join(address, port, &error)) log::get(name())->error("KKE_NET={}: {}", m, error);
    } else {
        log::get(name())->error("KKE_NET='{}': expected host, host:PORT or join:ADDRESS[:PORT]", m);
    }
}

void NetModule::shutdown() { leave(); }

std::string NetModule::discoveryInfo() const {
    const size_t players = 1 + (m_server ? m_server->clientCount() : 0);
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
        auto server = std::make_unique<net::NetServer>(*transport, m_config);
        if (!server->start(static_cast<uint16_t>(p), playerName, playerCharacter, &lastError)) continue;
        m_transport = std::move(transport);
        m_enet = raw;
        m_server = std::move(server);
        m_server->onEvent = [this](const net::GameEventMsg& e) { if (onEvent) onEvent(e); };
        m_server->onPlayer = [this](uint8_t id, bool joined) {
            log::get(name())->info("Player {} {} ({} connected)", id, joined ? "joined" : "left", m_server->clientCount());
            if (m_enet) m_enet->setDiscoveryInfo(discoveryInfo());
            if (onPlayer) onPlayer(id, joined);
        };
        m_enet->setDiscoveryInfo(discoveryInfo());
        m_role = Role::Host;
        m_status = "hosting on port " + std::to_string(p);
        m_search.reset();
        log::get(name())->info("Hosting '{}' on UDP port {} ({})", playerName, p, m_transport->backendName());
        return true;
    }
    if (error) *error = lastError;
    m_status = "can't host: " + lastError;
    return false;
}

bool NetModule::join(const std::string& address, uint16_t port, std::string* error) {
    leave();
    auto enet = std::make_unique<net::EnetTransport>();
    net::EnetTransport* raw = enet.get();
    auto transport = std::make_unique<net::ConditionedTransport>(std::move(enet));
    transport->conditions = simulated;
    auto client = std::make_unique<net::NetClient>(*transport, m_config);
    std::string err;
    if (!client->connect(address, port, playerName, playerCharacter, &err)) {
        if (error) *error = err;
        m_status = "can't join: " + err;
        return false;
    }
    m_transport = std::move(transport);
    m_enet = raw;
    m_client = std::move(client);
    m_client->onEvent = [this](const net::GameEventMsg& e) { if (onEvent) onEvent(e); };
    m_client->onCorrection = [this](const glm::vec3& p) { if (onCorrection) onCorrection(p); };
    m_client->onPlayer = [this](uint8_t id, bool joined) { if (onPlayer) onPlayer(id, joined); };
    m_role = Role::Client;
    m_status = "joining " + address + ":" + std::to_string(port);
    m_search.reset();
    log::get(name())->info("Joining {}:{} as '{}'", address, port, playerName);
    return true;
}

void NetModule::leave() {
    if (m_role == Role::Offline && !m_transport) return;
    if (m_client) m_client->disconnect();
    if (m_server) m_server->stop();
    // Flush the goodbye before the socket closes.
    if (m_transport) {
        std::vector<net::NetEvent> ignored;
        m_transport->poll(ignored);
        m_transport->close();
    }
    m_client.reset();
    m_server.reset();
    m_transport.reset();
    m_enet = nullptr;
    m_role = Role::Offline;
    m_status = "offline";
    m_remote.clear();
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

void NetModule::setLocalPlayer(const net::NetPlayerState& state) {
    m_local = state;
    m_hasLocal = true;
}

uint16_t NetModule::replicateBody(RigidWorld::BodyId body) {
    const auto id = static_cast<uint16_t>(m_bodies.size());
    m_bodies.push_back(body);
    return id;
}

void NetModule::clearBodies() {
    m_bodies.clear();
    if (m_server) {
        m_server->setBodies({});
        m_server->sendEvent(net::kEventBodiesReset, {});
    }
}

void NetModule::sendEvent(uint16_t kind, const std::vector<uint8_t>& payload) {
    if (m_server) m_server->sendEvent(kind, payload);
    else if (m_client && connected()) m_client->sendEvent(kind, payload);
}

void NetModule::relayEvent(const net::GameEventMsg& e) {
    if (m_server) m_server->relayEvent(e);
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
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        if (!m_client->body(static_cast<uint16_t>(i), t, s)) continue;
        const RigidWorld::BodyId b = m_bodies[i];
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
    for (auto it = m_capsules.begin(); it != m_capsules.end();) {
        const bool present = std::any_of(m_remote.begin(), m_remote.end(), [&](const net::RemotePlayer& p) { return p.id == it->first && p.hasState; });
        if (present) { ++it; continue; }
        w.remove(it->second);
        it = m_capsules.erase(it);
    }
    for (const net::RemotePlayer& p : m_remote) {
        if (!p.hasState) continue;
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

void NetModule::update(const UpdateContext&) {
    const double t = now();
    if (m_transport) m_transport->conditions = simulated;
    if (m_server) {
        if (m_hasLocal) m_server->setLocalState(m_local);
        if (m_rigid) {
            RigidWorld& w = m_rigid->world();
            std::vector<net::NetBodyState> bodies;
            bodies.reserve(m_bodies.size());
            for (size_t i = 0; i < m_bodies.size(); ++i) {
                net::NetBodyState s;
                s.id = static_cast<uint16_t>(i);
                s.position = w.position(m_bodies[i]);
                s.rotation = w.rotation(m_bodies[i]);
                s.velocity = w.velocity(m_bodies[i]);
                s.sleeping = !w.isActive(m_bodies[i]);
                bodies.push_back(s);
            }
            m_server->setBodies(bodies);
        }
        m_server->update(t);
        m_remote = m_server->players(t);
    } else if (m_client) {
        if (m_hasLocal) m_client->setLocalState(m_local);
        const auto before = m_client->status();
        m_client->update(t);
        m_remote = m_client->players(t);
        const auto status = m_client->status();
        if (status == net::NetClient::Status::Connected) {
            if (before != status) log::get(name())->info("Connected as player {}", m_client->playerId());
            m_status = "connected as player " + std::to_string(m_client->playerId());
        } else if (status == net::NetClient::Status::Rejected || status == net::NetClient::Status::Disconnected) {
            const std::string why = m_client->statusText();
            if (before != status) log::get(name())->warn("Left the game: {}", why);
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

void NetModule::lanSearchUi() {
    if (ImGui::Button("Search LAN")) {
        m_search = std::make_unique<net::EnetTransport>();
        m_search->discover(kDefaultPort, static_cast<uint16_t>(kDefaultPort + kPortRange - 1));
        m_searchUntil = now() + 1.0;
    }
    if (!m_search) return;
    ImGui::SameLine();
    ImGui::TextDisabled(now() < m_searchUntil ? "searching..." : "%zu found", m_search->lanGames().size());
    for (const auto& g : m_search->lanGames()) {
        // info = "name|players/max|gameId"
        std::string hostName = g.info, players, game;
        if (size_t a = g.info.find('|'); a != std::string::npos) {
            hostName = g.info.substr(0, a);
            const size_t b = g.info.find('|', a + 1);
            players = g.info.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if (b != std::string::npos) game = g.info.substr(b + 1);
        }
        ImGui::PushID(static_cast<int>(g.port) ^ static_cast<int>(std::hash<std::string>{}(g.address)));
        const bool ours = game == m_config.gameId;
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

void NetModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(320 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network")) { ImGui::End(); return; }
    ImGui::TextWrapped("%s", m_status.c_str());
    if (m_role == Role::Offline) {
        if (ImGui::InputText("Name", m_nameInput, sizeof(m_nameInput))) playerName = m_nameInput;
        if (ImGui::Button("Host")) host(0);
        ImGui::Separator();
        ImGui::InputText("Address", m_addressInput, sizeof(m_addressInput));
        ImGui::InputInt("Port", &m_portInput);
        m_portInput = std::clamp(m_portInput, 1, 65535);
        if (ImGui::Button("Join")) join(m_addressInput, static_cast<uint16_t>(m_portInput));
        ImGui::Separator();
        lanSearchUi();
    } else {
        if (ImGui::Button(m_role == Role::Host ? "Stop hosting" : "Leave")) leave();
        ImGui::Text("Up %.1f kbit/s, down %.1f kbit/s", m_upKbps, m_downKbps);
        if (m_client) {
            const auto st = m_client->stats();
            ImGui::Text("Host: RTT %.0f ms, loss %.1f%%", st.rttMs, st.lossPercent);
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
        if (m_server) ImGui::Text("Corrections sent: %zu, bad packets: %zu", m_server->corrections(), m_server->badPackets());
    }
    if (ImGui::CollapsingHeader("Simulate a bad connection")) {
        ImGui::SliderFloat("Lag (ms, one way)", &simulated.latencyMs, 0.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("Jitter (ms)", &simulated.jitterMs, 0.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("Loss (%)", &simulated.lossPercent, 0.0f, 50.0f, "%.0f");
    }
    ImGui::End();
}

} // namespace kke
