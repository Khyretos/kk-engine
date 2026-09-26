#pragma once

// What sv_ scripts spawn, as NetModule spawn descriptions
// (docs/SCRIPTING.md "Multiplayer"): ScriptModule's host side encodes,
// its client side decodes and builds. Pure serialization (no Lua, no
// physics), so tests/test_net.cpp round-trips and fuzzes it.

#include "kke/Material.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace kke::script_net {

// NetModule spawn kinds (next to NetModule::kScriptEventKind = 0x4C00).
constexpr uint16_t kSpawnBody = 0x4C01;          // physics.box / physics.sphere
constexpr uint16_t kSpawnBreakableBox = 0x4C02;  // breakable.box
constexpr uint16_t kSpawnBall = 0x4C03;          // breakable.ball (transient: a throw)

struct BodySpawn {
    bool sphere = false;
    bool isStatic = false;
    glm::vec3 position{0.0f}, velocity{0.0f};
    glm::vec3 halfExtents{0.25f};
    float radius = 0.25f;
    float density = 500.0f, friction = 0.6f, restitution = 0.1f;
    uint32_t material = 0;
    glm::vec3 color{0.8f};
};

struct BreakableBoxSpawn {
    glm::ivec3 cells{1};
    glm::vec3 size{1.0f}, position{0.0f}, velocity{0.0f};
    Material material;
    int pattern = 0;       // kke::FracturePattern
    float chunk = 0.3f;
    float armSeconds = 3.0f;
    uint32_t seed = 0;     // the host's fracture seed: the same pieces everywhere
};

struct BallSpawn {
    float radius = 0.15f;
    glm::vec3 position{0.0f}, velocity{0.0f};
    Material material;
};

std::vector<uint8_t> encode(const BodySpawn& b);
std::vector<uint8_t> encode(const BreakableBoxSpawn& b);
std::vector<uint8_t> encode(const BallSpawn& b);
// nullopt: malformed, out of range or not finite (it came off the network).
std::optional<BodySpawn> decodeBody(const std::vector<uint8_t>& data);
std::optional<BreakableBoxSpawn> decodeBreakableBox(const std::vector<uint8_t>& data);
std::optional<BallSpawn> decodeBall(const std::vector<uint8_t>& data);

} // namespace kke::script_net
