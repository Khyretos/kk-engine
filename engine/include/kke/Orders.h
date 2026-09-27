#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace kke {

// Orders: what the player tells companions and squads to do (docs/COMMANDS.md).
//
// This is the command layer only: which units are selected, what a click,
// a tap or a radial-menu pick means, where each unit of a squad should
// stand, and each unit's standing order. It does not move anyone. Brains
// (the AI core, docs/AI.md) read a unit's current order from the
// OrderBoard and report back when it is done or failed; the pet companion
// and platoon demos show both halves together.
//
// Pure logic, no rendering or input devices, so the same orders come from
// a mouse, a controller, a finger, the node graph and Lua (the `order.*`
// bindings, kke/OrderScript.h). Tested in tests/test_orders.cpp.

enum class OrderKind : uint8_t {
    None,
    Move,      // go to a point (a squad spreads into a formation there)
    Follow,    // stay with a leader (the player: "come", "heel")
    Stay,      // hold this spot; defend it, don't chase
    Attack,    // engage this target (each unit may pick others once it is down)
    FocusFire, // everyone on this one target and nothing else until it is down
    Fetch,     // bring this thing back to whoever gave the order
    Drop,      // let go of what you carry
    Sit,
    Pet,       // the issuer is petting this unit: stand still, enjoy it
    Regroup,   // gather on the issuer (or a point) in formation
    Free,      // no order: do what you like (wander, play, guard)
};

// Stable name for files, Lua and logs: "move", "focus", ... ("" for None).
const char* orderName(OrderKind kind);
// Accepts orderName() spellings; OrderKind::None if unknown.
OrderKind orderFromName(const std::string& name);
// What a button or the radial menu shows: "Go there", "Focus fire", ...
const char* orderLabel(OrderKind kind);
// Needs a thing (Attack, FocusFire, Fetch, Follow, Pet) / a point (Move).
bool orderNeedsTarget(OrderKind kind);
bool orderNeedsPoint(OrderKind kind);

// ---------------------------------------------------------------- formations

enum class Formation : uint8_t {
    Line,   // side by side, facing the way the order faces
    Wedge,  // a V with the first unit at the point
    Column, // in single file, two abreast past four units
    Circle, // around the point (Regroup, Stay with no facing)
};
const char* formationName(Formation f);
Formation formationFromName(const std::string& name, Formation fallback = Formation::Wedge);

// Where `count` units stand in formation `f` around `center`, facing
// `yawDegrees` (CameraRig's convention: yaw 0 faces -Z, 90 faces +X; see
// yawForward). `spacing` metres between neighbours.
// Slot 0 is the front/leader slot. Heights are `center.y`.
glm::vec3 yawForward(float yawDegrees); // (sin, 0, -cos)
float yawOf(const glm::vec3& direction); // inverse of yawForward (XZ only); 0 for a zero vector
std::vector<glm::vec3> formationSlots(Formation f, int count, const glm::vec3& center, float yawDegrees, float spacing);

// Which slot each unit takes so the squad travels the least in total
// (Hungarian method, exact; units and slots matched by index, sizes
// equal). result[unit] = slot. Paths then rarely cross.
std::vector<int> assignSlots(const std::vector<glm::vec3>& units, const std::vector<glm::vec3>& slots);

// ---------------------------------------------------------------- following without getting in the way

// A companion that follows must never block the player: it keeps to one
// side and a little behind, stays on the side it is already on (no
// crossing in front), and moves further aside the faster the player goes.
struct FollowSettings {
    float distance = 1.8f;    // from the leader, when the leader stands still
    float sideAngle = 125.0f; // degrees from the leader's heading: 90 = level with them, 180 = right behind
    float runAside = 0.6f;    // extra metres to the side at `runSpeed`
    float runSpeed = 5.0f;    // m/s
    float pathWidth = 0.9f;   // metres either side of the leader's path that count as "in the way"
    float lookAhead = 1.5f;   // seconds of the leader's path to keep clear
};
// Where the follower should be. `heading` is the leader's facing (yaw,
// degrees) and only matters when they stand still; moving, their
// velocity decides.
glm::vec3 followSlot(const glm::vec3& leader, const glm::vec3& leaderVelocity, float headingDegrees, const glm::vec3& follower,
                     const FollowSettings& s = {});
// Whether `point` is where the leader will walk in the next `lookAhead`
// seconds (within `pathWidth` of the path). A brain uses it to step aside.
bool inLeadersWay(const glm::vec3& leader, const glm::vec3& leaderVelocity, const glm::vec3& point, const FollowSettings& s = {});

// ---------------------------------------------------------------- selection

// Who the next order goes to. Keeps insertion order (the first selected
// leads a formation). Control groups 1-9 as in every RTS: Ctrl+1 stores,
// 1 recalls.
class Selection {
public:
    void clear() { m_ids.clear(); }
    void select(uint32_t id);           // only this one
    void set(std::vector<uint32_t> ids);
    void add(uint32_t id);
    void toggle(uint32_t id);           // Shift+click
    void remove(uint32_t id);           // also from every group (a unit died)
    bool contains(uint32_t id) const;
    bool empty() const { return m_ids.empty(); }
    size_t size() const { return m_ids.size(); }
    const std::vector<uint32_t>& ids() const { return m_ids; }

    void storeGroup(int group);         // 1..9; others ignored
    bool recallGroup(int group);        // false if empty (the selection is kept)
    const std::vector<uint32_t>& group(int group) const;

private:
    std::vector<uint32_t> m_ids;
    std::vector<uint32_t> m_groups[10];
};

// Units whose screen position lies in the rectangle between two corners
// (any order), for a drag-box (mouse) or a two-corner touch selection.
struct ScreenUnit { uint32_t id = 0; glm::vec2 screen{0.0f}; bool onScreen = true; };
std::vector<uint32_t> unitsInRect(const std::vector<ScreenUnit>& units, const glm::vec2& a, const glm::vec2& b);
// The unit nearest `point` within `radius` pixels, 0 if none (tap/click).
uint32_t unitNear(const std::vector<ScreenUnit>& units, const glm::vec2& point, float radius);

// ---------------------------------------------------------------- what a click means

// What the pointer (mouse, finger, a controller's reticle) is on.
enum class Relation : uint8_t {
    Ground,  // nothing: a place
    Self,    // the player
    Own,     // one of the player's units
    Friend,  // on our side, not ours to command
    Hostile,
    Item,    // something that can be picked up (a ball, a stick, ammo)
};
struct PointerTarget {
    Relation relation = Relation::Ground;
    uint32_t thing = 0;
    glm::vec3 point{0.0f};
};
// What the units can do: a pet fetches but doesn't fight; a soldier
// fights but doesn't fetch (a soldier sent to an item goes there).
struct UnitAbilities {
    bool attack = true;
    bool fetch = false;
};
struct PointerModifiers {
    bool force = false; // Ctrl / a held button: Attack becomes FocusFire, ground becomes Stay there
    bool queue = false; // Shift: after the current order instead of instead of it
};
// The one obvious order for pointing at something with units selected
// (right click, tap, controller "go"): hostile -> Attack (force:
// FocusFire), item -> Fetch (or Move if they can't fetch), the player or a
// friend -> Follow, ground -> Move (force: Stay there). Own unit -> Follow
// it. Units empty -> OrderKind::None.
struct Order;
Order contextOrder(const std::vector<uint32_t>& units, const PointerTarget& target, const UnitAbilities& can,
                   const PointerModifiers& mods = {}, uint32_t issuer = 0);

// ---------------------------------------------------------------- the radial command wheel

// A ring of choices picked with a stick (controller), a drag (mouse held,
// finger held): item 0 at the top, then clockwise. The direction picks;
// near the centre nothing is picked. The pick is sticky within a few
// degrees of the border so a worn stick doesn't flicker between two.
class RadialMenu {
public:
    explicit RadialMenu(int items = 8) : m_items(items) {}
    void setItems(int items) { m_items = items; m_picked = -1; }
    int items() const { return m_items; }
    // Stick in -1..1 (y down, SDL's convention); returns the picked item, -1 = none.
    int updateStick(const glm::vec2& stick, float deadzone = 0.45f);
    // Pointer offset from where the wheel opened, in pixels.
    int updatePointer(const glm::vec2& offset, float deadzonePixels = 28.0f);
    int picked() const { return m_picked; }
    void reset() { m_picked = -1; }
    // Unit direction of item `i` on screen (y down), for drawing it.
    glm::vec2 direction(int i) const;
    float hysteresisDegrees = 6.0f;

private:
    int pick(const glm::vec2& dir);
    int m_items;
    int m_picked = -1;
};

// ---------------------------------------------------------------- orders and the board

struct Order {
    OrderKind kind = OrderKind::None;
    std::vector<uint32_t> units;   // who it is for
    uint32_t target = 0;           // Attack/FocusFire/Fetch/Follow/Pet
    glm::vec3 point{0.0f};         // Move/Stay/Regroup (Stay without one: where each unit stands)
    bool hasPoint = false;
    float yawDegrees = 0.0f;       // facing at the point (formations)
    bool hasYaw = false;
    Formation formation = Formation::Wedge;
    float spacing = 1.6f;          // metres between units in formation
    uint32_t issuer = 0;           // the player (or unit) that gave it; Fetch brings things to them
    bool queue = false;            // after the current order instead of replacing it
};

// One unit's share of an order: the unit's own slot, the shared target.
struct UnitOrder {
    uint64_t id = 0;               // the order it came from (shared by the squad)
    OrderKind kind = OrderKind::None;
    uint32_t target = 0;
    glm::vec3 point{0.0f};         // this unit's slot for Move/Stay/Regroup
    bool hasPoint = false;
    float yawDegrees = 0.0f;
    bool hasYaw = false;
    uint32_t issuer = 0;
    std::vector<uint32_t> squad;   // everyone the order went to (FocusFire: who else is shooting)
    enum class Status : uint8_t { Given, Running, Done, Failed };
    Status status = Status::Given;
};

// Every unit's current order and queue. Give orders with issue(); a brain
// reads current(), marks it running, then done or failed, and the next
// queued order (or Free) takes over. Listeners hear every change, which is
// how the "Ordered" / "OrderDone" events and the HUD learn of them.
class OrderBoard {
public:
    // Where a unit is now (formation slots are assigned by distance).
    // Unknown units: return the order's point.
    using PositionFn = std::function<glm::vec3(uint32_t unit)>;
    explicit OrderBoard(PositionFn position = {});
    void setPositionFn(PositionFn position) { m_position = std::move(position); }

    // Validates (a Fetch needs a target, ...), splits it per unit
    // (formation slots for Move/Regroup, and for Stay with a point) and
    // gives it. Returns the order id, 0 if it was invalid (`why` says).
    uint64_t issue(const Order& order, std::string* why = nullptr);

    const UnitOrder* current(uint32_t unit) const;
    OrderKind currentKind(uint32_t unit) const;
    size_t queued(uint32_t unit) const; // orders waiting after the current one
    void markRunning(uint32_t unit);
    // The current order ended; the next queued one starts (or none).
    void complete(uint32_t unit, bool ok = true);
    void cancel(uint32_t unit);        // current and queued orders go; the unit is free
    void forget(uint32_t unit);        // the unit is gone (died, removed): no events after this
    // A thing left the world (a target died, a ball was removed): orders
    // about it end, Attack/FocusFire as done, Fetch/Follow/Pet as failed.
    void targetGone(uint32_t thing);
    std::vector<uint32_t> units() const;

    struct Listener {
        std::function<void(uint32_t unit, const UnitOrder&)> ordered;              // a new current order
        std::function<void(uint32_t unit, const UnitOrder&, bool ok)> finished;    // done / failed
    };
    int listen(Listener l);   // returns a handle for unlisten
    void unlisten(int handle);

private:
    struct Entry { UnitOrder current; std::deque<UnitOrder> queue; bool has = false; };
    void start(uint32_t unit, Entry& e, const UnitOrder& o);
    void advance(uint32_t unit, Entry& e);
    void notifyOrdered(uint32_t unit, const UnitOrder& o);
    void notifyFinished(uint32_t unit, const UnitOrder& o, bool ok);

    PositionFn m_position;
    std::map<uint32_t, Entry> m_units;
    std::map<int, Listener> m_listeners;
    int m_nextListener = 1;
    uint64_t m_nextId = 1;
};

// ---------------------------------------------------------------- reading the player's intent

// A good companion acts before being told: it looks where you look, goes
// ahead the way you are heading, comes to you when you stop near it, and
// gets out of your way. IntentReader turns the player's own movement and
// view into those hints; brains decide what to do with them.
struct IntentSample {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 view{0.0f, 0.0f, -1.0f}; // where the camera/eyes point, unit
};
struct IntentCandidate {
    uint32_t thing = 0;
    glm::vec3 position{0.0f};
    Relation relation = Relation::Item;
};
struct Intent {
    enum class Kind : uint8_t {
        Idle,        // standing still a while: relax, play, wander near
        Walking,
        Running,
        LookingAt,   // looking at `thing` for a moment: be curious about it too
        Approaching, // walking up to `thing` (the companion itself: wants to pet it)
    };
    Kind kind = Kind::Idle;
    uint32_t thing = 0;
    glm::vec3 heading{0.0f};  // where the player will be in `lookAhead` seconds
    float seconds = 0.0f;     // how long this intent has held
};
struct IntentSettings {
    float walkSpeed = 0.5f;       // m/s: slower is standing
    float runSpeed = 4.0f;        // m/s: faster is running
    float idleAfter = 1.5f;       // seconds standing still before Idle
    float lookCone = 8.0f;        // degrees between the view and a thing
    float lookDwell = 0.6f;       // seconds looking before it counts
    float lookRange = 25.0f;      // metres
    float approachCone = 25.0f;   // degrees between the velocity and the way to a thing
    float approachRange = 5.0f;   // metres
    float lookAhead = 1.5f;       // seconds
};
class IntentReader {
public:
    explicit IntentReader(IntentSettings s = {}) : m_s(s) {}
    const Intent& update(const IntentSample& player, const std::vector<IntentCandidate>& things, float dt);
    const Intent& intent() const { return m_intent; }

private:
    IntentSettings m_s;
    Intent m_intent;
    uint32_t m_lookThing = 0;
    float m_lookTime = 0.0f, m_stillTime = 0.0f;
};
const char* intentName(Intent::Kind kind);

} // namespace kke
