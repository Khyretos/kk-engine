#include "SparringBot.h"

#include <algorithm>

namespace duel {

SparringBot::Skill SparringBot::skillFor(const std::string& level) {
    Skill s;
    if (level == "easy") {
        s.reaction = 0.18f;
        s.blockChance = 0.3f;
        s.parryChance = 0.05f;
        s.aggression = 0.35f;
    } else if (level == "hard") {
        s.reaction = 0.05f;
        s.blockChance = 0.85f;
        s.parryChance = 0.5f;
        s.aggression = 0.75f;
    }
    return s;
}

DuelModule::Intent SparringBot::think(const kke::Combatant& self, const kke::Combatant& foe, float distance, float dt) {
    using S = kke::Combatant::State;
    DuelModule::Intent in;
    m_cooldown = std::max(0.0f, m_cooldown - dt);
    m_strafeTimer -= dt;
    if (m_strafeTimer <= 0.0f) {
        m_strafeTimer = 0.8f + roll() * 1.6f;
        m_strafe = roll() < 0.5f ? -1.0f : 1.0f;
        if (roll() < 0.3f) m_strafe = 0.0f;
    }
    // How often the foe hides behind a block.
    if (foe.blocking() && !m_foeWasBlocking) ++m_foeBlocks;
    m_foeWasBlocking = foe.blocking();
    m_foeBlockDecay += dt;
    if (m_foeBlockDecay > 4.0f) {
        m_foeBlockDecay = 0.0f;
        m_foeBlocks = std::max(0, m_foeBlocks - 1);
    }
    if (!self.alive() || !foe.alive()) return in;

    // A wind-up coming: decide once what to do about it, after a reaction time.
    if (foe.state() == S::Windup) {
        if (m_sawWindup < 0.0f) {
            m_sawWindup = 0.0f;
            const bool slow = foe.currentAttack().windup > 0.45f;
            m_willDodge = slow && self.stamina() > self.stats().dodgeCost && roll() < m_skill.blockChance * 0.5f;
            m_willBlock = !m_willDodge && roll() < m_skill.blockChance;
            m_willParry = m_willBlock && roll() < m_skill.parryChance;
        }
        m_sawWindup += dt;
    } else if (foe.state() != S::Active) {
        m_sawWindup = -1.0f;
        m_willBlock = m_willParry = m_willDodge = false;
    }
    const bool threatened = m_sawWindup >= m_skill.reaction && distance < 2.2f;
    if (threatened) {
        const float left = foe.windupLeft();
        if (m_willDodge && left < 0.25f) {
            in.dodge = true;
            in.move = glm::vec2(m_strafe != 0.0f ? m_strafe : 1.0f, -0.5f);
        }
        // A parry is the block raised in the last moment.
        if (m_willBlock && (!m_willParry || left < self.stats().parryWindow * 0.8f)) in.block = true;
    }
    if (foe.state() == S::Active && m_willBlock) in.block = true;

    // Footwork: striking range is ~1.1 m; tired = back off.
    const float tired = self.staminaFraction();
    const float want = tired < 0.25f ? 2.6f : 1.15f;
    in.move.y = std::clamp((distance - want) * 1.5f, -1.0f, 1.0f);
    in.move.x = m_strafe * (distance < 2.0f ? 0.4f : 0.7f);

    // Attack: punish openings, press at range when fresh.
    if (!in.block && !in.dodge && self.canAct() && m_cooldown <= 0.0f && distance < 1.5f) {
        const bool open = foe.state() == S::Recovery || foe.state() == S::Stunned;
        const bool press = tired > 0.4f && roll() < m_skill.aggression * dt * 3.0f;
        if (open || press) {
            if (m_foeBlocks >= 2 && foe.blocking()) in.kick = true;
            else if (foe.poise() < foe.stats().maxPoise * 0.45f || (open && foe.state() == S::Stunned && roll() < 0.5f)) in.heavy = true;
            else in.light = true;
            m_cooldown = 0.25f + roll() * (1.0f - m_skill.aggression);
        }
    }
    return in;
}

} // namespace duel
