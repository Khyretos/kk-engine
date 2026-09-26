#include "kke/net/ScriptSpawns.h"

#include "kke/net/BitStream.h"

#include <cmath>
#include <cstring>

namespace kke::script_net {

namespace {

// Floats as their 32 bits (these descriptions are small and sent once);
// a non-finite one read back fails the stream.
template <typename Stream> void rawFloat(Stream& s, float& f) {
    uint32_t bits = 0;
    if constexpr (Stream::kWriting) std::memcpy(&bits, &f, sizeof bits);
    s.bits(bits, 32);
    if constexpr (Stream::kReading) {
        std::memcpy(&f, &bits, sizeof f);
        if (!std::isfinite(f)) {
            s.fail();
            f = 0.0f;
        }
    }
}
template <typename Stream> void rawVec3(Stream& s, glm::vec3& v) {
    rawFloat(s, v.x);
    rawFloat(s, v.y);
    rawFloat(s, v.z);
}
template <typename Stream> void serialize(Stream& s, Material& m) {
    rawFloat(s, m.density);
    rawFloat(s, m.stiffness);
    rawFloat(s, m.poissonsRatio);
    rawFloat(s, m.fractureStressThreshold);
    rawFloat(s, m.plasticYieldThreshold);
    rawFloat(s, m.plasticCreep);
    rawFloat(s, m.metallic);
    rawFloat(s, m.roughness);
    s.integer(m.textureId, -1, 62);
}
template <typename Stream> void serialize(Stream& s, BodySpawn& b) {
    s.boolean(b.sphere);
    s.boolean(b.isStatic);
    rawVec3(s, b.position);
    rawVec3(s, b.velocity);
    rawVec3(s, b.halfExtents);
    rawFloat(s, b.radius);
    rawFloat(s, b.density);
    rawFloat(s, b.friction);
    rawFloat(s, b.restitution);
    s.bits(b.material, 32);
    rawVec3(s, b.color);
}
template <typename Stream> void serialize(Stream& s, BreakableBoxSpawn& b) {
    s.integer(b.cells.x, 1, 24);
    s.integer(b.cells.y, 1, 24);
    s.integer(b.cells.z, 1, 24);
    rawVec3(s, b.size);
    rawVec3(s, b.position);
    rawVec3(s, b.velocity);
    serialize(s, b.material);
    s.integer(b.pattern, 0, 15);
    rawFloat(s, b.chunk);
    rawFloat(s, b.armSeconds);
    s.bits(b.seed, 32);
}
template <typename Stream> void serialize(Stream& s, BallSpawn& b) {
    rawFloat(s, b.radius);
    rawVec3(s, b.position);
    rawVec3(s, b.velocity);
    serialize(s, b.material);
}

template <typename T> std::vector<uint8_t> pack(T value) {
    std::vector<uint8_t> out;
    {
        net::WriteStream w(out);
        serialize(w, value);
    }
    return out;
}
template <typename T> std::optional<T> unpack(const std::vector<uint8_t>& data) {
    net::ReadStream r(data.data(), data.size());
    T value{};
    serialize(r, value);
    if (!r.ok() || r.bitsLeft() >= 8) return std::nullopt;
    return value;
}

// Sizes a script could ask for (the bindings clamp to these too): a
// description off the network outside them is refused, not built.
bool sane(const glm::vec3& v, float lo, float hi) {
    return v.x >= lo && v.y >= lo && v.z >= lo && v.x <= hi && v.y <= hi && v.z <= hi;
}
bool saneMaterial(const Material& m) {
    return m.density > 0.0f && m.density <= 1.0e5f && m.stiffness > 0.0f && m.poissonsRatio > -1.0f && m.poissonsRatio < 0.5f &&
           m.fractureStressThreshold > 0.0f;
}
constexpr float kWorld = 4096.0f;

} // namespace

std::vector<uint8_t> encode(const BodySpawn& b) { return pack(b); }
std::vector<uint8_t> encode(const BreakableBoxSpawn& b) { return pack(b); }
std::vector<uint8_t> encode(const BallSpawn& b) { return pack(b); }

std::optional<BodySpawn> decodeBody(const std::vector<uint8_t>& data) {
    auto b = unpack<BodySpawn>(data);
    if (!b || !sane(b->position, -kWorld, kWorld) || !sane(b->velocity, -1000.0f, 1000.0f) || !sane(b->halfExtents, 0.005f, 100.0f) ||
        b->radius < 0.005f || b->radius > 100.0f || b->density <= 0.0f || b->density > 1.0e5f)
        return std::nullopt;
    return b;
}

std::optional<BreakableBoxSpawn> decodeBreakableBox(const std::vector<uint8_t>& data) {
    auto b = unpack<BreakableBoxSpawn>(data);
    if (!b || !sane(b->size, 0.01f, 20.0f) || !sane(b->position, -kWorld, kWorld) || !sane(b->velocity, -1000.0f, 1000.0f) ||
        !saneMaterial(b->material) || b->chunk < 0.01f || b->chunk > 20.0f || b->armSeconds < 0.0f || b->armSeconds > 600.0f)
        return std::nullopt;
    return b;
}

std::optional<BallSpawn> decodeBall(const std::vector<uint8_t>& data) {
    auto b = unpack<BallSpawn>(data);
    if (!b || b->radius < 0.01f || b->radius > 2.0f || !sane(b->position, -kWorld, kWorld) || !sane(b->velocity, -1000.0f, 1000.0f) ||
        !saneMaterial(b->material))
        return std::nullopt;
    return b;
}

} // namespace kke::script_net
