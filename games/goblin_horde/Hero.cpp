// The king: POLYGON Fantasy Characters' SK_Character_Male_King with the
// ornate sword from the same pack, animated with Quaternius' Universal
// Animation Library (UAL 1 for walking, UAL 2 for the sword) retargeted
// onto him. Without the pack he is the UAL mannequin; without UAL, a block.
#include "HordeModule.h"

#include "Flinch.h"

#include "kke/Application.h"
#include "kke/KnownPacks.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace horde {

namespace fs = std::filesystem;

namespace {

constexpr float kRun = 4.6f, kBlockWalk = 1.6f, kRollSpeed = 6.5f;

// A quick slash that catches the two or three goblins in front.
kke::AttackDesc slash() {
    kke::AttackDesc a = kke::AttackDesc::light();
    a.name = "slash";
    a.windup = 0.14f;
    a.active = 0.14f;
    a.recovery = 0.26f;
    a.damage = 16.0f;
    a.staminaCost = 10.0f;
    a.poiseDamage = 14.0f;
    a.reach = 1.25f;
    a.radius = 0.85f;
    a.knockback = 3.0f;
    a.hitStun = 0.45f;
    a.sweep = true;
    return a;
}
// The great swing: slow, costly, everything around him goes flying.
kke::AttackDesc greatSwing() {
    kke::AttackDesc a = kke::AttackDesc::heavy();
    a.name = "great_swing";
    a.windup = 0.4f;
    a.active = 0.22f;
    a.recovery = 0.45f;
    a.damage = 34.0f;
    a.staminaCost = 32.0f;
    a.poiseDamage = 40.0f;
    a.reach = 0.1f; // centred on him
    a.radius = 2.3f;
    a.knockback = 7.0f;
    a.sweep = true;
    return a;
}

float yawOf(const glm::vec3& v) { return glm::degrees(std::atan2(v.x, v.z)); }

glm::vec3 flatDir(glm::vec3 v, const glm::vec3& fallback) {
    v.y = 0.0f;
    const float l = glm::length(v);
    return l > 1e-4f ? v / l : fallback;
}

std::string firstExisting(std::initializer_list<std::string> paths) {
    std::error_code ec;
    for (const std::string& p : paths)
        if (!p.empty() && fs::exists(p, ec)) return p;
    return {};
}

int findCanonical(const kke::ModelData& m, const char* name) {
    const std::string want = kke::canonicalBoneName(name);
    for (size_t b = 0; b < m.bones.size(); ++b)
        if (kke::canonicalBoneName(m.bones[b].name) == want) return static_cast<int>(b);
    return -1;
}

} // namespace

void HordeModule::loadHero() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    const std::string animDir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string ual1 = animDir.empty() ? std::string() : (fs::path(animDir) / "UAL1_Standard.fbx").string();
    if (ual1.empty() || !fs::exists(ual1)) {
        log->warn("animation library not found (assets/animations/UAL1_Standard.fbx): the king is a block");
        return;
    }
    kke::ModelData ual;
    try {
        ual = kke::loadModel(ual1);
    } catch (const std::exception& e) {
        log->error("{}: {}", ual1, e.what());
        return;
    }
    const std::string ual2 = firstExisting({ (fs::path(animDir) / "UAL2.fbx").string(),
                                             kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", base ? base : "") });
    if (!ual2.empty()) {
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::appendClipsByBoneName(ual, kke::loadModel(ual2, o));
        } catch (const std::exception& e) {
            log->warn("UAL2.fbx: {}", e.what());
        }
    } else {
        log->info("UAL2.fbx not found: the king swings with UAL 1's single sword attack");
    }

    // The king and his sword, from POLYGON Fantasy Characters.
    const std::string packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    kke::CatalogScanOptions only;
    only.onlyPacks = { "POLYGON_Fantasy_Characters" }; // not the whole shared cache
    const kke::AssetCatalog catalog = packDir.empty() ? kke::AssetCatalog{} : kke::AssetCatalog::scan(packDir, only);
    const kke::CatalogAsset* king = nullptr;
    if (const char* want = std::getenv("KKE_HORDE_HERO")) king = catalog.find(want);
    for (const char* c : { "SK_Character_Male_King", "SK_Character_Male_Rouge_01", "SK_Character_Male_Peasant_01" })
        if (!king) king = catalog.find(c, { "POLYGON_Fantasy_Characters" });
    kke::ModelData body;
    bool synty = false;
    if (king) {
        try {
            body = kke::loadModel(king->path, kke::packLoadOptions(catalog, *king));
            synty = true;
        } catch (const std::exception& e) {
            log->warn("{}: {} (the king is the UAL mannequin)", king->name, e.what());
        }
    } else {
        log->info("POLYGON Fantasy Characters not found (assets/synty or KKE_ASSETS_DIR): the king is the UAL mannequin");
    }
    if (synty) {
        const kke::BoneMatch match = kke::matchBones(ual, body);
        m_heroRig = kke::ModelData{};
        m_heroRig.bones = body.bones;
        m_heroRig.boundsMin = body.boundsMin;
        m_heroRig.boundsMax = body.boundsMax;
        m_heroRig.animations = kke::retargetAnimations(ual, body, match);
        log->info("king '{}': {} of {} bones take the UAL clips", king->name, match.matched, body.bones.size());
        m_heroModel = m_models->add(std::move(body), "horde/king");
    } else {
        m_heroRig = ual;
        kke::ModelData gold = ual;
        gold.animations.clear();
        for (kke::ModelMaterial& mat : gold.materials) mat.baseColor = mat.name.find("Joint") != std::string::npos ? glm::vec3(0.15f) : glm::vec3(0.85f, 0.7f, 0.3f);
        m_heroModel = m_models->add(std::move(gold), "horde/king");
    }
    if (!m_heroModel) return;
    m_heroSet = std::make_unique<kke::AnimationSet>(m_heroRig);
    const glm::vec3 fwd = kke::modelForward(m_heroRig);
    m_heroYaw = -yawOf(fwd);
    m_handBone = findCanonical(m_heroRig, "hand_r");
    m_heroSpine = { findCanonical(m_heroRig, "spine_01") };

    // The sword: its blade is +Y from the grip. Held so that in the rest
    // pose the blade points the way he faces, in the fist.
    if (const kke::CatalogAsset* s = catalog.find("SM_Prop_SwordOrnate_01", { "POLYGON_Fantasy_Characters" }); s && m_handBone >= 0) {
        try {
            m_swordModel = m_models->add(kke::loadModel(s->path, kke::packLoadOptions(catalog, *s)), "horde/sword");
            const std::vector<glm::mat4> rest = kke::poseToModel(m_heroRig, m_heroSet->restPose());
            const glm::mat4 hand = rest[static_cast<size_t>(m_handBone)];
            const glm::vec3 handPos(hand[3]);
            const int lower = m_heroRig.bones[static_cast<size_t>(m_handBone)].parent;
            const glm::vec3 armDir = lower >= 0 ? glm::normalize(handPos - glm::vec3(rest[static_cast<size_t>(lower)][3])) : glm::vec3(-1, 0, 0);
            glm::mat4 want = glm::translate(glm::mat4(1.0f), handPos + armDir * 0.07f);
            // +Y (blade) onto the model's forward, the guard across the fist.
            const glm::vec3 y = fwd, x = glm::normalize(glm::cross(y, armDir)), z = glm::cross(x, y);
            want = want * glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(0, 0, 0, 1));
            m_grip = glm::inverse(hand) * want;
        } catch (const std::exception& e) {
            log->warn("sword: {}", e.what());
        }
    }
}

void HordeModule::spawnHero() {
    kke::CombatStats st = kke::CombatStats::fighter();
    st.maxHealth = 260.0f;
    st.maxStamina = 120.0f;
    st.staminaRegen = 40.0f;
    st.maxPoise = 120.0f;
    st.dodgeTime = 0.45f;
    st.dodgeCost = 16.0f;
    st.knockdownTime = 1.6f;
    st.blockAngle = 80.0f;
    m_hero.id = m_combat.add(0, st);
    kke::RigidWorld::CharacterDesc cd;
    cd.radius = 0.35f;
    cd.height = 1.8f;
    cd.position = glm::vec3(0.0f, 0.05f, 2.0f);
    m_hero.body = m_rigid->world().addCharacter(cd);
    m_ai.addActor(1, "hero", cd.position);
    m_ai.setTeam(1, 1);
    if (!m_heroModel || !m_heroSet) return;
    m_hero.model = m_models->spawn(m_heroModel, glm::mat4(1.0f));
    m_models->setOverlayEnabled(m_hero.model, false);
    if (m_swordModel) {
        m_hero.sword = m_models->spawn(m_swordModel, glm::mat4(1.0f));
        m_models->setOverlayEnabled(m_hero.sword, false);
    }
    m_hero.anim = std::make_unique<kke::Animator>(*m_heroSet);
    kke::Animator& a = *m_hero.anim;
    const kke::AnimationSet& s = *m_heroSet;
    auto pick = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (int c = s.find(n); c >= 0) return c;
        return -1;
    };
    auto timed = [&](const char* state, int clip, float seconds) {
        const float speed = clip >= 0 && seconds > 0.0f ? s.duration(clip) / seconds : 1.0f;
        return a.addClipState(state, clip, false, speed);
    };
    m_hs.move = a.addBlendState("move", { { { pick({ "|Sword_Idle", "|Idle_Loop" }), 0.0f },
                                            { pick({ "|Walk_Loop" }), 1.4f },
                                            { pick({ "|Jog_Fwd_Loop" }), 3.4f },
                                            { pick({ "|Sprint_Loop", "|Jog_Fwd_Loop" }), 5.5f } } });
    const kke::AttackDesc sl = slash(), gs = greatSwing();
    m_hs.slashA = timed("slash_a", pick({ "|Sword_Regular_A", "|Sword_Light_A", "|Sword_Attack" }), sl.windup + sl.active + sl.recovery);
    m_hs.slashB = timed("slash_b", pick({ "|Sword_Regular_B", "|Sword_Light_B", "|Sword_Attack" }), sl.windup + sl.active + sl.recovery);
    m_hs.heavy = timed("great_swing", pick({ "|Sword_Heavy_A", "|Sword_Attack" }), gs.windup + gs.active + gs.recovery);
    m_hs.block = a.addClipState("block", pick({ "|Sword_Block", "|Idle_Shield_Loop", "|Sword_Idle" }), false, 2.0f);
    m_hs.roll = timed("roll", pick({ "|Roll" }), st.dodgeTime + 0.3f);
    m_hs.hit = timed("hit", pick({ "|Hit_Chest", "|Hit_Head" }), 0.4f);
    m_hs.down = timed("down", pick({ "|Hit_Knockback", "|Death01" }), 0.8f);
    m_hs.getUp = timed("get_up", pick({ "|LayToIdle", "|KipUp", "|Idle_Loop" }), 0.8f);
    m_hs.cheer = a.addClipState("dead", pick({ "|Death01" }), false);
    a.play(m_hs.move, 0.0f);
}

void HordeModule::heroBot(glm::vec2& move, bool& doSlash, bool& heavy, bool& block, bool& roll) {
    // For headless runs and the attract mode: stand near the middle and
    // cut down whatever comes; the great swing when it's crowded, back off
    // to breathe when out of stamina.
    const kke::Combatant& c = m_combat.get(m_hero.id);
    const glm::vec3 feet = m_rigid->world().characterPosition(m_hero.body);
    const Goblin* nearest = nullptr;
    float best = 1e9f;
    int close = 0;
    bool windup = false;
    for (const auto& g : m_goblins) {
        if (g->dead) continue;
        const float d = glm::length(glm::vec2(g->position.x - feet.x, g->position.z - feet.z));
        if (d < 2.2f) ++close;
        if (d < 1.6f && m_combat.get(g->id).state() == kke::Combatant::State::Windup) windup = true;
        if (d < best) {
            best = d;
            nearest = g.get();
        }
    }
    // Hold the middle of the fort; step out only for one close by.
    glm::vec3 want(0.0f);
    const bool nearMiddle = nearest && glm::length(glm::vec2(nearest->position.x, nearest->position.z)) < 6.0f;
    if (nearMiddle && best > 1.4f && c.staminaFraction() > 0.2f) want = nearest->position - feet;
    else if (glm::length(glm::vec2(feet.x, feet.z)) > 2.5f) want = -feet;
    want.y = 0.0f;
    if (glm::length(want) > 0.2f) want = glm::normalize(want);
    // In the camera's frame (forward = away from the camera), as a stick would be.
    move = glm::vec2(glm::dot(want, m_rig.right()), glm::dot(want, m_rig.forward()));
    heavy = close >= 3 && c.staminaFraction() > 0.45f;
    doSlash = !heavy && nearest && best < 1.9f && c.staminaFraction() > 0.15f;
    block = windup && !doSlash && !heavy;
    roll = false;
}

void HordeModule::updateHero(float dt) {
    using S = kke::Combatant::State;
    kke::RigidWorld& w = m_rigid->world();
    kke::Combatant& c = m_combat.get(m_hero.id);
    kke::InputMap& in = m_input->map(0);
    const glm::vec3 feet = w.characterPosition(m_hero.body);

    glm::vec2 move(0.0f);
    bool doSlash = false, heavy = false, block = false, roll = false;
    if (m_phase != Phase::Overrun) {
        if (m_bot) {
            heroBot(move, doSlash, heavy, block, roll);
        } else {
            move = in.axis2("move");
            doSlash = in.pressed("horde.slash") && (m_captured || !m_app->debugUi().visible());
            heavy = in.pressed("horde.heavy");
            block = in.held("horde.block");
            roll = in.pressed("horde.roll");
        }
    }
    glm::vec3 wish = m_rig.forward() * move.y + m_rig.right() * move.x;
    wish.y = 0.0f;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    m_moveWorld = wish;

    // Swings aim at the nearest goblin in front (a soft lock), else ahead.
    auto aim = [&]() {
        const glm::vec3 ahead = glm::length(wish) > 0.2f ? glm::normalize(wish) : m_hero.facing;
        float best = 3.5f;
        glm::vec3 dir = ahead;
        for (const auto& g : m_goblins) {
            if (g->dead) continue;
            const glm::vec3 to = g->position - feet;
            const float d = glm::length(glm::vec2(to.x, to.z));
            if (d < best && glm::dot(flatDir(to, ahead), ahead) > 0.2f) {
                best = d;
                dir = flatDir(to, ahead);
            }
        }
        m_hero.facing = dir;
    };
    c.setBlocking(block);
    if (doSlash && c.canAct()) {
        aim();
        c.place(feet, m_hero.facing);
        c.attack(slash());
    } else if (heavy && c.canAct()) {
        c.attack(greatSwing());
    }
    if (roll && c.canAct() && c.dodge() && glm::length(wish) > 0.2f) m_hero.facing = glm::normalize(wish);

    // Footwork per state.
    glm::vec3 vel(0.0f);
    switch (c.state()) {
    case S::Idle:
        vel = wish * (c.blocking() ? kBlockWalk : kRun);
        if (glm::length(wish) > 0.1f && !c.blocking()) {
            const glm::vec3 want = glm::normalize(wish);
            m_hero.facing = flatDir(m_hero.facing + (want - m_hero.facing) * (1.0f - std::exp(-12.0f * dt)), want);
        }
        break;
    case S::Windup:
    case S::Active:
        vel = m_hero.facing * (c.currentAttack().name == "slash" ? 1.4f : 0.3f);
        break;
    case S::Dodging:
        vel = m_hero.facing * kRollSpeed;
        break;
    default:
        break;
    }
    m_hero.push *= std::exp(-6.0f * dt);
    vel += m_hero.push;
    kke::RigidWorld::CharacterInput ci;
    ci.move = vel;
    w.setCharacterInput(m_hero.body, ci);
    c.place(feet, m_hero.facing);
    m_ai.setTransform(1, feet, w.characterVelocity(m_hero.body), yawOf(m_hero.facing));
}

void HordeModule::animateHero(float dt) {
    if (!m_hero.model || !m_hero.anim) return;
    using S = kke::Combatant::State;
    kke::RigidWorld& w = m_rigid->world();
    const kke::Combatant& c = m_combat.get(m_hero.id);
    // Drawn between the last two physics steps (no 60 Hz shake).
    const glm::vec3 feet = w.characterDrawPosition(m_hero.body, m_app->fixedAlpha());
    const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yawOf(m_hero.facing) + m_heroYaw), glm::vec3(0, 1, 0));
    m_models->setTransform(m_hero.model, xf);

    kke::Animator& a = *m_hero.anim;
    const int state = static_cast<int>(c.state());
    const bool entered = state != m_hero.lastState;
    m_hero.lastState = state;
    switch (c.state()) {
    case S::Windup:
        if (entered) {
            if (c.currentAttack().name == "slash") {
                m_hero.secondSlash = !m_hero.secondSlash;
                a.play(m_hero.secondSlash ? m_hs.slashB : m_hs.slashA, 0.05f, true);
            } else {
                a.play(m_hs.heavy, 0.08f, true);
            }
        }
        break;
    case S::Dodging:
        if (entered) a.play(m_hs.roll, 0.05f, true);
        break;
    case S::Stunned:
        if (entered) a.play(m_hs.hit, 0.05f, true);
        break;
    case S::Knockdown:
        if (entered) a.play(m_hs.down, 0.05f, true);
        if (c.stateTime() > c.stateLength() - 0.8f && a.current() != m_hs.getUp) a.play(m_hs.getUp, 0.2f, true);
        break;
    case S::Dead:
        if (entered) a.play(m_hs.cheer, 0.1f, true);
        break;
    case S::Idle:
    case S::Active:
    case S::Recovery: {
        if (c.state() != S::Idle) break;
        const bool busy = (a.current() == m_hs.slashA || a.current() == m_hs.slashB || a.current() == m_hs.heavy || a.current() == m_hs.roll ||
                           a.current() == m_hs.hit || a.current() == m_hs.getUp) &&
                          !a.finished();
        if (c.blocking()) {
            if (a.current() != m_hs.block) a.play(m_hs.block, 0.08f, true);
        } else if (!busy && a.current() != m_hs.move) {
            a.play(m_hs.move, 0.2f);
        }
        const glm::vec3 v = w.characterVelocity(m_hero.body);
        a.setParameter(glm::length(glm::vec2(v.x, v.z)));
        break;
    }
    }
    a.update(dt);
    std::vector<glm::mat4>* locals = m_models->boneLocals(m_hero.model);
    if (!locals) return;
    kke::Pose pose = a.pose();
    if (m_hero.flinch > 0.0f) {
        const glm::vec3 awayModel = glm::vec3(glm::inverse(xf) * glm::vec4(m_hero.flinchDir, 0.0f));
        applyFlinch(m_heroRig, pose, m_heroSpine.empty() ? -1 : m_heroSpine[0], awayModel, m_hero.flinch, 14.0f);
        m_hero.flinch = std::max(0.0f, m_hero.flinch - dt * 4.0f);
    }
    kke::poseToLocals(pose, *locals);
    if (m_hero.sword && m_handBone >= 0) {
        const std::vector<glm::mat4> bones = kke::poseToModel(m_heroRig, pose);
        m_models->setTransform(m_hero.sword, xf * bones[static_cast<size_t>(m_handBone)] * m_grip);
    }
}

} // namespace horde
