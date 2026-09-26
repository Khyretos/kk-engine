#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// A "capability" is a small interface a module can optionally implement so
// *other* modules can discover and use it without either side depending on
// the other's concrete type. This is how "recognize each other if both are
// present, work fine standalone if not" actually gets implemented:
// NetworkModule never #includes DestructionModule.h, and DestructionModule
// never #includes NetworkModule.h — both only know about
// INetworkReplicable. Multiply-inherit it alongside Module:
//
//   class DestructionModule : public kke::Module, public kke::INetworkReplicable { ... };
//
// and discover implementers from the other side with:
//
//   for (auto* r : app.findCapability<kke::INetworkReplicable>()) { ... }
//
// findCapability() works whether zero, one, or several modules implement
// the interface — a DestructionModule with no NetworkModule present just
// never gets asked for its state, and keeps working exactly as if it were
// the only module in the Application.
//
// Add more capability interfaces the same way for other cross-cutting
// concerns — e.g. an ISaveable for save-game state, or an IDebugDrawable
// for a debug-draw module that wants to overlay every physics shape
// without physics knowing debug-draw exists.
class INetworkReplicable {
public:
    virtual ~INetworkReplicable() = default;

    // A short, stable routing key for this stream of state (e.g.
    // "destruction.crate_01"). Whatever transport eventually moves this
    // data uses this to route it to the matching object on the other end.
    virtual std::string replicationChannelName() const = 0;

    // Serialize whatever this module considers its authoritative,
    // must-reach-every-peer state. Deliberately NOT "serialize
    // everything" — the whole point of a capability like this is that
    // each implementer decides how little it can get away with sending.
    // DestructionModule, for example, serializes an 8-byte seed and a
    // trigger tick — not the fragment geometry that seed deterministically
    // produces on the receiving end.
    virtual std::vector<uint8_t> serializeReplicatedState() = 0;

    // Apply state received from a peer (or a replay/save file).
    virtual void deserializeReplicatedState(const std::vector<uint8_t>& data) = 0;
};

// Implemented by any module that reacts to player settings (UI scale,
// camera sensitivity, ...). kke::SettingsModule calls every implementer
// whenever settings are applied — and a module that implements this
// works fine in a game with no SettingsModule at all (it just never gets
// called). Same "recognize each other if both present" pattern as
// INetworkReplicable above.
struct EngineSettings;
class ISettingsListener {
public:
    virtual ~ISettingsListener() = default;
    virtual void onSettingsChanged(const EngineSettings& settings) = 0;
};

// Implemented by a physics module that can simulate ragdolls (FEMFX's
// PhysicsModule is the first). Character code builds a kke::RagdollDesc
// from its skeleton (kke/Ragdoll.h), hands it to whichever module offers
// this, and reads body transforms back every frame — so swapping physics
// engines doesn't touch the character side.
struct RagdollDesc;
class IRagdollPhysics {
public:
    using RagdollHandle = uint32_t; // 0 = invalid
    virtual ~IRagdollPhysics() = default;
    // Every body starts moving at `initialVelocity` (world, m/s).
    virtual RagdollHandle createRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) = 0;
    virtual void destroyRagdoll(RagdollHandle handle) = 0;
    // Current world transform of each body, in RagdollDesc::bodies order.
    virtual bool ragdollBodyTransforms(RagdollHandle handle, std::vector<glm::mat4>& out) const = 0;
    // Adds a velocity change to one body (a punch, a bullet, an explosion).
    virtual void pushRagdollBody(RagdollHandle handle, int body, const glm::vec3& deltaVelocity) = 0;
    // How good this module's ragdolls are, for picking one when several
    // offer them: FEMFX 0 (no joint limits, limbs pass through each
    // other), Jolt 1 (cone/twist limits, limbs collide).
    virtual int ragdollQuality() const { return 0; }
};

// The best ragdoll provider among `providers` (e.g.
// app.findCapability<IRagdollPhysics>()), or nullptr if there are none.
inline IRagdollPhysics* bestRagdollPhysics(const std::vector<IRagdollPhysics*>& providers) {
    IRagdollPhysics* best = nullptr;
    for (IRagdollPhysics* p : providers)
        if (p && (!best || p->ragdollQuality() > best->ragdollQuality())) best = p;
    return best;
}

// Implemented by every physics module: FEMFX's PhysicsModule (soft,
// breakable objects) and Jolt's RigidBodyModule (rigid bodies, the
// character, ragdolls, debris). Game code that asks "what's there?" or
// "blow this up" asks all of them through this (kke/PhysicsWorld.h:
// kke::physicsRaycast(), kke::physicsBlast()), without knowing which
// engine holds a thing. Issue #31.
class IPhysicsWorld {
public:
    struct Stats {
        size_t bodies = 0;   // what this engine simulates (FEMFX: pieces)
        size_t awake = 0;
        double stepMs = 0.0; // last (or recent average) step cost
    };
    struct Hit {
        bool hit = false;
        glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f}; // normal faces the ray's origin
        float distance = 0.0f;
        uint64_t body = 0;   // engine-specific id (Jolt body id, FEMFX object handle)
        const IPhysicsWorld* world = nullptr;
    };
    virtual ~IPhysicsWorld() = default;
    virtual const char* physicsEngineName() const = 0;
    virtual Stats physicsStats() const = 0;
    // Closest hit along the ray, up to maxDistance (m).
    virtual Hit physicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const = 0;
    // A blast: everything movable within `radius` of `center` gets a
    // velocity change away from it, `speed` m/s at the centre fading
    // linearly to 0 at the radius. Wakes what it touches. Returns how many
    // bodies (FEMFX: pieces) it pushed.
    virtual size_t physicsBlast(const glm::vec3& center, float radius, float speed) = 0;
    // World bounds of every movable thing overlapping min..max, appended.
    virtual void physicsBoundsInBox(const glm::vec3& min, const glm::vec3& max, std::vector<std::pair<glm::vec3, glm::vec3>>& out) const = 0;
};

} // namespace kke
