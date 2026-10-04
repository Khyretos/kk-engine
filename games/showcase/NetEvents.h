#pragma once
// kke_demo's game events and spawned-object descriptions (NetModule
// sendEvent / spawn), serialized the docs/NETWORKING.md way: one
// function per message for both directions.

#if KKE_ENABLE_NET
#include "kke/net/BitStream.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace kke_showcase::netev {

enum EventKind : uint16_t {
    kEventShoot = 1,    // anyone: a ball from the camera (the host relays it)
    kEventPush = 2,     // client -> host: push a level crate
    kEventReset = 3,    // client -> host: the level's crates back
    kEventSpawnRow = 4, // client -> host: a spawn menu row (made at the client's spot), or "clear"
    kEventCarry = 5,    // client -> host: picked up / put down a replicated body
    kEventThrow = 6,    // client -> host: threw what it carried
};
// Spawned-object kind (NetModule::spawn): a spawn menu prop. Script
// spawns use 0x4Cxx (kke/net/ScriptSpawns.h).
constexpr uint16_t kSpawnProp = 0x5301;

inline const glm::vec3 kWorldMin(-4096.0f, -512.0f, -4096.0f), kWorldMax(4096.0f, 1536.0f, 4096.0f);

struct ShotEvent { glm::vec3 from{0.0f}, dir{0.0f, 0.0f, -1.0f}; };
struct PushEvent { uint16_t body = 0; glm::vec3 dir{0.0f}, point{0.0f}; };
struct SpawnRowEvent { uint8_t row = 0; glm::vec3 at{0.0f}, base{0.0f}; };
struct CarryEvent { uint16_t body = 0; bool carrying = false; float yawOffset = 0.0f; glm::vec3 velocity{0.0f}; };
struct ThrowEvent { uint16_t body = 0; glm::vec3 velocity{0.0f}, spin{0.0f}; };
struct PropDesc {
    uint8_t shape = 0; // ShowcaseModule::PropShape
    glm::vec3 half{0.3f}, color{0.6f}, at{0.0f};
    float density = 250.0f, metallic = 0.0f, restitution = 0.2f;
    uint8_t material = 0;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

template <typename Stream> bool serialize(Stream& s, ShotEvent& e) {
    s.vec3(e.from, kWorldMin, kWorldMax, 1.0f / 256.0f);
    s.vec3(e.dir, 1.0f, 1.0f / 2048.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, PushEvent& e) {
    s.integer(e.body, 0, 65535);
    s.vec3(e.dir, 1.0f, 1.0f / 2048.0f);
    s.vec3(e.point, kWorldMin, kWorldMax, 1.0f / 256.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, SpawnRowEvent& e) {
    s.integer(e.row, 0, 31);
    s.vec3(e.at, kWorldMin, kWorldMax, 1.0f / 256.0f);
    s.vec3(e.base, kWorldMin, kWorldMax, 1.0f / 256.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, CarryEvent& e) {
    s.integer(e.body, 0, 65535);
    s.boolean(e.carrying);
    s.real(e.yawOffset, -7.0f, 7.0f, 1.0f / 512.0f);
    s.vec3(e.velocity, 40.0f, 1.0f / 256.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, ThrowEvent& e) {
    s.integer(e.body, 0, 65535);
    s.vec3(e.velocity, 40.0f, 1.0f / 256.0f);
    s.vec3(e.spin, 20.0f, 1.0f / 256.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, PropDesc& d) {
    s.integer(d.shape, 0, 2);
    s.vec3(d.half, glm::vec3(0.0f), glm::vec3(4.0f), 1.0f / 1024.0f);
    s.vec3(d.color, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f / 255.0f);
    s.vec3(d.at, kWorldMin, kWorldMax, 1.0f / 256.0f);
    s.real(d.density, 1.0f, 20000.0f, 1.0f);
    s.real(d.metallic, 0.0f, 1.0f, 1.0f / 255.0f);
    s.real(d.restitution, 0.0f, 1.0f, 1.0f / 255.0f);
    s.integer(d.material, 0, 255);
    s.quat(d.rotation);
    return s.ok();
}

template <typename T> std::vector<uint8_t> pack(T value) {
    std::vector<uint8_t> out;
    {
        kke::net::WriteStream w(out);
        serialize(w, value);
    }
    return out;
}
template <typename T> bool unpack(const std::vector<uint8_t>& data, T& value) {
    kke::net::ReadStream r(data.data(), data.size());
    return serialize(r, value) && r.ok();
}

} // namespace kke_showcase::netev
#endif
