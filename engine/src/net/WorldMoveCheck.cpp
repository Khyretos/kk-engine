#include "kke/net/WorldMoveCheck.h"

#include <algorithm>
#include <cmath>

namespace kke::net {

namespace {
// A hit this close to the start is the ray starting on a surface (the
// player brushing a wall), not a wall in the way.
constexpr float kStartSkin = 0.01f;
// Wall probes around the body: 8 directions at two heights.
constexpr float kProbeHeights[] = { 0.3f, 1.2f };
constexpr float kBodyRadius = 0.3f;
} // namespace

WorldMoveCheck::WorldMoveCheck(const RigidWorld& world, std::function<bool(RigidWorld::BodyId)> isPlayerBody)
    : m_world(world), m_isPlayerBody(std::move(isPlayerBody)) {}

bool WorldMoveCheck::segmentClear(const glm::vec3& a, const glm::vec3& b) const {
    const glm::vec3 d = b - a;
    const float length = glm::length(d);
    if (length < 1e-4f) return true;
    const RigidWorld::RayHit hit =
        m_world.raycast(a, d / length, length, [](RigidWorld::BodyId, RigidWorld::Motion m) { return m == RigidWorld::Motion::Static; });
    return !hit.hit || hit.distance <= kStartSkin;
}

bool WorldMoveCheck::pathClear(const glm::vec3& fromFeet, const glm::vec3& toFeet) const {
    const glm::vec3 up(0.0f, settings.rayHeight, 0.0f);
    const glm::vec3 a = fromFeet + up, b = toFeet + up;
    if (segmentClear(a, b)) return true;
    // Up, over, down: a vault or a climb goes over what's between.
    const float top = std::max(a.y, b.y);
    const glm::vec3 a2(a.x, top, a.z), b2(b.x, top, b.z);
    return segmentClear(a, a2) && segmentClear(a2, b2) && segmentClear(b2, b);
}

bool WorldMoveCheck::supported(const glm::vec3& feet) const {
    auto notPlayer = [this](RigidWorld::BodyId id, RigidWorld::Motion) { return !m_isPlayerBody || !m_isPlayerBody(id); };
    // Ground: from a little above the feet (a foot sunk a few mm into it
    // still finds it) down to supportReach below.
    constexpr float kAbove = 0.3f;
    if (m_world.raycast(feet + glm::vec3(0.0f, kAbove, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), kAbove + settings.supportReach, notPlayer).hit) return true;
    // Something to hold: a wall or a ledge beside the body.
    for (float h : kProbeHeights)
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 0.7853982f;
            const glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
            if (m_world.raycast(feet + glm::vec3(0.0f, h, 0.0f), dir, kBodyRadius + settings.supportReach, notPlayer).hit) return true;
        }
    return false;
}

WorldMoveCheck::Verdict WorldMoveCheck::check(uint8_t player, const NetPlayerState& from, const NetPlayerState& to, double dt) {
    if (!pathClear(from.position, to.position)) {
        ++throughWalls;
        return Verdict::ThroughWall;
    }
    Air& air = m_air[player];
    if (supported(to.position)) {
        air = Air{};
        return Verdict::Ok;
    }
    if (!air.inAir) air = Air{ true, 0.0, 0.0f, from.position.y };
    air.time += std::max(0.0, dt);
    air.rise += std::max(0.0f, to.position.y - from.position.y);
    air.peakY = std::max(air.peakY, to.position.y);
    bool fly = air.rise > settings.maxAirRise;
    if (!fly && settings.minFallGravity > 0.0f && air.time > settings.airGrace) {
        const float t = static_cast<float>(air.time - settings.airGrace);
        const float mustHaveFallen = 0.5f * settings.minFallGravity * t * t - 0.25f; // 25 cm slack
        fly = air.peakY - to.position.y < mustHaveFallen;
    }
    if (!fly) return Verdict::Ok;
    ++flying;
    // Corrected back to `from`: from there a fresh fall is judged (so the
    // honest drop that follows a correction isn't refused too).
    air = Air{ true, settings.airGrace, 0.0f, from.position.y };
    return Verdict::Flying;
}

} // namespace kke::net
