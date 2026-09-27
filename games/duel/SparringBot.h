#pragma once

#include "DuelModule.h"

#include <kke/ai/AiWorld.h>

#include <random>

namespace duel {

// The red corner when nobody's playing it. Two layers:
//  - Tactics come from the engine AI core (kke::ai::AiWorld, docs/AI.md):
//    the "boxer" species in data/boxer.yml scores press / punish / breathe /
//    circle from inputs this class sets (its stamina, whether the foe is
//    open or guarding, a drifting patience), and the core's Attack events
//    say when it is in reach and may throw. Change the fighter's style by
//    editing that file.
//  - Reflexes stay here, because they are about frame timing, not choices:
//    see a wind-up, block it (late enough and it's a parry) or step out of
//    a slow one; pick which punch fits (a knee for someone hiding behind a
//    block, the uppercut for someone low on poise).
class SparringBot {
public:
    struct Skill {
        float reaction = 0.1f;    // s before it notices a wind-up
        float blockChance = 0.6f; // of noticed wind-ups it blocks
        float parryChance = 0.3f; // of blocks it times as parries
        float aggression = 0.55f; // how readily it attacks
    };
    static Skill skillFor(const std::string& level);

    SparringBot(uint32_t seed, const Skill& skill) : m_rng(seed), m_skill(skill) {}

    // Before AiWorld::update: hand the AI core what it scores on.
    void sense(kke::ai::AiWorld& ai, kke::ai::AgentId me, const kke::Combatant& self, const kke::Combatant& foe, float dt);
    // An AiEvent::Attack for this agent: in reach, go.
    void strike() { m_strike = true; }
    // After AiWorld::update: what to do this frame. `facing` is the fighter's
    // forward, to turn the core's world-space steering into footwork.
    DuelModule::Intent think(const kke::ai::AiWorld& ai, kke::ai::AgentId me, const kke::Combatant& self, const kke::Combatant& foe,
                             const glm::vec3& facing, float distance, float dt);
    const std::string& tactic() const { return m_tactic; }

private:
    float roll() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }

    std::mt19937 m_rng;
    Skill m_skill;
    std::string m_tactic;
    bool m_strike = false;
    float m_patience = 0.5f, m_patienceTimer = 0.0f;
    float m_sawWindup = -1.0f;   // s since the foe's wind-up began (-1 = none)
    bool m_willBlock = false, m_willParry = false, m_willDodge = false;
    float m_cooldown = 0.5f;     // s until it may start another attack
    float m_strafe = 1.0f, m_strafeTimer = 0.0f;
    int m_foeBlocks = 0;         // blocks seen lately (fades)
    float m_foeBlockDecay = 0.0f;
    bool m_foeWasBlocking = false;
};

} // namespace duel
