#pragma once

#include "kke/RigidWorld.h"
#include "kke/net/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace kke::net {

// The host's second look at a client's move, after the speed limits
// (NetServer::checkMove, docs/NETWORKING.md "Movement checks"): with the
// level in its own physics world, it refuses a move that
//
//   goes through a wall   the path from the last accepted position to the
//                         new one, at knee height, crosses static level
//                         geometry. Either the straight line or "up, over,
//                         down" (a vault or a climb) must be clear, so
//                         steps, vaults and ledge climbs pass;
//   flies                 nothing to stand on or hold within reach (ground
//                         below; a wall or a ledge beside, for climbs,
//                         hangs and wall runs), and either it has risen
//                         more than a jump or a climb can (maxAirRise), or
//                         it has been in the air past the grace time
//                         without falling as gravity would make it.
//
// What it can't see: moves inside those rules (a player running a little
// fast stays within MovementLimits' slack). Server-side input replay is
// what closes that (issue #28); this catches the blatant cheats, and the
// bugs that look like them, without the client's controller on the host.
struct MoveCheckSettings {
    float rayHeight = 0.5f;      // m above the feet: under a crouch, over stairs
    float supportReach = 0.6f;   // m: ground below the feet, or a wall beside the body, this close holds you up
    float maxAirRise = 3.0f;     // m climbed with nothing in reach (a jump is ~1.3)
    double airGrace = 1.5;       // s in the air before it has to be falling
    float minFallGravity = 3.0f; // m/s2 of fall required after the grace (a third of real gravity); 0 = off
};

class WorldMoveCheck {
public:
    enum class Verdict { Ok, ThroughWall, Flying };

    // `isPlayerBody`: bodies standing in for players (NetModule's remote
    // capsules), which are no support: a player's own capsule is where
    // they are.
    explicit WorldMoveCheck(const RigidWorld& world, std::function<bool(RigidWorld::BodyId)> isPlayerBody = {});

    // One accepted-by-speed move of `player`, `dt` seconds long.
    Verdict check(uint8_t player, const NetPlayerState& from, const NetPlayerState& to, double dt);
    // The player left, teleported or was corrected: start its air time over.
    void forget(uint8_t player) { m_air.erase(player); }

    MoveCheckSettings settings;
    size_t throughWalls = 0, flying = 0; // refusals so far

    // The pieces, public for tests and debug views.
    bool pathClear(const glm::vec3& fromFeet, const glm::vec3& toFeet) const;
    bool supported(const glm::vec3& feet) const;

private:
    bool segmentClear(const glm::vec3& a, const glm::vec3& b) const;
    struct Air {
        bool inAir = false;
        double time = 0.0;
        float rise = 0.0f;
        float peakY = 0.0f;
    };
    const RigidWorld& m_world;
    std::function<bool(RigidWorld::BodyId)> m_isPlayerBody;
    std::map<uint8_t, Air> m_air;
};

} // namespace kke::net
