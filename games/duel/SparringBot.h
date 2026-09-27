#pragma once

#include "DuelModule.h"

#include <random>

namespace duel {

// The red corner when nobody's playing it: a small, readable set of
// fighting habits, driven by what kke::Combatant exposes (the opponent's
// wind-up, both stamina bars, who is stunned).
//
// This is the demo's own stand-in brain. The engine's AI core (the "AI
// behavior" work, docs/AI.md) replaces it once that lands; the habits
// below are the behaviour to carry over.
//  - Keep to striking range; back off to catch a breath when tired.
//  - See a wind-up coming: block it (sometimes early enough to parry), or
//    step out of a slow one.
//  - Punish: strike while the opponent recovers, is stunned, or has just
//    had a punch parried.
//  - Someone who blocks a lot gets kneed; someone low on poise gets the
//    uppercut.
class SparringBot {
public:
    struct Skill {
        float reaction = 0.1f;    // s before it notices a wind-up
        float blockChance = 0.6f; // of noticed wind-ups it blocks
        float parryChance = 0.3f; // of blocks it times as parries
        float aggression = 0.55f; // how readily it attacks at range
    };
    static Skill skillFor(const std::string& level);

    SparringBot(uint32_t seed, const Skill& skill) : m_rng(seed), m_skill(skill) {}
    DuelModule::Intent think(const kke::Combatant& self, const kke::Combatant& foe, float distance, float dt);

private:
    float roll() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }

    std::mt19937 m_rng;
    Skill m_skill;
    float m_sawWindup = -1.0f;   // s since the foe's wind-up began (-1 = none)
    bool m_willBlock = false, m_willParry = false, m_willDodge = false;
    float m_cooldown = 0.5f;     // s until it may start another attack
    float m_strafe = 1.0f, m_strafeTimer = 0.0f;
    int m_foeBlocks = 0;         // blocks seen lately (fades)
    float m_foeBlockDecay = 0.0f;
    bool m_foeWasBlocking = false;
};

} // namespace duel
