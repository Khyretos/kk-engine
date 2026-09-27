#include "kke/Combat.h"

#include <gtest/gtest.h>

namespace {

using kke::AttackDesc;
using kke::Combatant;
using kke::CombatantId;
using kke::CombatStats;
using kke::CombatWorld;
using kke::HitOutcome;

constexpr float kDt = 1.0f / 60.0f;

// Two fighters a metre apart, facing each other.
struct Duel {
    CombatWorld world;
    CombatantId a, b;
    Duel() {
        a = world.add(0, CombatStats::fighter());
        b = world.add(1, CombatStats::fighter());
        world.get(a).place({ 0, 0, 0 }, { 0, 0, -1 });
        world.get(b).place({ 0, 0, -1 }, { 0, 0, 1 });
    }
    // Steps until something lands (or `seconds` pass).
    std::vector<kke::HitEvent> run(float seconds) {
        std::vector<kke::HitEvent> all;
        for (float t = 0.0f; t < seconds; t += kDt) {
            const auto& e = world.step(kDt);
            all.insert(all.end(), e.begin(), e.end());
        }
        return all;
    }
};

TEST(Combat, AttackGoesThroughItsPhasesAndLandsOnce) {
    Duel d;
    Combatant& a = d.world.get(d.a);
    ASSERT_TRUE(a.attack(AttackDesc::light()));
    EXPECT_EQ(a.state(), Combatant::State::Windup);
    EXPECT_LT(a.stamina(), a.stats().maxStamina);
    EXPECT_FALSE(a.attack(AttackDesc::light())); // busy
    const auto hits = d.run(1.0f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Hit);
    EXPECT_EQ(hits[0].target, d.b);
    EXPECT_FLOAT_EQ(d.world.get(d.b).health(), 90.0f);
    EXPECT_EQ(a.state(), Combatant::State::Idle);
}

TEST(Combat, OutOfReachMisses) {
    Duel d;
    d.world.get(d.b).place({ 0, 0, -3 }, { 0, 0, 1 });
    d.world.get(d.a).attack(AttackDesc::light());
    EXPECT_TRUE(d.run(1.0f).empty());
}

TEST(Combat, SameTeamIsSafeUnlessFriendlyFire) {
    CombatWorld w;
    const CombatantId a = w.add(0), b = w.add(0);
    w.get(a).place({ 0, 0, 0 }, { 0, 0, -1 });
    w.get(b).place({ 0, 0, -1 }, { 0, 0, 1 });
    w.get(a).attack(AttackDesc::light());
    for (int i = 0; i < 60; ++i) EXPECT_TRUE(w.step(kDt).empty());
    w.setFriendlyFire(true);
    w.get(a).attack(AttackDesc::light());
    size_t n = 0;
    for (int i = 0; i < 60; ++i) n += w.step(kDt).size();
    EXPECT_EQ(n, 1u);
}

TEST(Combat, BlockTakesChipAndStaminaParryStunsTheAttacker) {
    Duel d;
    Combatant& a = d.world.get(d.a);
    Combatant& b = d.world.get(d.b);
    // An early block (well before the hit) is a block, not a parry.
    b.setBlocking(true);
    d.run(0.5f);
    a.attack(AttackDesc::light());
    auto hits = d.run(0.6f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Blocked);
    EXPECT_NEAR(b.health(), 100.0f - 10.0f * 0.15f, 1e-4f);
    EXPECT_LT(b.stamina(), 100.0f);

    // Raising it just before the hit parries: no damage, attacker stunned.
    d.run(1.0f);
    b.setBlocking(false);
    const float health = b.health();
    a.attack(AttackDesc::light());
    for (float t = 0.0f; t < 0.26f; t += kDt) d.world.step(kDt); // windup is 0.28 s
    b.setBlocking(true);
    hits = d.run(0.3f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Parried);
    EXPECT_FLOAT_EQ(b.health(), health);
    EXPECT_EQ(a.state(), Combatant::State::Stunned);
}

TEST(Combat, BlockOnlyCoversTheFront) {
    Duel d;
    Combatant& b = d.world.get(d.b);
    b.place({ 0, 0, -1 }, { 0, 0, -1 }); // back to the attacker
    b.setBlocking(true);
    d.run(0.5f);
    d.world.get(d.a).attack(AttackDesc::light());
    const auto hits = d.run(0.6f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Hit);
}

TEST(Combat, KicksBreakTheGuard) {
    Duel d;
    Combatant& a = d.world.get(d.a);
    Combatant& b = d.world.get(d.b);
    b.setBlocking(true);
    d.run(0.5f);
    bool broke = false;
    for (int i = 0; i < 6 && !broke; ++i) {
        a.attack(AttackDesc::kick());
        for (const auto& e : d.run(0.9f)) broke = broke || e.outcome == HitOutcome::GuardBroke;
    }
    EXPECT_TRUE(broke);
}

TEST(Combat, PoiseBreaksIntoKnockdownThenGetsUp) {
    Duel d;
    Combatant& a = d.world.get(d.a);
    Combatant& b = d.world.get(d.b);
    a.attack(AttackDesc::heavy());
    auto hits = d.run(1.3f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Hit);
    a.attack(AttackDesc::heavy());
    hits = d.run(0.75f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].outcome, HitOutcome::Knockdown);
    EXPECT_EQ(b.state(), Combatant::State::Knockdown);
    EXPECT_GT(glm::length(hits[0].push), 1.0f);
    // Can't be hit while down.
    d.run(0.6f);
    a.attack(AttackDesc::light());
    EXPECT_TRUE(d.run(0.5f).empty());
    d.run(2.0f);
    EXPECT_EQ(b.state(), Combatant::State::Idle);
    EXPECT_FLOAT_EQ(b.poise(), b.stats().maxPoise);
}

TEST(Combat, DeathAndTiredness) {
    Duel d;
    Combatant& a = d.world.get(d.a);
    Combatant& b = d.world.get(d.b);
    b.stats().maxPoise = 1000.0f;
    b.reset();
    int swings = 0;
    while (b.alive() && swings < 40) {
        if (a.attack(AttackDesc::light())) ++swings;
        d.run(0.8f);
    }
    EXPECT_FALSE(b.alive());
    EXPECT_EQ(swings, 10);
    // Tired: no swing with no stamina left.
    a.reset();
    a.stats().staminaRegen = 0.0f;
    int started = 0;
    for (int i = 0; i < 20; ++i)
        if (a.attack(AttackDesc::heavy())) {
            ++started;
            for (float t = 0.0f; t < 1.3f; t += kDt) a.update(kDt);
        }
    EXPECT_EQ(started, 4);
}

TEST(Combat, DodgeAvoidsTheHit) {
    Duel d;
    d.world.get(d.a).attack(AttackDesc::light());
    for (float t = 0.0f; t < 0.2f; t += kDt) d.world.step(kDt);
    ASSERT_TRUE(d.world.get(d.b).dodge());
    EXPECT_TRUE(d.run(0.25f).empty());
}

TEST(Combat, SweepHitsAHordeSingleHitsOne) {
    CombatWorld w;
    const CombatantId hero = w.add(0);
    w.get(hero).place({ 0, 0, 0 }, { 0, 0, -1 });
    std::vector<CombatantId> goblins;
    for (int i = 0; i < 3; ++i) {
        goblins.push_back(w.add(1, CombatStats::grunt()));
        w.get(goblins.back()).place({ -0.45f + 0.45f * static_cast<float>(i), 0, -1.1f }, { 0, 0, 1 });
    }
    // A far-off crowd the grid never tests.
    for (int i = 0; i < 200; ++i) w.get(w.add(1, CombatStats::grunt())).place({ 40.0f + static_cast<float>(i % 20), 0, static_cast<float>(i / 20) }, { 0, 0, 1 });

    AttackDesc heavy = AttackDesc::heavy();
    heavy.height = 0.9f;
    w.get(hero).attack(heavy);
    size_t hits = 0, tests = 0;
    for (float t = 0.0f; t < 1.3f; t += kDt) {
        hits += w.step(kDt).size();
        tests += w.testsLastStep();
    }
    EXPECT_EQ(hits, 3u); // and knocked down: grunts have little poise
    EXPECT_LT(tests, 50u);
    for (float t = 0.0f; t < 2.0f; t += kDt) w.step(kDt); // back up

    w.get(hero).attack(AttackDesc::light());
    hits = 0;
    for (float t = 0.0f; t < 1.0f; t += kDt) hits += w.step(kDt).size();
    EXPECT_EQ(hits, 1u);
}

TEST(Combat, HealUpToTheMaximumNotTheDead) {
    CombatWorld w;
    const CombatantId hero = w.add(0, CombatStats::fighter());
    const CombatantId foe = w.add(1, CombatStats::fighter());
    w.get(hero).place({ 0, 0, 0 }, { 0, 0, 1 });
    w.get(foe).place({ 0, 0, 1.0f }, { 0, 0, -1 });
    w.get(foe).attack(AttackDesc::heavy());
    for (float t = 0.0f; t < 1.5f; t += kDt) w.step(kDt);
    Combatant& c = w.get(hero);
    ASSERT_LT(c.health(), c.stats().maxHealth);
    c.heal(5.0f);
    EXPECT_NEAR(c.health(), c.stats().maxHealth - AttackDesc::heavy().damage + 5.0f, 1e-3f);
    c.heal(1000.0f);
    EXPECT_FLOAT_EQ(c.health(), c.stats().maxHealth);
    c.heal(-50.0f); // not a way to hurt
    EXPECT_FLOAT_EQ(c.health(), c.stats().maxHealth);
}

TEST(Combat, CapsuleDistance) {
    float h = 0.0f;
    EXPECT_NEAR(kke::distanceToCapsule({ 1, 1, 0 }, { 0, 0, 0 }, 0.3f, 1.8f, &h), 0.7f, 1e-5f);
    EXPECT_NEAR(h, 1.0f / 1.8f, 1e-5f);
    EXPECT_LT(kke::distanceToCapsule({ 0, 1, 0 }, { 0, 0, 0 }, 0.3f, 1.8f), 0.0f);
    EXPECT_NEAR(kke::distanceToCapsule({ 0, 2.5f, 0 }, { 0, 0, 0 }, 0.3f, 1.8f), 0.7f, 1e-5f);
}

} // namespace
