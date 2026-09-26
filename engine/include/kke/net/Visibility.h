#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace kke::net {

// Server fog of war (docs/ANTI_CHEAT.md "Fog of war"): which players each
// player may know about. The server knows everything; a client is only
// sent what its player could see or hear, so a wallhack has nothing to
// draw. The same idea as Valorant's and CS2's server-side fog of war.
//
// It costs no input latency: the server decides while building each
// snapshot. What it costs is server CPU (a few rays per pair of players),
// kept down by:
//   distance     beyond maxDistance: never; within hearingRadius: always
//                (footsteps and grenades are heard through walls);
//   rechecks     each pair is re-traced every `recheck` seconds, not
//                every snapshot, and stays visible for `keepVisible`
//                after losing sight (no flicker at a doorframe);
//   early rays   are cast from where the viewer's eye will be and to
//                where the subject will be `lead` seconds from now
//                (latency plus a frame), to the subject's head, middle,
//                feet and sides: a player coming round a corner is sent
//                before they appear, never after (no "pop-in").
//
// Pure logic over a line-of-sight callback, so it runs on any world;
// makeRayClear() (NetModule / kke_server) uses the host's static level.
struct VisibilitySettings {
    float maxDistance = 200.0f;   // m: never sent beyond this
    float hearingRadius = 15.0f;  // m: always sent within this
    float eyeHeight = 1.6f;       // m above the feet
    float bodyHeight = 1.8f;
    float bodyRadius = 0.45f;     // plus the margin below: rays go to the body's edges
    float margin = 0.3f;          // m added around the body
    double lead = 0.15;           // s both players are moved ahead along their velocity
    double recheck = 0.1;         // s between traces of one pair
    double keepVisible = 0.5;     // s a pair stays visible after losing sight
};

class Visibility {
public:
    // True when nothing solid is between the two points.
    using RayClear = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;

    struct Player {
        uint8_t id = 0;
        glm::vec3 feet{0.0f};
        glm::vec3 velocity{0.0f};
    };

    explicit Visibility(RayClear clear) : m_clear(std::move(clear)) {}

    // Brings every pair up to date for `now`. Players not in the list are
    // forgotten.
    void update(double now, const std::vector<Player>& players);
    // May `viewer` be told about `subject`? Unknown pairs: false.
    bool visible(uint8_t viewer, uint8_t subject) const;
    void forget(uint8_t id);

    VisibilitySettings settings;
    size_t raysLastUpdate = 0; // cost of the last update(), for stats
    size_t tracedPairs = 0;    // pairs traced (not decided by distance) in the last update()

private:
    struct Pair {
        bool visible = false;
        double nextCheck = -1e300;
        double lastSeen = -1e300;
    };
    bool sees(const Player& viewer, const Player& subject);

    RayClear m_clear;
    std::map<std::pair<uint8_t, uint8_t>, Pair> m_pairs; // (viewer, subject)
};

} // namespace kke::net
