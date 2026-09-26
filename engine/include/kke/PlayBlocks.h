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

// A controller as a pointer (Simple mode on a gamepad): the left stick
// moves a cursor, so every drag-and-click in the palette and the world
// works the same as with a mouse or a finger.
struct PadPointerSettings {
    float deadzone = 0.2f;        // stick deflection that does nothing (worn sticks drift)
    float pixelsPerSecond = 900.0f; // at full tilt, on a 720-pixel-tall screen
    float curve = 2.0f;           // deflection^curve: small tilts for aiming, full tilt to cross the screen
};
// How far the cursor moves this frame for a stick deflection (-1..1 per
// axis). `screenHeight` scales the speed so it takes the same time to
// cross any screen.
glm::vec2 padPointerStep(const glm::vec2& stick, float dt, float screenHeight, const PadPointerSettings& settings = {});
// The palette cell to jump to from `cursor` with the shoulder buttons:
// `direction` +1 = the next cell to the right, -1 = to the left, wrapping
// around. `centers` in left-to-right order. -1 if there are none.
int stepPaletteCell(const std::vector<glm::vec2>& centers, const glm::vec2& cursor, int direction);

// A horizontal bat swing, right-handed: the bat sweeps from the swinger's
// left, through straight ahead, to the right, around `pivot` (the
// swinger's shoulder). Angle 0 = straight ahead along `forward`.
struct SwingSettings {
    float swingSeconds = 0.26f;   // the fast part, the only part that hits
    float maxStepSeconds = 0.05f; // longer frames (a hitch) advance the swing by this much, so it's still seen
    float holdSeconds = 0.30f;    // follow-through pose, then the bat goes away
    float startDegrees = -80.0f;  // negative = to the left
    float endDegrees = 95.0f;
    float innerReach = 0.35f;     // hands, from the pivot (m)
    float reach = 1.25f;          // bat tip, from the pivot (m)
    float pivotHeight = 1.15f;    // shoulder height above the ground (m)
    float maxPushSpeed = 9.0f;    // m/s given to what is hit, at full swing speed
    float minPushSpeed = 4.0f;    // m/s at least: a hit always knocks things over, even at the swing's slow ends
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
        float degrees = 0.0f;     // the bat's angle when it hit
    };
    Hit sweep(const glm::vec3& boxMin, const glm::vec3& boxMax) const;

    const SwingSettings& settings() const { return m_settings; }

private:
    enum class Phase { Idle, Swing, Hold };
    float angleAt(float t) const;        // t = seconds into the swing
    float angularSpeedAt(float t) const; // degrees per second at t

    SwingSettings m_settings;
    Phase m_phase = Phase::Idle;
    float m_time = 0.0f;
    float m_angle = 0.0f, m_prevAngle = 0.0f;
    float m_sweptFrom = 0.0f, m_sweptTo = 0.0f; // swing time the last update covered
    bool m_swept = false;        // the last update moved the bat through the swing
    glm::vec3 m_pivot{0.0f};
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f}, m_right{1.0f, 0.0f, 0.0f};
};

} // namespace kke
