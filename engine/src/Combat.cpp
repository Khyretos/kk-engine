#include "kke/Combat.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kke {

AttackDesc AttackDesc::light() {
    AttackDesc a;
    a.name = "light";
    return a;
}

AttackDesc AttackDesc::heavy() {
    AttackDesc a;
    a.name = "heavy";
    a.windup = 0.55f;
    a.active = 0.16f;
    a.recovery = 0.5f;
    a.damage = 24.0f;
    a.staminaCost = 26.0f;
    a.poiseDamage = 40.0f;
    a.reach = 1.15f;
    a.radius = 0.5f;
    a.knockback = 4.0f;
    a.hitStun = 0.6f;
    a.blockStun = 0.35f;
    a.guardDamage = 30.0f;
    a.sweep = true;
    return a;
}

AttackDesc AttackDesc::kick() {
    AttackDesc a;
    a.name = "kick";
    a.windup = 0.32f;
    a.active = 0.12f;
    a.recovery = 0.4f;
    a.damage = 6.0f;
    a.staminaCost = 14.0f;
    a.poiseDamage = 26.0f;
    a.reach = 1.05f;
    a.height = 0.9f;
    a.knockback = 3.0f;
    a.hitStun = 0.45f;
    a.blockStun = 0.4f;
    a.chip = 0.0f;
    a.guardDamage = 45.0f;
    return a;
}

CombatStats CombatStats::fighter() { return CombatStats{}; }

CombatStats CombatStats::grunt() {
    CombatStats s;
    s.maxHealth = 30.0f;
    s.maxStamina = 60.0f;
    s.maxPoise = 12.0f;
    s.knockdownTime = 1.8f;
    s.radius = 0.3f;
    s.height = 1.3f; // goblins are short
    return s;
}

const char* toString(HitOutcome o) {
    switch (o) {
    case HitOutcome::Hit: return "hit";
    case HitOutcome::Blocked: return "blocked";
    case HitOutcome::GuardBroke: return "guard broken";
    case HitOutcome::Parried: return "parried";
    case HitOutcome::Knockdown: return "knockdown";
    case HitOutcome::Killed: return "killed";
    }
    return "?";
}

float distanceToCapsule(const glm::vec3& p, const glm::vec3& feet, float radius, float height, float* heightFraction) {
    // The capsule's core segment runs from feet + r up to feet + h - r.
    const float lo = feet.y + radius, hi = feet.y + std::max(radius, height - radius);
    const float y = std::clamp(p.y, lo, hi);
    if (heightFraction) *heightFraction = height > 0.0f ? std::clamp((p.y - feet.y) / height, 0.0f, 1.0f) : 0.5f;
    return glm::length(p - glm::vec3(feet.x, y, feet.z)) - radius;
}

// ------------------------------------------------------------ Combatant

Combatant::Combatant(CombatantId id, int team, const CombatStats& stats)
    : m_id(id), m_team(team), m_stats(stats), m_health(stats.maxHealth), m_stamina(stats.maxStamina), m_poise(stats.maxPoise) {}

void Combatant::place(const glm::vec3& feet, const glm::vec3& facing) {
    m_feet = feet;
    glm::vec3 f(facing.x, 0.0f, facing.z);
    if (glm::dot(f, f) > 1e-8f) m_facing = glm::normalize(f);
}

void Combatant::enter(State s) {
    m_state = s;
    m_stateTime = 0.0f;
}

void Combatant::spend(float stamina) {
    m_stamina = std::max(0.0f, m_stamina - stamina);
    m_sinceSpend = 0.0f;
}

float Combatant::stateLength() const {
    switch (m_state) {
    case State::Windup: return m_attack.windup;
    case State::Active: return m_attack.active;
    case State::Recovery: return m_attack.recovery;
    case State::Stunned: return m_stunLength;
    case State::Dodging: return m_stats.dodgeTime;
    case State::Knockdown: return m_stats.knockdownTime;
    default: return 0.0f;
    }
}

bool Combatant::attack(const AttackDesc& a) {
    if (m_state != State::Idle || m_blockHold > 0.0f) return false;
    // A tired fighter can still swing with some stamina left, not with none.
    if (m_stamina <= 0.0f || m_stamina < a.staminaCost * 0.5f) return false;
    m_attack = a;
    m_blocking = false;
    m_hitThisSwing.clear();
    spend(a.staminaCost);
    enter(State::Windup);
    return true;
}

void Combatant::setBlocking(bool on) {
    if (on && !m_blocking) m_blockTime = 0.0f;
    m_blocking = on;
}

bool Combatant::dodge() {
    if (m_state != State::Idle || m_blockHold > 0.0f || m_stamina < m_stats.dodgeCost * 0.5f) return false;
    m_blocking = false;
    spend(m_stats.dodgeCost);
    enter(State::Dodging);
    return true;
}

void Combatant::getUp() {
    if (m_state == State::Knockdown) {
        m_poise = m_stats.maxPoise;
        enter(State::Idle);
    }
}

void Combatant::heal(float amount) {
    if (m_state == State::Dead || !(amount > 0.0f)) return;
    m_health = std::min(m_stats.maxHealth, m_health + amount);
}

void Combatant::reset() {
    m_health = m_stats.maxHealth;
    m_stamina = m_stats.maxStamina;
    m_poise = m_stats.maxPoise;
    m_blocking = false;
    m_blockHold = 0.0f;
    m_sinceSpend = m_sinceHit = 10.0f;
    m_hitThisSwing.clear();
    enter(State::Idle);
}

void Combatant::stun(float seconds) {
    if (m_state == State::Dead || m_state == State::Knockdown) return;
    m_stunLength = seconds;
    enter(State::Stunned);
}

bool Combatant::hitThisSwing(CombatantId target) const {
    return std::find(m_hitThisSwing.begin(), m_hitThisSwing.end(), target) != m_hitThisSwing.end();
}

void Combatant::update(float dt) {
    m_stateTime += dt;
    m_sinceSpend += dt;
    m_sinceHit += dt;
    m_blockTime += dt;
    m_blockHold = std::max(0.0f, m_blockHold - dt);
    if (m_state == State::Dead) return;
    if (m_sinceSpend >= m_stats.regenDelay && m_state != State::Knockdown) {
        const float rate = m_stats.staminaRegen * (blocking() ? m_stats.blockRegenScale : 1.0f);
        m_stamina = std::min(m_stats.maxStamina, m_stamina + rate * dt);
    }
    if (m_sinceHit >= m_stats.poiseDelay) m_poise = std::min(m_stats.maxPoise, m_poise + m_stats.poiseRegen * dt);

    const float len = stateLength();
    switch (m_state) {
    case State::Windup:
        if (m_stateTime >= len) enter(State::Active);
        break;
    case State::Active:
        if (m_stateTime >= len) enter(State::Recovery);
        break;
    case State::Recovery:
    case State::Stunned:
    case State::Dodging:
        if (m_stateTime >= len) enter(State::Idle);
        break;
    case State::Knockdown:
        if (m_stateTime >= len) getUp();
        break;
    default: break;
    }
}

glm::vec3 Combatant::strikeCenter() const {
    if (m_hasStrike) return m_strike;
    return m_feet + m_facing * m_attack.reach + glm::vec3(0.0f, m_attack.height, 0.0f);
}

HitEvent Combatant::receive(const Combatant& attacker, const AttackDesc& a, const glm::vec3& point) {
    HitEvent e;
    e.attacker = attacker.id();
    e.target = m_id;
    e.point = point;
    e.attack = a.name;
    distanceToCapsule(point, m_feet, m_stats.radius, m_stats.height, &e.heightFraction);
    glm::vec3 away = m_feet - attacker.feet();
    away.y = 0.0f;
    away = glm::dot(away, away) > 1e-8f ? glm::normalize(away) : -m_facing;

    // Is the hit in front of the block? The attacker is where it comes from.
    const bool facingIt = glm::dot(m_facing, -away) >= std::cos(glm::radians(m_stats.blockAngle));
    if (blocking() && facingIt && !a.unblockable) {
        if (m_blockTime <= m_stats.parryWindow) {
            e.outcome = HitOutcome::Parried;
            return e; // the world stuns the attacker
        }
        e.damage = a.damage * a.chip;
        m_health = std::max(0.0f, m_health - e.damage);
        spend(a.guardDamage);
        e.push = away * a.knockback * 0.5f;
        if (m_health <= 0.0f) {
            e.outcome = HitOutcome::Killed;
            m_blocking = false;
            enter(State::Dead);
        } else if (m_stamina <= 0.0f) {
            e.outcome = HitOutcome::GuardBroke;
            m_blocking = false;
            stun(m_stats.guardBreakStun);
        } else {
            e.outcome = HitOutcome::Blocked;
            m_blockHold = a.blockStun; // held in the block: still blocking, can't strike back yet
            m_blockTime = m_stats.parryWindow + 1.0f;
        }
        return e;
    }

    e.damage = a.damage;
    m_health = std::max(0.0f, m_health - a.damage);
    m_poise -= a.poiseDamage;
    m_sinceHit = 0.0f;
    e.push = away * a.knockback + glm::vec3(0.0f, a.knockback * 0.25f, 0.0f);
    m_blocking = false;
    if (m_health <= 0.0f) {
        e.outcome = HitOutcome::Killed;
        enter(State::Dead);
    } else if (m_poise <= 0.0f) {
        e.outcome = HitOutcome::Knockdown;
        m_poise = 0.0f;
        enter(State::Knockdown);
    } else {
        e.outcome = HitOutcome::Hit;
        stun(a.hitStun);
    }
    return e;
}

// ------------------------------------------------------------ CombatWorld

CombatantId CombatWorld::add(int team, const CombatStats& stats) {
    const CombatantId id = static_cast<CombatantId>(m_all.size() + 1);
    m_all.push_back(std::make_unique<Combatant>(id, team, stats));
    return id;
}

void CombatWorld::remove(CombatantId id) {
    if (id >= 1 && id <= m_all.size()) m_all[id - 1].reset();
}

Combatant* CombatWorld::find(CombatantId id) {
    return id >= 1 && id <= m_all.size() ? m_all[id - 1].get() : nullptr;
}

Combatant& CombatWorld::get(CombatantId id) {
    Combatant* c = find(id);
    if (!c) throw std::out_of_range("CombatWorld: no combatant " + std::to_string(id));
    return *c;
}

const Combatant& CombatWorld::get(CombatantId id) const { return const_cast<CombatWorld*>(this)->get(id); }

size_t CombatWorld::size() const {
    return static_cast<size_t>(std::count_if(m_all.begin(), m_all.end(), [](const auto& c) { return c != nullptr; }));
}

void CombatWorld::rebuildGrid() {
    m_grid.clear();
    for (const auto& c : m_all) {
        if (!c || !c->alive()) continue;
        const glm::vec3 f = c->feet();
        m_grid.push_back({ cellKey(static_cast<int>(std::floor(f.x / m_cell)), static_cast<int>(std::floor(f.z / m_cell))), c->id() });
    }
    std::sort(m_grid.begin(), m_grid.end(), [](const Cell& a, const Cell& b) { return a.key < b.key; });
}

void CombatWorld::query(const glm::vec3& p, float radius, std::vector<CombatantId>& out) const {
    out.clear();
    const int x0 = static_cast<int>(std::floor((p.x - radius) / m_cell)), x1 = static_cast<int>(std::floor((p.x + radius) / m_cell));
    const int z0 = static_cast<int>(std::floor((p.z - radius) / m_cell)), z1 = static_cast<int>(std::floor((p.z + radius) / m_cell));
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            const long long key = cellKey(x, z);
            auto it = std::lower_bound(m_grid.begin(), m_grid.end(), key, [](const Cell& c, long long k) { return c.key < k; });
            for (; it != m_grid.end() && it->key == key; ++it) {
                const Combatant& c = *m_all[it->id - 1];
                glm::vec3 d = c.feet() - p;
                d.y = 0.0f;
                if (glm::length(d) <= radius) out.push_back(it->id);
            }
        }
}

const std::vector<HitEvent>& CombatWorld::step(float dt) {
    m_events.clear();
    m_tests = 0;
    for (auto& c : m_all)
        if (c) c->update(dt);
    rebuildGrid();

    std::vector<CombatantId> near;
    for (auto& ap : m_all) {
        if (!ap || ap->state() != Combatant::State::Active) continue;
        Combatant& a = *ap;
        const AttackDesc& atk = a.currentAttack();
        if (!atk.sweep && a.hitThisSwing(0)) continue; // single-target swing already landed
        const glm::vec3 s = a.strikeCenter();
        // Fighters whose capsule could touch the sphere (+ the widest capsule).
        query(s, atk.radius + 1.0f, near);
        // Nearest first, so a single-target swing hits whoever is in front.
        std::sort(near.begin(), near.end(), [&](CombatantId x, CombatantId y) {
            return glm::length(get(x).feet() - a.feet()) < glm::length(get(y).feet() - a.feet());
        });
        for (CombatantId tid : near) {
            if (tid == a.id() || a.hitThisSwing(tid)) continue;
            Combatant& t = get(tid);
            if (!t.alive() || t.state() == Combatant::State::Knockdown || t.state() == Combatant::State::Dodging) continue;
            if (t.team() == a.team() && !m_friendlyFire) continue;
            ++m_tests;
            if (distanceToCapsule(s, t.feet(), t.stats().radius, t.stats().height) > atk.radius) continue;
            a.markHit(tid);
            HitEvent e = t.receive(a, atk, s);
            if (e.outcome == HitOutcome::Parried) a.stun(t.stats().parryStun);
            m_events.push_back(std::move(e));
            if (!atk.sweep) {
                a.markHit(0); // "landed": no second target this swing
                break;
            }
        }
    }
    return m_events;
}

} // namespace kke
