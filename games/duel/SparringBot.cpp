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

void SparringBot::sense(kke::ai::AiWorld& ai, kke::ai::AgentId me, const kke::Combatant& self, const kke::Combatant& foe, float dt) {
    using S = kke::Combatant::State;
    // Patience drifts: sometimes it walks you down, sometimes it waits.
    // Aggressive fighters wait less.
    m_patienceTimer -= dt;
    if (m_patienceTimer <= 0.0f) {
        m_patienceTimer = 1.2f + roll() * 2.0f;
        m_patience = std::clamp(roll() * (1.3f - m_skill.aggression), 0.0f, 1.0f);
    }
    const bool open = foe.state() == S::Recovery || foe.state() == S::Stunned;
    ai.setInput(me, "stamina", self.staminaFraction());
    ai.setInput(me, "health", self.healthFraction());
    ai.setInput(me, "foe_open", open ? 1.0f : 0.0f);
    ai.setInput(me, "foe_guard", foe.blocking() ? 1.0f : 0.0f);
    ai.setInput(me, "patience", m_patience);
}

DuelModule::Intent SparringBot::think(const kke::ai::AiWorld& ai, kke::ai::AgentId me, const kke::Combatant& self, const kke::Combatant& foe,
                                      const glm::vec3& facing, float distance, float dt) {
    using S = kke::Combatant::State;
    DuelModule::Intent in;
    const bool strike = m_strike;
    m_strike = false;
    m_tactic = ai.actionName(me);
    m_cooldown = std::max(0.0f, m_cooldown - dt);
    m_strafeTimer -= dt;
    if (m_strafeTimer <= 0.0f) {
        m_strafeTimer = 0.8f + roll() * 1.6f;
        m_strafe = roll() < 0.5f ? -1.0f : 1.0f;
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

    // Footwork: the AI core's steering, in the fighter's frame.
    const glm::vec3 right = glm::normalize(glm::cross(facing, glm::vec3(0, 1, 0)));
    glm::vec3 v(0.0f);
    if (const kke::ai::Agent* a = ai.agent(me)) v = a->desiredVelocity;
    in.move = glm::vec2(glm::dot(v, right), glm::dot(v, facing)) / 2.2f;
    if (m_tactic == "circle") {
        // Hover at the edge of reach, stepping sideways.
        in.move = glm::vec2(m_strafe * 0.6f, std::clamp((distance - 1.8f) * 1.5f, -1.0f, 1.0f));
    } else if (m_tactic == "breathe") {
        // Out of reach is far enough: no point running into the ropes.
        if (distance > 2.7f) in.move.y = std::max(in.move.y, 0.0f);
        in.move.x += m_strafe * 0.3f;
    }
    if (glm::length(in.move) > 1.0f) in.move = glm::normalize(in.move);

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
            in.move = glm::vec2(m_strafe, -0.5f);
        }
        // A parry is the block raised in the last moment.
        if (m_willBlock && (!m_willParry || left < self.stats().parryWindow * 0.8f)) in.block = true;
    }
    if (foe.state() == S::Active && m_willBlock) in.block = true;

    // The AI core says it's in reach and may throw: pick the punch.
    if (strike && !in.block && !in.dodge && self.canAct() && m_cooldown <= 0.0f) {
        const bool open = foe.state() == S::Recovery || foe.state() == S::Stunned;
        if (m_foeBlocks >= 2 && foe.blocking()) in.kick = true;
        else if (foe.poise() < foe.stats().maxPoise * 0.45f || (open && foe.state() == S::Stunned && roll() < 0.5f)) in.heavy = true;
        else in.light = true;
        m_cooldown = 0.2f + roll() * (1.0f - m_skill.aggression);
    }
    return in;
}

} // namespace duel
