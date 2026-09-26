#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kke {

// Building blocks for Simple mode ("make the game while playing it",
// docs/PLAY_TO_MAKE.md): the big-icon palette a five-year-old drags
// things out of, and the bat that knocks characters over. Pure logic, no
// rendering or physics, so the three levels (Simple palette, node graph,
// Lua) can share it and it is unit-tested (tests/test_play_blocks.cpp).

enum class PlayBlockKind {
    Character, // placed in the world; ragdolls when hit
    Prop,      // placed in the world
    Tool,      // held: what a click does (the bat)
};

struct PlayBlock {
    std::string id;                  // stable: "person", "bat", ...
    std::string label;               // what the palette shows under the icon
    PlayBlockKind kind = PlayBlockKind::Prop;
    // Catalog asset names that can stand for this block, most wanted
    // first. Characters pick one at random from those present, so every
    // person dragged out looks different.
    std::vector<std::string> assets;
    bool randomAsset = false;
};

// The Simple palette: a person, a bat, a crate, a barrel, a ball, a cone.
// Asset names are from the Synty packs the project uses (POLYGON City
// Characters / Fantasy Characters, POLYGON Prototype); anyone without
// them simply doesn't see those blocks (see availableAssets).
std::vector<PlayBlock> defaultPlayBlocks();

// The block's assets that `exists` says are on disk, in the block's order.
std::vector<std::string> availableAssets(const PlayBlock& block, const std::function<bool(const std::string&)>& exists);

// Which asset a block uses this time: the first available one, or (for
// randomAsset blocks) available[pick % size]. Empty if none is available.
std::string chooseAsset(const PlayBlock& block, const std::vector<std::string>& available, uint32_t pick);

// Yaw (degrees, about +Y, the engine's placement convention: a model's +Z
// ends up pointing at (sin yaw, 0, cos yaw)) that turns something at
// `position` to face `viewer`. 0 if they're on top of each other.
float yawToFace(const glm::vec3& position, const glm::vec3& viewer);

// A long, thin model (a bat) described along its length: `axis` is a unit
// vector in model space from the handle to the thick end, `handle` the
// handle's end point. Found from the vertices: the longest bounds axis,
// oriented so the end with the wider cross-section is the far one.
struct LongAxis {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    glm::vec3 handle{0.0f};
    float length = 0.0f;
};
LongAxis findLongAxis(const std::vector<glm::vec3>& points);

// A horizontal bat swing, right-handed: the bat sweeps from the swinger's
// left, through straight ahead, to the right, around `pivot` (the
// swinger's shoulder). Angle 0 = straight ahead along `forward`.
struct SwingSettings {
    float swingSeconds = 0.26f;   // the fast part, the only part that hits
    float holdSeconds = 0.30f;    // follow-through pose, then the bat goes away
    float startDegrees = -80.0f;  // negative = to the left
    float endDegrees = 95.0f;
    float innerReach = 0.35f;     // hands, from the pivot (m)
    float reach = 1.25f;          // bat tip, from the pivot (m)
    float pivotHeight = 1.15f;    // shoulder height above the ground (m)
    float maxPushSpeed = 9.0f;    // m/s given to what is hit, at full swing speed
    float lift = 2.5f;            // m/s upward on a hit, so hit things leave the ground
};

class BatSwing {
public:
    explicit BatSwing(SwingSettings settings = {});

    // Where to stand so the sweet spot (80% of the reach, angle 0) passes
    // through `target`, swinging toward `forward` (only its XZ part counts).
    glm::vec3 pivotFor(const glm::vec3& target, const glm::vec3& forward) const;

    // Starts a swing; false (and nothing changes) if one is still going.
    bool start(const glm::vec3& pivot, const glm::vec3& forward);
    // Advances time. Returns true while the bat is out (swing + hold).
    bool update(float dt);

    bool active() const { return m_phase != Phase::Idle; }
    bool hitting() const { return m_phase == Phase::Swing; }
    float angleDegrees() const { return m_angle; }
    const glm::vec3& pivot() const { return m_pivot; }
    // Unit, horizontal: from the pivot along the bat, at `degrees`.
    glm::vec3 direction(float degrees) const;
    glm::vec3 direction() const { return direction(m_angle); }

    // What the bat hit between the previous update and this one: its path
    // (the segment from the hands to the tip, swept through the angles in
    // between) against a box. `push` is the velocity to give what was hit:
    // along the swing, a little away from the swinger, and up.
    struct Hit {
        bool hit = false;
        glm::vec3 point{0.0f};
        glm::vec3 push{0.0f};
    };
    Hit sweep(const glm::vec3& boxMin, const glm::vec3& boxMax) const;

    const SwingSettings& settings() const { return m_settings; }

private:
    enum class Phase { Idle, Swing, Hold };
    float angleAt(float t) const; // t = seconds into the swing

    SwingSettings m_settings;
    Phase m_phase = Phase::Idle;
    float m_time = 0.0f;
    float m_angle = 0.0f, m_prevAngle = 0.0f;
    float m_angularSpeed = 0.0f; // degrees per second, last update
    bool m_swept = false;        // the last update moved the bat through the swing
    glm::vec3 m_pivot{0.0f};
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f}, m_right{1.0f, 0.0f, 0.0f};
};

} // namespace kke
