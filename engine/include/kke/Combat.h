#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kke {

// Melee combat as rules and data: attacks with wind-up, active and
// recovery phases, health, stamina, poise, blocking, parries, dodges and
// knockdowns. No rendering and no physics engine: the game places each
// fighter every frame (feet and facing, optionally the weapon's tip from
// a bone), asks for attacks and blocks, and reads the hits back to play
// reactions (a flinch, a ragdoll on a knockdown, a sound).
//
//   kke::CombatWorld combat;
//   auto hero = combat.add(0);              // team 0
//   auto goblin = combat.add(1);            // team 1: can hit team 0
//   combat.get(hero).place(feet, facing);   // each frame, per fighter
//   combat.get(hero).attack(kke::AttackDesc::light());
//   for (const kke::HitEvent& h : combat.step(dt)) ...
//
// The fighting rules a player learns (and a bot plays by):
//  - An attack spends stamina up front; with too little it can't start.
//    Stamina comes back after a short pause, slower while blocking.
//  - Blocking faces the hit (within `blockAngle`) takes a little chip
//    damage and costs stamina instead of health; at zero stamina the
//    guard breaks and the blocker is stunned.
//  - Raising the block just before a hit (`parryWindow`) parries it: no
//    damage, and the attacker is stunned instead.
//  - Every hit also wears down poise; at zero the target is knocked down
//    (the game ragdolls it) and gets up after `knockdownTime`.
//  - A dodge is a short moment no attack lands, for stamina.
// Unit-tested in tests/test_combat.cpp.

struct AttackDesc {
    std::string name = "attack";
    float windup = 0.28f, active = 0.12f, recovery = 0.32f; // seconds
    float damage = 10.0f;
    float staminaCost = 12.0f;
    float poiseDamage = 18.0f;
    // Where it lands, relative to the attacker's feet and facing, when
    // the game doesn't give a weapon point: `reach` m in front, `height`
    // m up, a sphere of `radius`.
    float reach = 1.0f, height = 1.25f, radius = 0.4f;
    float knockback = 1.5f;        // m/s pushed away on a clean hit
    float hitStun = 0.35f;         // s the target can't act
    float blockStun = 0.18f;       // s a blocker is held
    float chip = 0.15f;            // share of the damage a block lets through
    float guardDamage = 16.0f;     // stamina a block costs
    bool unblockable = false;      // a grab, a charged slam
    bool sweep = false;            // hits everyone in range (a wide swing), else only the first

    // Presets for a person-sized fighter (the demos use these).
    static AttackDesc light();     // quick jab / slash
    static AttackDesc heavy();     // slow, hurts, breaks poise
    static AttackDesc kick();      // guard breaker: mostly stamina and poise
};

struct CombatStats {
    float maxHealth = 100.0f;
    float maxStamina = 100.0f;
    float staminaRegen = 28.0f;    // per second
    float regenDelay = 0.6f;       // s after spending stamina before it comes back
    float blockRegenScale = 0.35f; // regen while blocking
    float maxPoise = 45.0f;
    float poiseRegen = 15.0f;      // per second, after `poiseDelay` without a hit
    float poiseDelay = 1.5f;
    float blockAngle = 75.0f;      // degrees either side of facing a block covers
    float parryWindow = 0.15f;     // s
    float parryStun = 0.8f;        // s the parried attacker is stunned
    float guardBreakStun = 1.1f;   // s
    float dodgeTime = 0.3f, dodgeCost = 18.0f;
    float knockdownTime = 2.2f;    // s on the ground before getting up
    float radius = 0.35f, height = 1.8f; // hurt capsule, from the feet up
    // Presets.
    static CombatStats fighter();  // a duellist
    static CombatStats grunt();    // a horde goblin: little health, little poise
};

enum class HitOutcome : uint8_t {
    Hit,        // took the damage, stunned
    Blocked,    // chip damage, stamina spent
    GuardBroke, // blocked, but out of stamina: stunned for long
    Parried,    // no damage; the attacker is stunned
    Knockdown,  // poise gone: on the ground (ragdoll it)
    Killed,     // health gone
};
const char* toString(HitOutcome o);

using CombatantId = uint32_t; // 0 = none

struct HitEvent {
    CombatantId attacker = 0, target = 0;
    HitOutcome outcome = HitOutcome::Hit;
    float damage = 0.0f;           // health taken
    glm::vec3 point{0.0f};         // where it landed (world)
    glm::vec3 push{0.0f};          // velocity to add to the target (knockback, m/s)
    std::string attack;            // AttackDesc::name
    // Height of the hit on the target: 0 = feet, 1 = top of the head.
    float heightFraction = 0.5f;
};

class Combatant {
public:
    enum class State : uint8_t { Idle, Windup, Active, Recovery, Stunned, Dodging, Knockdown, Dead };

    Combatant(CombatantId id, int team, const CombatStats& stats);

    // ---- the game, every frame (before CombatWorld::step)
    // Feet position and facing (horizontal, any length).
    void place(const glm::vec3& feet, const glm::vec3& facing);
    // The weapon's tip / fist in the world while an attack is active (from
    // a hand bone); none = the attack's reach/height in front.
    void setStrikePoint(const glm::vec3& p) { m_strike = p; m_hasStrike = true; }
    void clearStrikePoint() { m_hasStrike = false; }

    // ---- actions (false = not now: busy, stunned, down or tired)
    bool attack(const AttackDesc& a);
    void setBlocking(bool on);
    bool dodge();
    // Back up early from a knockdown (e.g. once a ragdoll has settled).
    void getUp();
    // Everything back to full (a new round).
    void reset();
    // Health back, up to the maximum (a potion, a breather between waves).
    // Nothing for the dead.
    void heal(float amount);

    // ---- what the game draws and animates from
    CombatantId id() const { return m_id; }
    int team() const { return m_team; }
    State state() const { return m_state; }
    bool alive() const { return m_state != State::Dead; }
    bool canAct() const { return m_state == State::Idle && m_blockHold <= 0.0f; }
    bool blocking() const { return m_blocking && (m_state == State::Idle); }
    // The current (or last) attack, and how far through its current phase.
    const AttackDesc& currentAttack() const { return m_attack; }
    float stateTime() const { return m_stateTime; }
    float stateLength() const;
    float health() const { return m_health; }
    float stamina() const { return m_stamina; }
    float poise() const { return m_poise; }
    float healthFraction() const { return m_health / m_stats.maxHealth; }
    float staminaFraction() const { return m_stamina / m_stats.maxStamina; }
    const CombatStats& stats() const { return m_stats; }
    CombatStats& stats() { return m_stats; }
    glm::vec3 feet() const { return m_feet; }
    glm::vec3 facing() const { return m_facing; }
    // The attack's hit sphere right now (valid while Active).
    glm::vec3 strikeCenter() const;
    // Seconds of wind-up left (0 when not winding up): what a bot reads to
    // decide when to block.
    float windupLeft() const { return m_state == State::Windup ? m_attack.windup - m_stateTime : 0.0f; }

    // CombatWorld drives these.
    void update(float dt);
    HitEvent receive(const Combatant& attacker, const AttackDesc& a, const glm::vec3& point);
    void stun(float seconds);
    bool hitThisSwing(CombatantId target) const;
    void markHit(CombatantId target) { m_hitThisSwing.push_back(target); }

private:
    void enter(State s);
    void spend(float stamina);

    CombatantId m_id;
    int m_team;
    CombatStats m_stats;
    State m_state = State::Idle;
    float m_stateTime = 0.0f, m_stunLength = 0.0f;
    float m_health, m_stamina, m_poise;
    float m_sinceSpend = 10.0f, m_sinceHit = 10.0f;
    bool m_blocking = false;
    float m_blockTime = 10.0f; // since the block went up
    float m_blockHold = 0.0f;  // block stun left: can't attack or dodge
    AttackDesc m_attack;
    std::vector<CombatantId> m_hitThisSwing;
    glm::vec3 m_feet{0.0f}, m_facing{0.0f, 0.0f, -1.0f};
    glm::vec3 m_strike{0.0f};
    bool m_hasStrike = false;
};

// Everyone in a fight: advances them and resolves active attacks against
// the other teams' hurt capsules. A uniform grid keeps a horde cheap: an
// attack only tests fighters in the cells its sphere touches.
class CombatWorld {
public:
    explicit CombatWorld(float cellSize = 2.0f) : m_cell(cellSize) {}
    CombatantId add(int team, const CombatStats& stats = CombatStats{});
    void remove(CombatantId id);
    Combatant& get(CombatantId id);
    const Combatant& get(CombatantId id) const;
    Combatant* find(CombatantId id);
    size_t size() const;
    // Every fighter's timers, then every active attack. Returns what hit
    // what this step (in the order they happened).
    const std::vector<HitEvent>& step(float dt);
    // Friendly fire (same team hits); off by default.
    void setFriendlyFire(bool on) { m_friendlyFire = on; }
    // Everyone alive within `radius` of `p` (for bots: who's near me).
    void query(const glm::vec3& p, float radius, std::vector<CombatantId>& out) const;
    // Pair tests done last step (how well the grid is culling).
    size_t testsLastStep() const { return m_tests; }

private:
    void rebuildGrid();
    long long cellKey(int x, int z) const { return (static_cast<long long>(x) << 32) ^ static_cast<unsigned int>(z); }

    float m_cell;
    std::vector<std::unique_ptr<Combatant>> m_all; // index = id - 1 (null = removed)
    std::vector<HitEvent> m_events;
    struct Cell { long long key; CombatantId id; };
    std::vector<Cell> m_grid; // sorted by key
    bool m_friendlyFire = false;
    size_t m_tests = 0;
};

// Distance from a point to a vertical capsule (feet, radius, height),
// negative inside. Also gives the height fraction of the closest point.
float distanceToCapsule(const glm::vec3& p, const glm::vec3& feet, float radius, float height, float* heightFraction = nullptr);

} // namespace kke
