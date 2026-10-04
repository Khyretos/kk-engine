// Sailing online (README.md "Online"): a simple shared sea, no match.
//
//   Every ship a person sails is a network player (kke::NetModule; a second
//   captain at the same screen is a local player in slot 1..). On the host
//   the enemy ships are local players too (the engine's "a host's bots"),
//   so everyone sees the same enemies; only the host steers them.
//   NetPlayerState carries where a ship is and how fast it goes; `extra`
//   carries its attitude (a quaternion), class, team, health, sails and
//   which masts still stand. `character` says what it is ("ship").
//   Shots: the screen that fires sends every ball (Shot) so the others see
//   it fly; only the shooter's screen decides what it hits. A hit on
//   another screen's ship goes to that screen (Hit), which applies the
//   damage; everyone gets the dent and the splinters. The host passes
//   clients' events on.
//
// Host or join from the start menu's Online row (or KKE_NET=host,
// KKE_NET=join:ADDRESS); the typed "Address or code" row works over a VPN
// where the LAN search finds nothing.

#include "SeaDemoModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kke_sea {

namespace {

constexpr uint16_t kEventBase = 0x5E00;           // the sea demo's own event kinds
constexpr uint16_t kEventShot = kEventBase + 0;   // anyone -> all: a ball in flight (drawn only)
constexpr uint16_t kEventHit = kEventBase + 1;    // anyone -> all: a ship was hit (its owner applies the damage)
constexpr int kModeOff = 0, kModeJoin = 1, kModeHost = 2;

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void f32(float v) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        bytes.insert(bytes.end(), b, b + 4);
    }
    void vec(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void i16(float unit) { // -1..1
        const int16_t q = static_cast<int16_t>(std::lround(std::clamp(unit, -1.0f, 1.0f) * 32767.0f));
        bytes.push_back(static_cast<uint8_t>(q & 0xFF));
        bytes.push_back(static_cast<uint8_t>((q >> 8) & 0xFF));
    }
};

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at + 1 > bytes.size()) {
            ok = false;
            return 0;
        }
        return bytes[at++];
    }
    float f32() {
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0.0f;
        }
        float v;
        std::memcpy(&v, bytes.data() + at, 4);
        at += 4;
        return std::isfinite(v) ? v : (ok = false, 0.0f);
    }
    glm::vec3 vec() {
        const float x = f32(), y = f32(), z = f32();
        return { x, y, z };
    }
    float i16() {
        if (at + 2 > bytes.size()) {
            ok = false;
            return 0.0f;
        }
        const int16_t q = static_cast<int16_t>(bytes[at] | (bytes[at + 1] << 8));
        at += 2;
        return static_cast<float>(q) / 32767.0f;
    }
};

} // namespace

void SeaDemoModule::setupNet() {
    if (!m_net) return;
    m_net->standIns = false;   // ships, not walking characters
    m_net->checkMoves = false; // nothing on the sea to walk through
    kke::net::MovementLimits limits;
    limits.horizontalSpeed = 30.0f; // a rowing boat surfing a wave stays well under this
    limits.riseSpeed = 30.0f;
    limits.fallSpeed = 40.0f;       // a sinking ship
    limits.slack = 4.0f;
    limits.teleportCooldown = 1.0; // a new ship after sinking
    m_net->movementLimits = limits;
    m_net->addEventListener([this](const kke::net::GameEventMsg& e) { onNetEvent(e); });
    // The character goes out once, when hosting or joining starts (KKE_NET
    // does that in the first frame): set it before then, or the others
    // never learn that this player is a ship. The name stays NetModule's
    // ("Player", or KKE_NET_NAME).
    m_net->playerCharacter = "ship:p";
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::Option mode{ "net.mode", "Online", { "Off", "Join", "Host" }, kModeOff, true, {}, {} };
    mode.onChange = [this](int value) {
        std::string error;
        m_net->leave();
        if (value == kModeHost) {
            if (!m_net->host(0, &error)) {
                m_lobby->lobby().toast("Can't host: " + error, 5.0f);
                if (kke::Lobby::Option* o = m_lobby->lobby().option("net.mode")) o->value = kModeOff;
            } else {
                m_lobby->lobby().toast("Hosting: friends join with your address (or the join code)", 5.0f);
            }
        } else if (value == kModeJoin) {
            m_net->searchLan();
        }
        if (kke::Lobby::Option* typed = m_lobby->lobby().option("net.address")) typed->visible = value == kModeJoin;
        if (kke::Lobby::Option* lan = m_lobby->lobby().option("net.lan")) lan->visible = value == kModeJoin;
    };
    l.addOption(std::move(mode));
    kke::Lobby::Option lan{ "net.lan", "Join a game on this network", {}, 0, false, {}, {} };
    lan.onPress = [this]() {
        std::vector<kke::NetModule::LanGame> games = m_net->lanGames();
        std::erase_if(games, [](const kke::NetModule::LanGame& g) { return !g.ours; });
        if (games.empty()) {
            m_lobby->lobby().toast("No sea game found on this network yet (still looking). Or type the host's address.", 5.0f);
            m_net->searchLan();
            return;
        }
        std::string error;
        if (!m_net->join(games.front().address, games.front().port, &error)) m_lobby->lobby().toast("Can't join: " + error, 5.0f);
    };
    l.addOption(std::move(lan));
    l.addTextOption("net.address", "Address or code", "type the host's IP or join code", [this](const std::string& typed) {
        if (typed.empty()) return;
        m_lobby->save();
        std::string error;
        if (!m_net->joinTyped(typed, kke::NetModule::kDefaultPort, &error)) m_lobby->lobby().toast("Can't join: " + error, 5.0f);
    });
    if (kke::Lobby::Option* typed = l.option("net.address")) typed->visible = false;
    if (kke::Lobby::Option* o = l.option("net.lan")) o->visible = false;
}

// This screen's ships as network players: captains first, then (host)
// the enemies.
void SeaDemoModule::sendNet() {
    if (!m_net || !m_net->connected()) {
        for (Ship& s : m_ships) s.netSlot = -1;
        return;
    }
    int slot = 0;
    auto send = [&](Ship& s, const std::string& who) {
        if (slot >= kke::NetModule::kMaxLocalPlayers) return;
        const char team = s.team == kPlayers ? 'p' : 'e';
        const std::string character = std::string("ship:") + team;
        // Slot 0 is NetModule's own player (its character is set in
        // setupNet); the others are local players: split-screen captains
        // and the host's enemy ships.
        if (slot > 0 && s.netSlot != slot) {
            m_net->addLocalPlayer(slot, who, character);
        }
        s.netSlot = slot;
        const kke::FloatingBody& b = m_bodies.bodies()[s.body];
        kke::net::NetPlayerState st;
        st.position = b.position;
        st.velocity = b.velocity;
        const glm::vec3 fwd = glm::mat3_cast(b.orientation) * glm::vec3(0, 0, 1);
        st.yaw = glm::degrees(std::atan2(fwd.x, fwd.z));
        st.flags = s.sinking ? 1u : 0u;
        st.speed = std::clamp(glm::length(b.velocity), 0.0f, 20.0f);
        Writer w;
        const glm::quat q = glm::normalize(b.orientation);
        w.i16(q.w);
        w.i16(q.x);
        w.i16(q.y);
        w.i16(q.z);
        w.u8(static_cast<uint8_t>(s.cls));
        w.u8(static_cast<uint8_t>(s.team));
        w.u8(static_cast<uint8_t>(std::lround(std::clamp(s.health / s.maxHealth, 0.0f, 1.0f) * 255.0f)));
        w.u8(static_cast<uint8_t>(std::lround(std::clamp(s.sailShown, 0.0f, 1.0f) * 255.0f)));
        uint8_t masts = 0;
        for (size_t m = 0; m < s.mastUp.size() && m < 8; ++m)
            if (s.mastUp[m]) masts |= static_cast<uint8_t>(1u << m);
        w.u8(masts);
        st.extra = std::move(w.bytes);
        m_net->setLocalPlayer(slot, st);
        ++slot;
    };
    for (Captain& c : m_captains)
        if (c.ship >= 0 && m_ships[static_cast<size_t>(c.ship)].alive) send(m_ships[static_cast<size_t>(c.ship)], c.name);
    if (m_net->authority())
        for (int e : m_enemyShips)
            if (e >= 0 && m_ships[static_cast<size_t>(e)].alive) send(m_ships[static_cast<size_t>(e)], m_ships[static_cast<size_t>(e)].name);
    for (int s = std::max(1, slot); s < kke::NetModule::kMaxLocalPlayers; ++s) m_net->removeLocalPlayer(s);
}

void SeaDemoModule::receiveNet() {
    if (!m_net || !m_net->connected()) {
        // Offline again: everyone else's ships go.
        for (size_t i = 0; i < m_ships.size(); ++i)
            if (m_ships[i].alive && m_ships[i].remote) removeShip(i);
        return;
    }
    // A client's own enemies make way for the host's.
    if (!m_net->authority())
        for (int& e : m_enemyShips) {
            if (e >= 0) removeShip(static_cast<size_t>(e));
            e = -1;
        }
    const auto& players = m_net->remotePlayers();
    for (size_t i = 0; i < m_ships.size(); ++i) {
        Ship& s = m_ships[i];
        if (!s.alive || !s.remote) continue;
        const bool still = std::any_of(players.begin(), players.end(), [&](const kke::net::RemotePlayer& p) { return p.id == s.netId && p.hasState; });
        if (!still) removeShip(i);
    }
    for (const kke::net::RemotePlayer& p : players) {
        if (!p.hasState || p.character.rfind("ship:", 0) != 0) continue;
        Reader r{ p.state.extra };
        glm::quat q;
        q.w = r.i16();
        q.x = r.i16();
        q.y = r.i16();
        q.z = r.i16();
        const int cls = std::clamp(static_cast<int>(r.u8()), 0, static_cast<int>(shipClasses().size()) - 1);
        const int team = r.u8() == 0 ? kPlayers : kEnemies;
        const float health = static_cast<float>(r.u8()) / 255.0f;
        const float sail = static_cast<float>(r.u8()) / 255.0f;
        const uint8_t masts = r.u8();
        if (!r.ok || glm::length(q) < 0.5f) continue;
        q = glm::normalize(q);
        int index = -1;
        for (size_t i = 0; i < m_ships.size(); ++i)
            if (m_ships[i].alive && m_ships[i].remote && m_ships[i].netId == p.id) index = static_cast<int>(i);
        if (index >= 0 && m_ships[static_cast<size_t>(index)].cls != cls) { // a new ship
            removeShip(static_cast<size_t>(index));
            index = -1;
        }
        if (index < 0) {
            index = spawnShip(cls, team, p.state.position, glm::radians(p.state.yaw), false);
            Ship& s = m_ships[static_cast<size_t>(index)];
            s.remote = true;
            s.netId = p.id;
            s.name = p.name;
            kke::log::get("SeaDemo")->info("{} sails in on a {}", p.name, shipClasses()[static_cast<size_t>(cls)].name);
        }
        Ship& s = m_ships[static_cast<size_t>(index)];
        s.team = team;
        s.health = health * s.maxHealth;
        s.sailShown = sail;
        s.sinking = (p.state.flags & 1u) != 0;
        for (size_t m = 0; m < s.mastUp.size() && m < 8; ++m) s.mastUp[m] = (masts & (1u << m)) != 0;
        // Its body follows what its owner sends (so our ships bump into it).
        kke::FloatingBody& b = m_bodies.bodies()[s.body];
        b.position = p.state.position;
        b.orientation = q;
        b.velocity = p.state.velocity;
        b.angularVelocity = glm::vec3(0.0f);
        s.drawPos = b.position;
        s.drawRot = q;
        if (s.body < m_prevPos.size()) {
            m_prevPos[s.body] = b.position;
            m_prevRot[s.body] = q;
        }
    }
}

void SeaDemoModule::onNetEvent(const kke::net::GameEventMsg& e) {
    if (e.kind != kEventShot && e.kind != kEventHit) return;
    if (m_net->role() == kke::NetModule::Role::Host) m_net->relayEvent(e); // everyone else hears it too
    Reader r{ e.payload };
    if (e.kind == kEventShot) {
        Ball b;
        b.pos = r.vec();
        b.vel = r.vec();
        b.mass = std::clamp(r.f32(), 0.5f, 20.0f);
        if (!r.ok || glm::length(b.vel) > 400.0f) return;
        b.live = false;
        b.life = 9.0f;
        b.team = -1;
        m_balls.push_back(b);
        muzzle(b.pos, glm::normalize(b.vel), glm::vec3(0.0f));
        return;
    }
    const uint8_t victim = r.u8();
    const glm::vec3 shipPoint = r.vec();
    glm::vec3 shipDir = r.vec();
    const float amount = std::clamp(r.f32(), 0.0f, 500.0f);
    if (!r.ok || glm::length(shipDir) < 1e-4f) return;
    shipDir = glm::normalize(shipDir);
    for (size_t i = 0; i < m_ships.size(); ++i) {
        Ship& s = m_ships[i];
        if (!s.alive) continue;
        const bool ours = !s.remote && s.netSlot >= 0 && m_net->localPlayerId(s.netSlot) == victim;
        const bool theirs = s.remote && s.netId == victim;
        if (!ours && !theirs) continue;
        const ShipArt& art = m_library.art(s.cls);
        const glm::mat3 R = glm::mat3_cast(s.remote ? s.drawRot : m_bodies.bodies()[s.body].orientation);
        const glm::vec3 centre = s.remote ? s.drawPos : m_bodies.bodies()[s.body].position;
        const glm::vec3 world = centre + R * (shipPoint - art.hullCenter);
        if (ours) damageShip(i, world, R * shipDir, amount, true);
        else {
            dent(s, shipPoint, shipDir, std::clamp(amount / 90.0f, 0.15f, 0.6f));
            splinters(world, R * shipDir, 4);
        }
        return;
    }
}

void SeaDemoModule::sendShot(const glm::vec3& pos, const glm::vec3& vel, float mass) {
    if (!m_net || !m_net->connected()) return;
    Writer w;
    w.vec(pos);
    w.vec(vel);
    w.f32(mass);
    m_net->sendEvent(kEventShot, w.bytes);
}

void SeaDemoModule::sendHit(int victimNetId, const glm::vec3& shipPoint, const glm::vec3& shipDir, float amount) {
    if (!m_net || !m_net->connected() || victimNetId < 0) return;
    Writer w;
    w.u8(static_cast<uint8_t>(victimNetId));
    w.vec(shipPoint);
    w.vec(shipDir);
    w.f32(amount);
    m_net->sendEvent(kEventHit, w.bytes);
}

} // namespace kke_sea
