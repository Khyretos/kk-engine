// Spawning and carrying online (Kees's showcase list: "online for
// spawning and carrying"). The host's world is the real one:
//
// - Spawn menu: on the host, every prop a spawn menu row makes is a
//   NetModule spawned object (kSpawnProp): each client builds a copy that
//   follows the host's body, late joiners too, and clearing removes them
//   everywhere. A client's spawn menu asks the host (kEventSpawnRow, with
//   the spot in front of the client); the host checks the spot is near
//   that player and makes it there. Dummies (ragdolls) stay one per
//   machine: each is eleven bodies.
// - Carrying: a client lifts its own copy at once (no lag in its hands)
//   and tells the host (kEventCarry); the host carries the real body in
//   front of that player, from the player's state, so everyone sees it
//   carried. Putting it down and throwing (kEventThrow) end it on the host
//   with the client's velocities. Bodies that aren't replicated (a felled
//   tree's logs: every machine has its own forest) are carried locally.
//
// Offline every function here is a no-op.

#include "ShowcaseModule.h"
#include "NetEvents.h"

#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace kke_showcase {

namespace {
#if KKE_ENABLE_NET
using namespace netev;
constexpr float kSpawnReach = 8.0f; // m: a client's spawn spot must be this near the client
constexpr float kCarryReach = 3.0f; // m: and what it picks up this near
constexpr uint8_t kFlagCrouch = 0x01; // NetPlayerState::flags, as in ShowcaseModule.cpp
glm::vec3 facingOf(float yawDegrees) {
    const float r = glm::radians(yawDegrees);
    return glm::vec3(std::sin(r), 0.0f, -std::cos(r));
}
#endif
} // namespace

bool ShowcaseModule::onlineClient() const {
#if KKE_ENABLE_NET
    return m_net && m_net->connected() && !m_net->authority();
#else
    return false;
#endif
}

bool ShowcaseModule::onlineHost() const {
#if KKE_ENABLE_NET
    return m_net && m_net->role() == kke::NetModule::Role::Host;
#else
    return false;
#endif
}

bool ShowcaseModule::replicating() const { return m_sharing && onlineHost(); }

void ShowcaseModule::setupOnline() {
#if KKE_ENABLE_NET
    if (!m_net) return;
    // A client: the host made a prop. Build our copy and tie it to theirs.
    m_net->addSpawnListener([this](const kke::net::SpawnMsg& m) {
        if (m.kind != kSpawnProp) return;
        PropDesc d;
        if (!unpack(m.desc, d)) return;
        const kke::RigidWorld::BodyId last = m_props.empty() ? kke::RigidWorld::kNoBody : m_props.back().body;
        spawnProp(static_cast<PropShape>(d.shape), d.half, d.density, d.color, d.material, d.at, d.metallic, d.restitution);
        if (m_props.empty() || m_props.back().body == last) return; // the world was full
        Prop& p = m_props.back();
        p.netId = m.id;
        m_rigid->world().setTransform(p.body, d.at, d.rotation);
        m_net->bindSpawnedBody(m.id, p.body);
    });
    m_net->addDespawnListener([this](uint16_t id) {
        for (size_t k = 0; k < m_props.size(); ++k) {
            if (m_props[k].netId != id) continue;
            if (m_held.body == m_props[k].body) m_held = Held{}; // it's gone: nothing to put down
            m_rigid->world().remove(m_props[k].body);
            m_props.erase(m_props.begin() + static_cast<std::ptrdiff_t>(k));
            return;
        }
    });
#endif
}

// Host: this prop on every machine (it was just made by spawnRowAt).
void ShowcaseModule::shareProp(Prop& p, const glm::quat& rotation, float density, uint32_t material, float restitution) {
#if KKE_ENABLE_NET
    PropDesc d;
    d.shape = static_cast<uint8_t>(p.shape);
    d.half = p.half;
    d.color = p.color;
    d.at = m_rigid->world().position(p.body);
    d.density = density;
    d.metallic = p.metallic;
    d.restitution = restitution;
    d.material = static_cast<uint8_t>(material);
    d.rotation = rotation;
    p.netId = m_net->spawn(kSpawnProp, pack(d));
    if (p.netId) m_net->bindSpawnedBody(p.netId, p.body);
#else
    (void)p;
    (void)rotation;
    (void)density;
    (void)material;
    (void)restitution;
#endif
}

void ShowcaseModule::forgetProp(const Prop& p) {
#if KKE_ENABLE_NET
    if (!p.netId || !onlineHost()) return;
    std::erase_if(m_remoteCarries, [&](const RemoteCarry& c) { return c.body == p.body; });
    m_net->despawn(p.netId);
#else
    (void)p;
#endif
}

// Network ids: 1.. the level's crates (0 is the platform), then spawned props.
uint16_t ShowcaseModule::netIdOf(kke::RigidWorld::BodyId body) const {
    for (size_t k = 0; k < m_crates.size(); ++k)
        if (m_crates[k].body == body) return static_cast<uint16_t>(k + 1);
    for (const Prop& p : m_props)
        if (p.body == body) return p.netId;
    return 0;
}

kke::RigidWorld::BodyId ShowcaseModule::bodyOfNet(uint16_t id) const {
    if (id == 0) return kke::RigidWorld::kNoBody;
    if (id <= m_crates.size()) return m_crates[id - 1].body;
    for (const Prop& p : m_props)
        if (p.netId == id) return p.body;
    return kke::RigidWorld::kNoBody;
}

void ShowcaseModule::askHostToSpawn(int row, const glm::vec3& at, const glm::vec3& base) {
#if KKE_ENABLE_NET
    if (!onlineClient()) return;
    m_net->sendEvent(kEventSpawnRow, pack(SpawnRowEvent{ static_cast<uint8_t>(row), at, base }));
#else
    (void)row;
    (void)at;
    (void)base;
#endif
}

void ShowcaseModule::tellCarry(bool carrying, const glm::vec3& velocity) {
#if KKE_ENABLE_NET
    if (!onlineClient()) return;
    if (const uint16_t id = netIdOf(m_held.body)) m_net->sendEvent(kEventCarry, pack(CarryEvent{ id, carrying, m_held.yawOffset, velocity }));
#else
    (void)carrying;
    (void)velocity;
#endif
}

void ShowcaseModule::tellThrow(const glm::vec3& velocity, const glm::vec3& spin) {
#if KKE_ENABLE_NET
    if (!onlineClient()) return;
    if (const uint16_t id = netIdOf(m_held.body)) m_net->sendEvent(kEventThrow, pack(ThrowEvent{ id, velocity, spin }));
#else
    (void)velocity;
    (void)spin;
#endif
}

// Host: a client's spawn, carry or throw. False: not one of ours.
bool ShowcaseModule::onOnlineEvent(uint16_t kind, uint8_t from, const std::vector<uint8_t>& payload) {
#if KKE_ENABLE_NET
    if (kind != kEventSpawnRow && kind != kEventCarry && kind != kEventThrow) return false;
    if (!onlineHost()) return true;
    const kke::net::RemotePlayer* who = nullptr;
    for (const kke::net::RemotePlayer& r : m_net->remotePlayers())
        if (r.id == from && r.hasState) who = &r;
    if (!who) return true;
    const glm::vec3 feet = who->state.position;
    auto log = kke::log::get(name());
    kke::RigidWorld& w = m_rigid->world();
    if (kind == kEventSpawnRow) {
        SpawnRowEvent e;
        if (!unpack(payload, e)) return true;
        if (e.row == kRowClear) {
            clearSpawned(); // for everyone
            log->info("online: player {} cleared the spawned things", from);
            return true;
        }
        // The menu's rows that make things; a spot near that player.
        if (e.row >= kRowClear || glm::length(e.at - feet) > kSpawnReach || glm::length(e.base - feet) > kSpawnReach) return true;
        m_sharing = true;
        spawnRowAt(e.row, e.at, e.base);
        m_sharing = false;
        log->info("online: player {} spawned row {} at {:.1f} {:.1f} {:.1f}", from, e.row, e.at.x, e.at.y, e.at.z);
        return true;
    }
    if (kind == kEventCarry) {
        CarryEvent e;
        if (!unpack(payload, e)) return true;
        const kke::RigidWorld::BodyId body = bodyOfNet(e.body);
        std::erase_if(m_remoteCarries, [&](const RemoteCarry& c) { return c.player == from || c.body == body; });
        if (body == kke::RigidWorld::kNoBody) return true;
        if (!e.carrying) {
            w.setAngularVelocity(body, glm::vec3(0.0f));
            w.setVelocity(body, e.velocity);
            log->info("online: player {} put down body {}", from, e.body);
            return true;
        }
        if (body == m_held.body || glm::length(w.position(body) - feet) > kCarryReach) return true; // ours, or out of their reach
        std::vector<kke::RigidWorld::BodyBox> boxes;
        const glm::vec3 at = w.position(body);
        w.bodiesInBox(at - glm::vec3(0.05f), at + glm::vec3(0.05f), boxes);
        glm::vec3 half(0.3f);
        for (const kke::RigidWorld::BodyBox& b : boxes)
            if (b.id == body) half = b.halfExtents;
        m_remoteCarries.push_back({ from, body, half, e.yawOffset });
        log->info("online: player {} picked up body {}", from, e.body);
        return true;
    }
    ThrowEvent e;
    if (!unpack(payload, e)) return true;
    const kke::RigidWorld::BodyId body = bodyOfNet(e.body);
    std::erase_if(m_remoteCarries, [&](const RemoteCarry& c) { return c.player == from; });
    if (body == kke::RigidWorld::kNoBody || glm::length(w.position(body) - feet) > kCarryReach) return true;
    w.setVelocity(body, e.velocity);
    w.setAngularVelocity(body, e.spin);
    log->info("online: player {} threw body {}", from, e.body);
    return true;
#else
    (void)kind;
    (void)from;
    (void)payload;
    return false;
#endif
}

// Host, physics rate: what clients carry, in front of them.
void ShowcaseModule::stepRemoteCarries(float dt) {
#if KKE_ENABLE_NET
    if (m_remoteCarries.empty()) return;
    if (!onlineHost()) {
        m_remoteCarries.clear();
        return;
    }
    kke::RigidWorld& w = m_rigid->world();
    const float radius = w.characterRadius(m_player);
    std::erase_if(m_remoteCarries, [&](const RemoteCarry& c) {
        const kke::net::RemotePlayer* who = nullptr;
        for (const kke::net::RemotePlayer& r : m_net->remotePlayers())
            if (r.id == c.player && r.hasState) who = &r;
        if (!who) return true; // left the game: it drops
        const kke::net::NetPlayerState& s = who->state;
        const glm::vec3 f = facingOf(s.yaw);
        const glm::vec3 target = holdPointFor(s.position, f, radius, (s.flags & kFlagCrouch) != 0, c.half);
        if (glm::length(target - w.position(c.body)) > 2.5f) return true; // snagged: it drops (the client's copy too, soon)
        steerHeld(c.body, target, s.velocity, f, c.yawOffset, dt);
        return false;
    });
#else
    (void)dt;
#endif
}

} // namespace kke_showcase

namespace kke_showcase {

// KKE_DEMO_ONLINE=1 (run it on a host and a client, KKE_NET=host /
// join:...): the host spawns a barrel; the client asks for a crate, walks
// to it, lifts it, carries it a few steps and throws it. Both log what
// they see: the checks for this file.
void ShowcaseModule::updateOnlineDemo(float dt, kke::Locomotion::Input& in) {
#if KKE_ENABLE_NET
    if (!m_net || !m_net->connected()) return; // the clock starts once the game is joined
    m_demoOnline += dt;
    const float t = m_demoOnline;
    auto at = [&](float mark) { return t >= mark && t - dt < mark; };
    auto log = kke::log::get(name());
    kke::RigidWorld& w = m_rigid->world();
    auto report = [&](const char* when) {
        std::string list;
        for (const Prop& p : m_props) {
            const glm::vec3 q = w.position(p.body);
            list += fmt::format(" [{} at {:.1f} {:.1f} {:.1f}]", p.netId, q.x, q.y, q.z);
        }
        log->info("online demo ({}): {} {} props:{}{}", onlineHost() ? "host" : "client", when, m_props.size(), list,
                  m_held.body != kke::RigidWorld::kNoBody ? fmt::format(", holding body {}", netIdOf(m_held.body)) : std::string());
    };
    if (onlineHost()) {
        if (at(1.0f)) spawnRow(kRowBarrel);
        for (float mark : { 2.0f, 8.0f, 11.0f, 14.0f }) if (at(mark)) report(fmt::format("{:.0f} s", mark).c_str());
        if (at(16.0f)) log->info("online demo (host): done");
        return;
    }
    // Steps wait for what they need (at ~9 fps headless the clock alone misses).
    const float since = t - m_demoOnlineMark;
    auto next = [&] {
        ++m_demoOnlineStep;
        m_demoOnlineMark = t;
    };
    const glm::vec3 me = w.characterPosition(m_player);
    switch (m_demoOnlineStep) {
    case 0: // ask the host for a crate
        if (t >= 3.0f) {
            spawnRow(kRowCrate);
            next();
        }
        break;
    case 1: // walk up to it once it arrives
        if (!m_props.empty() && m_props.size() >= 2) {
            glm::vec3 to = w.position(m_props.back().body) - me;
            to.y = 0.0f;
            if (glm::length(to) < 1.3f) {
                report("the crate came");
                next();
            } else
                in.move = glm::normalize(to) * 0.4f;
        }
        if (since > 15.0f) {
            log->info("online demo (client): FAILED, no crate in reach");
            m_demoOnlineStep = 9;
        }
        break;
    case 2: // face it, lift it
        if (since > 0.5f) {
            togglePickUp();
            report("lifted");
            next();
        }
        break;
    case 3: // carry it a few steps
        if (since < 2.0f) in.move = m_loco->facing() * 0.5f;
        else {
            report("carrying");
            throwHeld(m_app->camera());
            report("thrown");
            next();
        }
        break;
    case 4:
        if (since > 2.5f) {
            report("after the throw");
            spawnRow(kRowClear);
            next();
        }
        break;
    case 5:
        if (since > 2.0f) {
            report("after clear");
            next();
        }
        break;
    case 6:
        log->info("online demo (client): done");
        next();
        break;
    default:
        break;
    }
#else
    (void)dt;
    (void)in;
#endif
}

} // namespace kke_showcase
