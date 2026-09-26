// ScriptModule, multiplayer: what sv_ scripts spawn appears on every
// player's machine (docs/SCRIPTING.md "Multiplayer", docs/NETWORKING.md
// "Spawned objects").
//
//   Host     an sv_ script's body, breakable box or ball is described
//            (kke/net/ScriptSpawns.h) and handed to NetModule::spawn;
//            bodies then travel in snapshots, breakables send their
//            breaks. Removing it (or reloading the script) despawns it.
//   Client   builds the same thing from the description, owned by the
//            pseudo-script kNetSource: drawn and cleaned up like a
//            script's own, and gone when we leave the game.
//
// Scripts that aren't sv_ run on every machine already, so what they
// spawn stays local (replicating it would make doubles).

#include "kke/modules/ScriptModule.h"

#include "kke/Log.h"
#include "kke/net/ScriptSpawns.h"

#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_FEMFX
#include "kke/FracturePattern.h"
#include "kke/modules/PhysicsModule.h"
#endif
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
#include "kke/modules/NetModule.h"
#endif

#include "kke/Application.h"

#include <algorithm>
#include <filesystem>
#include <typeindex>
#include <typeinfo>

namespace kke {

namespace {
// Owner of what the host's scripts made, on a client.
const char* const kNetSource = "(host)";
} // namespace

// NetModule first: whether this machine is the server (which sv_ scripts
// load) is known at the first folder scan, so a joining client never runs
// the host's sv_ scripts for a frame.
std::vector<ModuleDependency> ScriptModule::dependencies() const {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    return { { std::type_index(typeid(NetModule)), false, "net.*, and which sv_ scripts run here (host or client)" } };
#else
    return {};
#endif
}

bool ScriptModule::replicates(const std::string& source) const {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    const auto* net = m_app ? m_app->getModule<NetModule>() : nullptr;
    if (!net || net->role() != NetModule::Role::Host) return false;
    return std::filesystem::path(source).filename().string().rfind("sv_", 0) == 0;
#else
    (void)source;
    return false;
#endif
}

void ScriptModule::replicate(Body& b) {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    if (b.netDesc.empty() || !replicates(b.source)) return;
    auto* net = m_app->getModule<NetModule>();
    b.netId = net->spawn(script_net::kSpawnBody, b.netDesc);
    // Static bodies never move: the description says it all.
    const auto desc = script_net::decodeBody(b.netDesc);
    if (b.netId && desc && !desc->isStatic) net->bindSpawnedBody(b.netId, b.id);
#else
    (void)b;
#endif
}

void ScriptModule::replicate(Breakable& b) {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    if (b.netDesc.empty() || !replicates(b.source)) return;
    auto* net = m_app->getModule<NetModule>();
    // A ball is a throw: players who join later don't see it thrown again.
    const bool persistent = b.netKind != script_net::kSpawnBall;
    b.netId = net->spawn(b.netKind, b.netDesc, persistent);
    if (b.netId && b.netKind == script_net::kSpawnBreakableBox) net->bindSpawnedBreakable(b.netId, b.handle);
#else
    (void)b;
#endif
}

// Host: gone for everyone. Client (a script removing the host's copy
// here): NetModule stops steering a body that no longer exists.
void ScriptModule::unreplicate(uint16_t netId) {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    if (!netId) return;
    if (auto* net = m_app->getModule<NetModule>()) net->despawn(netId);
#else
    (void)netId;
#endif
}

void ScriptModule::bindReplication() {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    auto* net = m_app->getModule<NetModule>();
    if (!net) return;
    net->addSpawnListener([this](const net::SpawnMsg& m) { onNetSpawn(m.id, m.kind, m.desc); });
    net->addDespawnListener([this](uint16_t id) { onNetDespawn(id); });
    m_netRole = static_cast<int>(net->role());
#endif
}

// Hosting starts: everything sv_ scripts made so far goes out. Hosting
// ends: those ids mean nothing any more. Leaving someone else's game:
// what their scripts made goes with them.
void ScriptModule::syncNetRole() {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    auto* net = m_app->getModule<NetModule>();
    if (!net) return;
    const int role = static_cast<int>(net->role());
    if (role == m_netRole) return;
    const int was = m_netRole;
    m_netRole = role;
    if (was == static_cast<int>(NetModule::Role::Client)) releaseScript(kNetSource);
    if (was == static_cast<int>(NetModule::Role::Host)) {
        for (Body& b : m_bodies) b.netId = 0;
        for (Breakable& b : m_breakables) b.netId = 0;
    }
    if (net->role() != NetModule::Role::Host) return;
    // Not yet sent (made while offline); what a script spawned since
    // hosting began already went out with its spawn.
    for (Body& b : m_bodies)
        if (!b.netId) replicate(b);
    for (Breakable& b : m_breakables)
        if (!b.netId && b.netKind != script_net::kSpawnBall) replicate(b); // an old throw isn't thrown again
#endif
}

void ScriptModule::onNetSpawn(uint16_t id, uint16_t kind, const std::vector<uint8_t>& desc) {
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    auto* net = m_app->getModule<NetModule>();
    const size_t mine = std::count_if(m_bodies.begin(), m_bodies.end(), [](const Body& b) { return b.source == kNetSource; }) +
                        std::count_if(m_breakables.begin(), m_breakables.end(), [](const Breakable& b) { return b.source == kNetSource; });
    if (mine >= maxBodiesPerScript + maxBreakablesPerScript) {
        log::get(name())->error("the host's scripts spawned more than {} objects; object {} (kind {:#x}) not built", mine, id, kind);
        return;
    }
    if (kind == script_net::kSpawnBody) {
        auto* rbm = m_app->getModule<RigidBodyModule>();
        const auto b = script_net::decodeBody(desc);
        if (!b) {
            log::get(name())->error("the host's script body {} has a damaged description; not built", id);
            return;
        }
        if (!rbm) return;
        RigidWorld::BodyDesc d;
        d.shape = b->sphere ? RigidWorld::Shape::Sphere : RigidWorld::Shape::Box;
        d.position = b->position;
        d.velocity = b->velocity;
        d.radius = b->radius;
        d.halfExtents = b->halfExtents;
        d.density = b->density;
        d.friction = b->friction;
        d.restitution = b->restitution;
        d.material = b->material;
        if (b->isStatic) d.motion = RigidWorld::Motion::Static;
        const RigidWorld::BodyId body = rbm->world().add(d);
        if (body == RigidWorld::kNoBody) {
            log::get(name())->error("the host's script body {}: this physics world is full; not built", id);
            return;
        }
        m_bodies.push_back({ body, kNetSource, b->sphere, b->sphere ? glm::vec3(b->radius) : b->halfExtents, b->color, id, {} });
        if (!b->isStatic) net->bindSpawnedBody(id, body);
        return;
    }
#if KKE_ENABLE_FEMFX
    auto* femfx = m_app->getModule<PhysicsModule>();
    if (kind == script_net::kSpawnBreakableBox) {
        const auto b = script_net::decodeBreakableBox(desc);
        if (!b) {
            log::get(name())->error("the host's script breakable {} has a damaged description; not built", id);
            return;
        }
        if (!femfx) return;
        const PhysicsModule::ObjectHandle h = femfx->spawnPatternedBox(b->cells, b->size, b->position, b->material, b->pattern, b->chunk, 0, b->velocity,
                                                                       b->armSeconds, nullptr, b->seed);
        if (h == PhysicsModule::kInvalidHandle) {
            log::get(name())->error("the host's script breakable {}: the FEMFX scene is full; not built", id);
            return;
        }
        m_breakables.push_back({ h, kNetSource, false, id, kind, {} });
        net->bindSpawnedBreakable(id, h);
        return;
    }
    if (kind == script_net::kSpawnBall) {
        const auto b = script_net::decodeBall(desc);
        if (!b) {
            log::get(name())->error("the host's script ball {} has a damaged description; not built", id);
            return;
        }
        if (!femfx) return;
        const PhysicsModule::ObjectHandle h = femfx->spawnFracturableTetMesh(PhysicsModule::buildSphere(3, b->radius), b->position, b->material, b->velocity);
        if (h == PhysicsModule::kInvalidHandle) {
            log::get(name())->error("the host's script ball {}: the FEMFX scene is full; not built", id);
            return;
        }
        m_breakables.push_back({ h, kNetSource, false, id, kind, {} });
        return;
    }
#endif
    // Another module's kind, or one this build can't make (no FEMFX): not ours.
#else
    (void)id;
    (void)kind;
    (void)desc;
#endif
}

void ScriptModule::onNetDespawn(uint16_t id) {
#if KKE_ENABLE_JOLT
    if (auto* rbm = m_app->getModule<RigidBodyModule>())
        for (const Body& b : m_bodies)
            if (b.source == kNetSource && b.netId == id) rbm->world().remove(b.id);
#endif
    std::erase_if(m_bodies, [id](const Body& b) { return b.source == kNetSource && b.netId == id; });
#if KKE_ENABLE_FEMFX
    if (auto* femfx = m_app->getModule<PhysicsModule>())
        for (const Breakable& b : m_breakables)
            if (b.source == kNetSource && b.netId == id) femfx->removeObject(b.handle);
#endif
    std::erase_if(m_breakables, [id](const Breakable& b) { return b.source == kNetSource && b.netId == id; });
}

} // namespace kke
