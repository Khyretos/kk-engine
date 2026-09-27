#pragma once

#include "kke/ai/Perception.h"
#include "kke/ai/Steering.h"
#include "kke/ai/Utility.h"

#include <nlohmann/json_fwd.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke::ai {

class NavMesh;

// The AI core: animals, enemies, companions and soldiers all run on this
// (docs/AI.md). One AiWorld holds every agent in a level and, each
// update, runs the same four layers for each one:
//
//   perceive  sight / hearing / smell / touch -> awareness of others (Perception.h)
//   need      hunger, thirst, tiredness rise over time; eating, drinking, resting lower them
//   decide    utility AI picks the action that scores highest (Utility.h);
//             a player's order is an action that wins while it stands
//   move      the action's behaviour steers (Steering.h), along navmesh
//             paths when there is a NavMesh (NavMesh.h)
//
// What an agent is (a sheep, a goblin) is its Species: senses, speeds,
// temperament, who it fears / hunts / likes, what it eats, and its list
// of utility actions (built from the rest when not given). Species load
// from JSON or YAML (loadSpecies) and have built-in defaults for the farm
// (builtinSpecies).
//
// The game owns bodies, meshes and animation. Ids are the game's own
// (the sandbox's thing ids, an ECS entity): add an agent with an id and a
// position; each frame read back position, velocity, yaw and `anim`
// (idle / walk / run / eat / rest / alert / sniff / attack), or, when
// the game moves the body itself (a character controller, a ragdoll), read
// `desiredVelocity` and call setTransform. Players and anything else the
// AI doesn't drive are *actors*: perceived, never moved.
//
// Deterministic for a given seed and update sequence (its own random
// generator; no clocks), so tests and replays see the same choices.
// Pure CPU, no GPU: unit-tested in tests/test_ai.cpp.

using AgentId = uint32_t;

// How one agent feels about another.
enum class Attitude : uint8_t {
    Ignore,   // doesn't care
    Friendly, // same herd, same team, its owner
    Curious,  // goes to have a look (from a safe distance)
    Fear,     // runs away
    Hostile,  // stands its ground and attacks (a goose at a dog, an enemy soldier)
    Hunt,     // chases it (a fox and a chicken)
};
const char* attitudeName(Attitude a);
bool attitudeFromName(const std::string& name, Attitude& out);

struct Temperament {
    float boldness = 0.3f;    // 0 flees at first sight, 1 never flees
    float curiosity = 0.4f;   // goes to look at unknown things
    float aggression = 0.1f;  // > 0.6: attacks what it fears instead of fleeing
    float sociability = 0.6f; // stays with its herd
};

// A need rises by `perSecond` (0..1 per second) and is lowered by the
// action that satisfies it. The built-in ones: hunger (graze), thirst
// (drink), tiredness (rest). Species can add their own; scripts read and
// set them (ai.need / ai.setNeed).
struct NeedDef {
    std::string name;
    float perSecond = 1.0f / 300.0f;
    float start = 0.2f;
};

struct Species {
    std::string id;     // "sheep"
    std::string label;  // "Sheep"
    float walkSpeed = 1.2f;  // m/s
    float runSpeed = 5.0f;
    float acceleration = 8.0f;
    float turnRate = 360.0f; // degrees per second
    float radius = 0.4f;
    Senses senses;
    Temperament temperament;
    // Species ids (or "*" for everything else).
    std::vector<std::string> fears, hunts, friends, hostileTo;
    // Place kinds it eats and drinks from ("grass", "grain", "water").
    std::vector<std::string> eats{ "grass" };
    std::vector<std::string> drinks{ "water" };
    float fearDistance = 12.0f;       // runs until the threat is this far
    float investigateDistance = 3.0f; // how close a curious look gets
    float attackRange = 1.2f;
    float attackCooldown = 1.5f;
    float homeRadius = 25.0f;         // wanders this far from where it was put
    bool flocks = false;              // herd / flock / pack with its own kind
    float flockRadius = 6.0f;
    FlockWeights flock;
    // Footsteps others hear (metres, see Noise), walking and running.
    float footstepsWalk = 1.5f, footstepsRun = 5.0f;
    // Smells to others this strongly (0 = no scent trail).
    float scent = 1.0f;
    std::vector<NeedDef> needs{ { "hunger", 1.0f / 240.0f, 0.3f }, { "thirst", 1.0f / 360.0f, 0.2f }, { "tiredness", 1.0f / 600.0f, 0.1f } };
    // Empty: defaultActions(*this) when defined.
    std::vector<UtilityAction> actions;
};

// The standard action set, from a species' temperament and lists (see
// docs/AI.md "Actions"): orders, flee, fight, hunt, defend, investigate,
// watch, graze, drink, rest, regroup, wander, idle.
std::vector<UtilityAction> defaultActions(const Species& s);
// sheep, cow, pig, chicken, horse, goose, cat, dog, fox, farmer: the farm.
std::vector<Species> builtinSpecies();

// From / to JSON (the same shape in YAML). Missing keys keep defaults.
bool speciesFromJson(const nlohmann::json& j, Species& out, std::string* error = nullptr);
nlohmann::json speciesToJson(const Species& s);

// A player's (or a script's) instruction. Wins over everything the agent
// wants on its own while it stands.
struct Order {
    enum class Kind : uint8_t {
        None,
        MoveTo,   // go to `position`, then hold there
        Follow,   // stay within `distance` of `target`
        Attack,   // go for `target` until it is gone or the order changes
        Hold,     // stay at `position` (facing and engaging what comes)
        Flee,     // run from `target` (or `position`)
        Interact, // go to `target`; fires Arrived and ends (fetch, pet, open)
    };
    Kind kind = Kind::None;
    AgentId target = 0;
    glm::vec3 position{0.0f};
    float distance = 2.5f; // Follow: how close; MoveTo/Interact: arrival radius
    bool run = false;      // hurry
};
const char* orderKindName(Order::Kind k);
bool orderKindFromName(const std::string& name, Order::Kind& out);

struct Agent {
    AgentId id = 0;
    int species = -1; // index into AiWorld's species
    bool actor = false;   // perceived only; the game moves it (the player)
    bool enabled = true;  // false: ragdolled, asleep, carried: no thinking, no moving
    uint32_t team = 0;    // nonzero: same team = Friendly, other nonzero team = Hostile

    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f; // degrees, 0 = +z, 90 = +x
    glm::vec3 home{0.0f};

    // What the AI wants this frame (read these when the game moves the body).
    glm::vec3 desiredVelocity{0.0f};
    std::string anim = "idle";
    AgentId focus = 0;          // what it's attending to (fleeing, chasing, watching)
    glm::vec3 lookAt{0.0f};
    bool hasLookAt = false;

    Temperament mood;           // starts as the species' temperament
    Conspicuity conspicuity;
    bool autoConspicuity = true; // derive visibility from speed
    std::vector<std::pair<std::string, float>> needs;
    std::vector<Awareness> memory;
    std::unordered_map<AgentId, Attitude> attitudeOverrides;

    Order order;
    int action = -1;            // index into the species' actions
    float actionScore = 0.0f;
    float actionTime = 0.0f;    // seconds in the current action

    // Internals (movement and timers); stable enough to show in debug views.
    std::vector<glm::vec3> path;
    size_t pathIndex = 0;
    glm::vec3 pathGoal{0.0f};
    float repathTimer = 0.0f;
    float wanderAngle = 0.0f;
    float thinkTimer = 0.0f;
    float perceiveTimer = 0.0f;
    float attackTimer = 0.0f;
    float restless = 0.5f;
    float restlessTimer = 0.0f;
    glm::vec3 fleeGoal{0.0f};
    float fleeTimer = 0.0f;
    std::vector<float> cooldownUntil;
    uint64_t lastNoise = 0;
    // Values scripts and games set for their own considerations
    // (ai.setInput(thing, "health", 0.3)); read before the built-in inputs.
    std::vector<std::pair<std::string, float>> inputs;
};

struct AiEvent {
    enum class Kind : uint8_t {
        Spotted,       // who became aware of other (awareness passed "spotted")
        Lost,          // who lost track of other
        Heard,         // who heard a noise at position (other = who made it)
        Scared,        // who started fleeing from other
        Calmed,        // who stopped fleeing
        Attack,        // who attacks other (in range, cooldown over): the game deals the damage
        Arrived,       // who reached its order's goal (other = the Interact target)
        ActionChanged, // who switched to `action`
    };
    Kind kind = Kind::Spotted;
    AgentId who = 0;
    AgentId other = 0;
    glm::vec3 position{0.0f};
    std::string action;
};
const char* eventKindName(AiEvent::Kind k);

// A place animals use: food, water, a bed, cover.
struct Place {
    uint32_t id = 0;
    std::string kind; // "grass", "grain", "water", "bed", ...
    glm::vec3 position{0.0f};
    float radius = 1.5f;
};

class AiWorld {
public:
    explicit AiWorld(uint64_t seed = 1);

    // ---- Species
    // Adds or replaces (by id). Empty `actions` get defaultActions.
    void defineSpecies(Species s);
    const Species* species(const std::string& id) const;
    const Species* speciesOf(AgentId id) const;
    std::vector<std::string> speciesIds() const;
    // A JSON or YAML file (kke/DataFile.h): one species object, a list of
    // them, or { "species": [ ... ] }. Returns how many were defined.
    int loadSpecies(const std::filesystem::path& file, std::string* error = nullptr);

    // ---- Agents
    // False when the id is taken or the species unknown.
    bool addAgent(AgentId id, const std::string& species, const glm::vec3& position, float yawDegrees = 0.0f);
    // Perceived by agents, never moved or thinking (a player, a car).
    bool addActor(AgentId id, const std::string& species, const glm::vec3& position);
    bool remove(AgentId id);
    bool has(AgentId id) const;
    Agent* agent(AgentId id);
    const Agent* agent(AgentId id) const;
    const std::vector<Agent>& agents() const { return m_agents; }
    // The game moved it (an actor every frame; an agent after physics).
    void setTransform(AgentId id, const glm::vec3& position, const glm::vec3& velocity, float yawDegrees);
    void setEnabled(AgentId id, bool enabled);
    void setTeam(AgentId id, uint32_t team);
    void setAttitude(AgentId who, AgentId towards, Attitude a);
    void clearAttitude(AgentId who, AgentId towards);
    Attitude attitude(AgentId who, AgentId towards) const;

    // ---- Orders
    void order(AgentId id, const Order& o);
    void clearOrder(AgentId id);

    // ---- Needs and mood
    float need(AgentId id, const std::string& name) const; // -1 when unknown
    bool setNeed(AgentId id, const std::string& name, float value);
    bool setMood(AgentId id, const Temperament& t);
    // A custom input for this agent's considerations (see Agent::inputs).
    bool setInput(AgentId id, const std::string& name, float value);

    // ---- The world around them
    void setNavMesh(const NavMesh* mesh) { m_nav = mesh; }
    const NavMesh* navMesh() const { return m_nav; }
    uint32_t addPlace(const std::string& kind, const glm::vec3& position, float radius = 1.5f);
    bool removePlace(uint32_t id);
    const std::vector<Place>& places() const { return m_places; }
    void addObstacle(const CircleObstacle& o) { m_obstacles.push_back(o); }
    void clearObstacles() { m_obstacles.clear(); }
    ScentField& scent() { return m_scent; }
    const ScentField& scent() const { return m_scent; }
    // Heard by every agent in range at the next perception tick.
    void makeNoise(const Noise& n);

    // Clear line of sight between two eye positions (the game casts a ray
    // against its level). Unset = always clear.
    std::function<bool(const glm::vec3& from, const glm::vec3& to)> lineOfSight;
    // Ground height at x, z (when there's no NavMesh). False = keep y.
    std::function<bool(float x, float z, float& y)> groundHeight;

    // ---- Running
    float perceptionInterval = 0.1f; // seconds (staggered across agents)
    float thinkInterval = 0.25f;
    void update(float dt);
    // Events since the last call, oldest first.
    std::vector<AiEvent> takeEvents();
    double time() const { return m_time; }

    // ---- Why (debug overlays, the node editor, tests)
    // The inputs utility considerations read, for one agent right now.
    float input(AgentId id, const std::string& name) const;
    std::vector<float> actionScores(AgentId id) const;
    std::string actionName(AgentId id) const;
    // One line: "sheep 12 flee 0.83 (fear 0.9) from 3".
    std::string describe(AgentId id) const;

    // Random number in [0, 1) from the world's generator.
    float random01();

private:
    int speciesIndex(const std::string& id) const;
    size_t indexOf(AgentId id) const; // SIZE_MAX when missing
    void rebuildGrid();
    void perceive(Agent& a, float dt);
    void think(Agent& a);
    void act(Agent& a, float dt);
    void move(Agent& a, const glm::vec3& steering, float speedLimit, float dt);
    glm::vec3 followPath(Agent& a, const glm::vec3& goal, float arriveRadius, float speed, float dt);
    bool pickFleeGoal(Agent& a, const glm::vec3& threat, glm::vec3& out);
    const Awareness* strongest(const Agent& a, Attitude kind, float* score = nullptr) const;
    const Place* nearestPlace(const Agent& a, const std::vector<std::string>& kinds, float maxDistance) const;
    float computeInput(const Agent& a, const std::string& name) const;
    void emit(AiEvent e) { m_events.push_back(std::move(e)); }
    void neighbours(const Agent& a, float radius, bool sameSpeciesOnly, std::vector<Neighbour>& out) const;

    uint64_t m_rng;
    double m_time = 0.0;
    std::vector<Species> m_species;
    std::vector<Agent> m_agents;
    std::unordered_map<AgentId, size_t> m_index;
    std::vector<Place> m_places;
    uint32_t m_nextPlace = 1;
    std::vector<CircleObstacle> m_obstacles;
    struct TimedNoise {
        Noise noise;
        double time = 0.0;
        uint64_t serial = 0;
    };
    std::vector<TimedNoise> m_noises;
    uint64_t m_noiseSerial = 0;
    ScentField m_scent;
    const NavMesh* m_nav = nullptr;
    SpatialGrid m_grid{ 6.0f };
    std::vector<AiEvent> m_events;
    mutable std::vector<uint32_t> m_query;
};

} // namespace kke::ai
