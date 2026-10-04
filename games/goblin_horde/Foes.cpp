// The horde: goblins and bosses from data/foes.yml, dressed in Goblin War
// Camp, Dungeon and Fantasy Rivals art, moved by the engine AI core and
// fighting with the moves their type lists. Online, only the host thinks
// for them; a client draws where the host says they are (Net.cpp).

#include "HordeModule.h"

#include "Flinch.h"

#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace horde {

namespace {

float yawOf(const glm::vec3& v) { return glm::degrees(std::atan2(v.x, v.z)); }
glm::vec3 dirOf(float yaw) { return { std::sin(glm::radians(yaw)), 0.0f, std::cos(glm::radians(yaw)) }; }

bool usesCombat(Move::Kind k) {
    return k == Move::Kind::Melee || k == Move::Kind::Dash || k == Move::Kind::Slam || k == Move::Kind::Combo;
}

// Puppet::moves has three states per move: the clip (or a combo's first
// hit), then a combo's second hit or a super's roar, then a combo's third
// hit or a shot's aim.
constexpr int kPerMove = 3;


// Where to head for `to` from `at` without walking into the fort's walls
// (half width 13, a gateway 4.4 wide in the middle of each): through the
// gateway on `at`'s side when one is inside the walls and the other not.
glm::vec2 wayTo(const std::vector<glm::vec3>& gates, const glm::vec2& at, const glm::vec2& to) {
    const auto inside = [](const glm::vec2& p) { return std::max(std::abs(p.x), std::abs(p.y)) < 13.0f; };
    const bool in = inside(at);
    if (gates.empty() || in == inside(to)) return to;
    const glm::vec3* gate = &gates.front();
    for (const glm::vec3& g : gates)
        if (glm::dot(glm::vec2(g.x, g.z), at) > glm::dot(glm::vec2(gate->x, gate->z), at)) gate = &g;
    const glm::vec2 out(gate->x, gate->z), side(out.y, -out.x);
    const float depth = glm::dot(out, at);
    // Off the gateway's line: get in line just off the wall first.
    if (std::abs(glm::dot(side, at)) > 1.4f) return out * (in ? std::min(depth, 10.5f) : std::max(depth, 15.5f));
    return out * (in ? 15.5f : 10.5f);
}

} // namespace

HordeModule::Foe* HordeModule::foeByCombatant(kke::CombatantId id) {
    for (auto& f : m_foes)
        if (f->id == id) return f.get();
    return nullptr;
}

HordeModule::Foe* HordeModule::foeByAgent(kke::ai::AgentId id) {
    for (auto& f : m_foes)
        if (f->agent == id) return f.get();
    return nullptr;
}

HordeModule::Foe* HordeModule::foeByNet(uint16_t id) {
    for (auto& f : m_foes)
        if (f->netId == id) return f.get();
    return nullptr;
}

// A type for the next goblin, by the wave's mix (or each type's share).
int HordeModule::pickType() {
    const Wave w = m_roster.waveAt(m_wave);
    std::vector<float> weights;
    float total = 0.0f;
    for (const FoeType& t : m_roster.types) {
        float s = t.share;
        if (!w.mix.empty()) {
            auto it = w.mix.find(t.id);
            s = it == w.mix.end() ? 0.0f : it->second;
        }
        weights.push_back(s);
        total += s;
    }
    if (total <= 0.0f) return 0;
    float r = std::uniform_real_distribution<float>(0.0f, total)(m_rng);
    for (size_t i = 0; i < weights.size(); ++i) {
        if (r < weights[i]) return static_cast<int>(i);
        r -= weights[i];
    }
    return 0;
}

HordeModule::Foe& HordeModule::spawnFoe(int type, bool boss, const glm::vec3& at, uint16_t netId, int look, int skin, int weapon, int offhand) {
    const std::vector<FoeType>& list = boss ? m_roster.bosses : m_roster.types;
    const FoeType& t = list[static_cast<size_t>(std::clamp(type, 0, static_cast<int>(list.size()) - 1))];
    const Wave w = m_roster.waveAt(m_wave);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto f = std::make_unique<Foe>();
    f->type = &t;
    f->typeIndex = type;
    f->agent = m_nextAgent++;
    f->netId = netId ? netId : m_nextNetId++;
    f->position = f->netPos = at;
    f->speedScale = t.speed * (boss ? 1.0f : w.speed * (0.88f + 0.24f * u(m_rng)));
    f->scale = glm::mix(t.scale.x, t.scale.y, u(m_rng));
    f->yaw = f->netYaw = yawOf(-at);
    f->lookIndex = look;
    f->skin = skin;
    f->weaponIndex = weapon;
    f->offhandIndex = offhand;

    kke::CombatStats st = kke::CombatStats::grunt();
    const int players = std::max(1, static_cast<int>(m_heroes.size()));
    if (boss) {
        st.maxHealth = t.health * (1.0f + 0.5f * static_cast<float>(players - 1));
        st.maxPoise = 400.0f * t.poise;
        st.knockdownTime = 1.5f;
    } else {
        st.maxHealth *= t.health * w.health;
        st.maxPoise *= t.poise;
    }
    // A client's copy only shows hits; the host's horde decides who falls.
    if (netClient()) st.maxHealth = 1e6f;
    st.radius = boss ? 0.45f * f->scale : 0.38f * f->scale;
    st.height = 1.8f * f->scale;
    f->id = m_combat.add(1, st);
    m_ai.addAgent(f->agent, boss ? "boss" : "goblin", at, f->yaw);
    m_ai.setTeam(f->agent, 2);
    if (kke::ai::Agent* a = m_ai.agent(f->agent)) a->home = glm::vec3(0.0f);
    for (const Move& m : t.moves) f->cooldown.push_back(m.cooldown * (0.3f + 0.7f * u(m_rng)));
    f->nextThink = 0.5f + u(m_rng);
    if (netClient()) m_ai.setEnabled(f->agent, false);
    dressFoe(*f);
    m_foes.push_back(std::move(f));
    return *m_foes.back();
}

void HordeModule::dressFoe(Foe& f) {
    const FoeType& t = *f.type;
    std::uniform_int_distribution<int> any(0, 1 << 20);
    auto pick = [&](int& index, size_t count) {
        if (count == 0) return;
        if (index < 0 || index >= static_cast<int>(count)) index = any(m_rng) % static_cast<int>(count);
    };
    pick(f.lookIndex, t.models.size());
    // The picked look; if its pack isn't there, the next one that is.
    const Look* look = nullptr;
    for (size_t k = 0; k < t.models.size() && !look; ++k) {
        const size_t i = (static_cast<size_t>(std::max(0, f.lookIndex)) + k) % t.models.size();
        const ArtRef& ref = t.models[i];
        if (f.skin < 0) f.skin = any(m_rng) % std::max(1, m_art->skins(ref));
        look = m_art->character(ref, f.skin);
        if (!look && f.skin != 0) look = m_art->character(ref, 0);
        if (look) f.lookIndex = static_cast<int>(i);
    }
    glm::vec3 tint = t.tint;
    if (!look) {
        // No pack: the mannequin, goblin green (bosses dark red).
        look = m_art->mannequin(t.boss ? glm::vec3(0.55f, 0.2f, 0.16f) : glm::vec3(0.35f, 0.55f, 0.22f) * t.tint);
        tint = glm::vec3(1.0f);
    }
    if (f.skin < 0) f.skin = 0;
    dressPuppet(f.look, look, tint, f.scale);
    Puppet& p = f.look;
    if (!p.model || !p.look || !p.anim) return;
    const Rig& r = *p.look->rig;
    pick(f.weaponIndex, t.weapons.size());
    pick(f.offhandIndex, t.offhands.size());
    auto hold = [&](const ArtRef& ref, bool left) {
        const kke::ModelModule::ModelId m = m_art->prop(ref);
        if (!m) return;
        kke::ModelModule::InstanceId& slot = left ? p.left : p.right;
        slot = m_models->spawn(m, glm::mat4(1.0f));
        m_models->setOverlayEnabled(slot, false);
        (left ? p.leftGrip : p.rightGrip) = m_art->grip(r, ref.grip, left);
        if (!left && ref.grip == "twohand") p.twoHanded = true;
    };
    if (f.weaponIndex >= 0) hold(t.weapons[static_cast<size_t>(f.weaponIndex)], false);
    if (f.offhandIndex >= 0) hold(t.offhands[static_cast<size_t>(f.offhandIndex)], true);
    if (!t.back.empty())
        if (const kke::ModelModule::ModelId m = m_art->prop(t.back)) {
            p.back = m_models->spawn(m, glm::mat4(1.0f));
            m_models->setOverlayEnabled(p.back, false);
            p.backAt = m_art->attach(r, r.chest);
        }
    // A state per move, timed to the move.
    kke::Animator& a = *p.anim;
    p.moves.assign(t.moves.size() * kPerMove, -1);
    for (size_t i = 0; i < t.moves.size(); ++i) {
        const Move& m = t.moves[i];
        const std::string base = "m" + std::to_string(i) + "_";
        auto add = [&](int k, int clip, float seconds, bool loop) {
            if (clip < 0) return;
            const float speed = seconds > 0.0f ? r.set->duration(clip) / seconds : 1.0f;
            p.moves[i * kPerMove + static_cast<size_t>(k)] = a.addClipState(base + std::to_string(k), clip, loop, speed);
        };
        if (m.kind == Move::Kind::Combo) {
            for (size_t k = 0; k < m.clips.size() && k < kPerMove; ++k) add(static_cast<int>(k), r.clip(m.clips[k]), m.total(), false);
            continue;
        }
        const int roar = r.clip(m.roarClips);
        // A super: the roar for most of the wind-up, then the blow.
        const float blow = roar >= 0 ? m.hit.windup * 0.3f + m.hit.active + m.hit.recovery : m.total();
        add(0, r.clip(m.clips), blow, false);
        if (roar >= 0) add(1, roar, m.hit.windup * 0.7f, false);
        add(2, r.clip(m.aimClips), 0.0f, true);
    }
}

void HordeModule::releaseFoe(Foe& f) {
    if (f.ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(f.ragdoll);
    f.ragdoll = 0;
    if (f.markId) unmark(f.markId);
    f.markId = 0;
    m_ai.remove(f.agent);
    if (f.id) m_combat.remove(f.id);
    f.id = 0;
    undressPuppet(f.look);
}

void HordeModule::killFoe(Foe& f, const glm::vec3& push) {
    if (f.dead) return;
    f.dead = true;
    f.deadTime = 0.0f;
    f.move = -1;
    if (f.markId) unmark(f.markId);
    f.markId = 0;
    m_ai.setEnabled(f.agent, false);
    ++m_kills;
    ++m_waveKills;
    m_morale = std::max(0.0f, m_morale - (f.type->boss ? 0.6f : 0.035f));
    Puppet& p = f.look;
    if (!m_ragdolls || !p.model || !p.look || m_ragdollCap <= 0) return;
    // Too many down at once: the oldest stops simulating and lies still.
    while (static_cast<int>(m_ragdolled.size()) >= m_ragdollCap) {
        Foe* old = foeByAgent(m_ragdolled.front());
        m_ragdolled.pop_front();
        if (old && old->ragdoll) {
            m_ragdolls->destroyRagdoll(old->ragdoll);
            old->ragdoll = 0;
            old->frozen = true;
        }
    }
    const Rig& r = *p.look->rig;
    const glm::mat4 instance = m_models->transform(p.model);
    std::vector<glm::mat4> world = m_models->boneWorld(p.model);
    for (glm::mat4& b : world) b = instance * b;
    if (m_noRagdoll.count(p.look)) return;
    std::string missing;
    f.ragdollDesc = kke::buildHumanoidRagdoll(r.data, world, 30.0f * f.scale * f.scale * f.scale, &missing);
    if (f.ragdollDesc.bodies.empty()) {
        kke::log::get(name())->info("{}: no ragdoll, the skeleton has no '{}' bone (it plays its death instead)", p.look->name, missing);
        m_noRagdoll.insert(p.look);
        return;
    }
    f.binding = kke::bindSkeletonToRagdoll(r.data, world, f.ragdollDesc);
    f.ragdoll = m_ragdolls->createRagdoll(f.ragdollDesc, f.velocity * 0.5f + push * 0.4f);
    if (!f.ragdoll) return;
    for (const char* b : { "torso", "head" }) m_ragdolls->pushRagdollBody(f.ragdoll, f.ragdollDesc.findBody(b), push);
    m_ragdolled.push_back(f.agent);
    // What it held falls with it (no hand to follow any more).
    for (kke::ModelModule::InstanceId* i : { &p.right, &p.left, &p.head, &p.back })
        if (*i) m_models->setVisible(*i, false);
}

// Which move, if any, the goblin can start now against `target` at `dist`.
void HordeModule::thinkFoe(Foe& f, float dt) {
    const FoeType& t = *f.type;
    for (float& c : f.cooldown) c -= dt;
    f.nextThink -= dt;
    kke::Combatant& c = m_combat.get(f.id);
    if (f.move >= 0 || !c.canAct() || f.nextThink > 0.0f || f.target < 0) return;
    const Hero& h = *m_heroes[static_cast<size_t>(f.target)];
    // A player on the ground or just up gets a moment: no knockdown lock.
    if (h.upGrace > 0.0f || (h.id && !h.remote && m_combat.get(h.id).state() == kke::Combatant::State::Knockdown)) return;
    const glm::vec3 hp = heroFeet(h);
    const float dist = glm::length(glm::vec2(hp.x - f.position.x, hp.z - f.position.z)) - c.stats().radius;
    const glm::vec3 eye = f.position + glm::vec3(0.0f, 1.4f * f.scale, 0.0f);
    const bool sees = !lineBlocked(eye, hp + glm::vec3(0.0f, 1.2f, 0.0f));
    const bool crowded = f.tactic == "wait_turn";
    for (size_t i = 0; i < t.moves.size(); ++i) {
        const Move& m = t.moves[i];
        if (f.cooldown[i] > 0.0f) continue;
        bool ok = false;
        switch (m.kind) {
        case Move::Kind::Melee:
        case Move::Kind::Combo:
        case Move::Kind::Slam:
            ok = dist <= m.range && (!crowded || t.boss);
            break;
        case Move::Kind::Dash:
            ok = dist <= m.range && dist >= m.minRange && sees;
            break;
        case Move::Kind::Shot:
        case Move::Kind::Lob:
        case Move::Kind::Blast:
            ok = dist <= m.range && dist >= m.minRange && (sees || m.kind != Move::Kind::Shot);
            break;
        case Move::Kind::Buff: {
            int near = 0;
            for (const auto& o : m_foes)
                if (o.get() != &f && !o->dead && glm::distance(o->position, f.position) < m.radius) ++near;
            ok = near >= 3;
            break;
        }
        case Move::Kind::Heal:
            for (const auto& o : m_foes)
                if (!o->dead && o->id && glm::distance(o->position, f.position) < m.range && m_combat.get(o->id).healthFraction() < 0.65f) ok = true;
            break;
        }
        if (ok) {
            beginFoeMove(f, static_cast<int>(i));
            return;
        }
    }
}

void HordeModule::beginFoeMove(Foe& f, int index) {
    const Move& m = f.type->moves[static_cast<size_t>(index)];
    kke::Combatant& c = m_combat.get(f.id);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const Hero* h = f.target >= 0 ? m_heroes[static_cast<size_t>(f.target)].get() : nullptr;
    const glm::vec3 at = h ? heroFeet(*h) : f.position + dirOf(f.yaw) * 3.0f;
    const glm::vec3 to = at - f.position;
    const float len = glm::length(glm::vec2(to.x, to.z));
    const glm::vec3 dir = len > 1e-3f ? glm::vec3(to.x, 0.0f, to.z) / len : dirOf(f.yaw);
    f.yaw = yawOf(dir);
    c.place(f.position, dir);
    if (usesCombat(m.kind) && !c.attack(m.hit)) return;
    f.move = index;
    f.moveTime = 0.0f;
    f.fired = false;
    f.roared = false;
    f.comboHit = 0;
    f.aimAt = at;
    f.cooldown[static_cast<size_t>(index)] = m.cooldown * (0.85f + 0.3f * u(m_rng));
    f.look.lastState = -1;
    // The warning on the ground, for whatever's slow and hurts.
    if (m.telegraph > 0.0f && (m.kind == Move::Kind::Slam || m.kind == Move::Kind::Dash || m.kind == Move::Kind::Combo)) {
        Mark k;
        k.super = m.super;
        k.total = m.hit.windup;
        k.color = m.super ? glm::vec3(1.0f, 0.1f, 0.05f) : glm::vec3(1.0f, 0.45f, 0.1f);
        if (m.kind == Move::Kind::Slam) {
            k.shape = Mark::Shape::Circle;
            k.center = f.position;
            k.radius = m.hit.radius;
        } else if (m.kind == Move::Kind::Dash) {
            k.shape = Mark::Shape::Line;
            k.center = f.position;
            k.dir = dir;
            k.length = m.lunge + m.hit.reach + m.hit.radius;
            k.radius = m.hit.radius + 0.3f;
        } else {
            k.shape = Mark::Shape::Cone;
            k.center = f.position;
            k.dir = dir;
            k.length = m.hit.reach + m.hit.radius + 0.5f;
            k.radius = 70.0f; // degrees either side
        }
        f.markId = mark(k);
    }
    // Spells and shots run on their own clock (no swing to land).
    if (!usesCombat(m.kind)) {
        const size_t s = static_cast<size_t>(index) * kPerMove;
        Puppet& p = f.look;
        if (p.anim && s < p.moves.size()) {
            const int aim = p.moves[s + 2];
            const int clip = aim >= 0 && m.kind == Move::Kind::Shot ? aim : p.moves[s];
            if (clip >= 0) p.anim->play(clip, 0.1f, true);
        }
    }
}

void HordeModule::runFoeMove(Foe& f, float dt) {
    using S = kke::Combatant::State;
    if (f.move < 0) return;
    const Move& m = f.type->moves[static_cast<size_t>(f.move)];
    kke::Combatant& c = m_combat.get(f.id);
    f.moveTime += dt;
    auto finish = [&] {
        f.move = -1;
        if (f.markId) unmark(f.markId);
        f.markId = 0;
        f.nextThink = std::uniform_real_distribution<float>(0.25f, 0.8f)(m_rng);
        c.clearStrikePoint();
    };
    // Hit out of it (stunned, knocked down): the move is lost.
    if (c.state() == S::Stunned || c.state() == S::Knockdown || c.state() == S::Dead) {
        finish();
        return;
    }
    const Hero* h = f.target >= 0 && f.target < static_cast<int>(m_heroes.size()) ? m_heroes[static_cast<size_t>(f.target)].get() : nullptr;
    const glm::vec3 handR = f.look.model ? f.look.handPos : f.position + glm::vec3(0.0f, 1.3f * f.scale, 0.0f);
    const glm::vec3 handL = f.look.model && f.look.left ? f.look.offhandPos : handR;
    if (usesCombat(m.kind)) {
        if (m.kind == Move::Kind::Slam && c.state() == S::Active) {
            c.setStrikePoint(f.position + glm::vec3(0.0f, 0.5f, 0.0f));
            if (!f.fired) {
                f.fired = true;
                if (m_fx) {
                    for (int i = 0; i < (m.super ? 40 : 16); ++i) {
                        const float a = 6.283f * static_cast<float>(i) / (m.super ? 40.0f : 16.0f);
                        const glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
                        m_fx->smoke(f.position + out * m.hit.radius * 0.5f, out * m.hit.radius * 0.8f + glm::vec3(0, 0.6f, 0), { 0.45f, 0.4f, 0.33f },
                                    m.super ? 0.9f : 0.5f, 1.6f, 0.5f);
                    }
                    m_fx->sparks(f.position, glm::vec3(0, 1, 0), m.super ? 40 : 12, 6.0f);
                }
                if (f.markId) unmark(f.markId);
                f.markId = 0;
            }
        } else if (c.state() != S::Active) {
            c.clearStrikePoint();
        }
        if (c.state() == S::Idle) {
            // A combo's next hit follows straight away.
            if (m.kind == Move::Kind::Combo && f.comboHit + 1 < static_cast<int>(std::min<size_t>(m.clips.size(), kPerMove))) {
                ++f.comboHit;
                if (h) {
                    const glm::vec3 to = heroFeet(*h) - f.position;
                    if (glm::length(glm::vec2(to.x, to.z)) > 1e-3f) f.yaw = yawOf(to);
                }
                c.place(f.position, dirOf(f.yaw));
                f.look.lastState = -1;
                if (c.attack(m.hit)) {
                    if (f.markId) unmark(f.markId);
                    Mark k;
                    k.shape = Mark::Shape::Cone;
                    k.center = f.position;
                    k.dir = dirOf(f.yaw);
                    k.length = m.hit.reach + m.hit.radius + 0.5f;
                    k.radius = 70.0f;
                    k.total = m.hit.windup;
                    k.color = glm::vec3(1.0f, 0.45f, 0.1f);
                    f.markId = m.telegraph > 0.0f ? mark(k) : 0;
                    return;
                }
            }
            finish();
        }
        return;
    }
    // Shots, lobs, blasts, buffs, heals: wind-up, then it happens.
    if (h && m.kind == Move::Kind::Shot && !f.fired) f.aimAt = heroFeet(*h);
    if (!f.fired && f.moveTime >= m.hit.windup) {
        f.fired = true;
        // The clip that throws (after holding an aim).
        const size_t s = static_cast<size_t>(f.move) * kPerMove;
        if (f.look.anim && s < f.look.moves.size() && f.look.moves[s + 2] >= 0 && f.look.moves[s] >= 0) f.look.anim->play(f.look.moves[s], 0.05f, true);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        switch (m.kind) {
        case Move::Kind::Shot: {
            const bool bow = m.projectile == "arrow" || m.projectile == "bolt";
            const glm::vec3 from = (bow ? handL : handR) + dirOf(f.yaw) * 0.3f;
            glm::vec3 target = f.aimAt + glm::vec3(0.0f, 1.2f, 0.0f);
            // Lead a running player a little, and miss a little.
            if (h) target += h->velocity * (glm::distance(from, target) / std::max(1.0f, m.speed)) * 0.6f;
            target += glm::vec3(u(m_rng), u(m_rng) * 0.4f, u(m_rng)) * 0.35f;
            for (int i = 0; i < m.count; ++i) {
                Shot s;
                s.kind = m.projectile;
                s.team = 1;
                s.from = f.id;
                s.position = from;
                s.velocity = glm::normalize(target - from) * m.speed;
                s.gravity = bow ? 9.8f * 0.35f : 0.0f;
                s.hit = m.hit;
                s.splash = m.splash;
                s.homing = m.homing;
                s.target = f.target;
                s.life = 4.0f;
                fire(std::move(s));
            }
            break;
        }
        case Move::Kind::Lob: {
            // Marked where it lands; it lands as the mark runs out.
            const float flight = std::max(0.6f, m.telegraph);
            Mark k;
            k.shape = Mark::Shape::Circle;
            k.center = f.aimAt;
            k.radius = m.radius;
            k.total = flight;
            k.color = glm::vec3(1.0f, 0.4f, 0.1f);
            const int id = mark(k);
            for (int i = 0; i < m.count; ++i) {
                Shot s;
                s.kind = m.projectile;
                s.team = 1;
                s.from = f.id;
                const glm::vec3 from = (m.projectile == "arrow" ? handL : handR) + glm::vec3(0.0f, 0.3f, 0.0f);
                glm::vec3 land = f.aimAt;
                if (m.count > 1) land += glm::vec3(u(m_rng), 0.0f, u(m_rng)) * m.spread;
                land.y = groundAt(land);
                s.position = from;
                s.gravity = 9.8f;
                s.velocity = ballistic(from, land, flight + 0.05f * static_cast<float>(i), s.gravity);
                s.hit = m.hit;
                s.splash = m.count > 1 ? 0.9f : m.radius;
                s.life = flight + 1.0f;
                s.markId = id; // the mark goes when they land
                fire(std::move(s));
            }
            break;
        }
        case Move::Kind::Blast: {
            Mark k;
            k.shape = Mark::Shape::Circle;
            k.center = f.aimAt;
            k.radius = m.radius;
            k.total = m.telegraph;
            k.color = glm::vec3(1.0f, 0.3f, 0.05f);
            mark(k);
            Blast b;
            b.center = f.aimAt;
            b.radius = m.radius;
            b.delay = m.telegraph;
            b.hit = m.hit;
            b.element = m.element;
            b.team = 1;
            blast(b);
            if (m_fx) m_fx->sparks(handR, glm::vec3(0, 1, 0), 20, 3.0f);
            break;
        }
        case Move::Kind::Buff:
            for (auto& o : m_foes) {
                if (o->dead || glm::distance(o->position, f.position) > m.radius) continue;
                if (m.buff == "haste") o->haste = std::max(o->haste, m.duration);
                else o->stoneskin = std::max(o->stoneskin, m.duration);
                if (m_fx)
                    m_fx->sparks(o->position + glm::vec3(0.0f, 1.0f * o->scale, 0.0f), glm::vec3(0, 1, 0), 10, 2.5f);
            }
            break;
        case Move::Kind::Heal: {
            Foe* worst = nullptr;
            float lowest = 0.99f;
            for (auto& o : m_foes) {
                if (o->dead || !o->id || glm::distance(o->position, f.position) > m.range) continue;
                const float hf = m_combat.get(o->id).healthFraction();
                if (hf < lowest) {
                    lowest = hf;
                    worst = o.get();
                }
            }
            if (worst) {
                // A green spark flies over; the health is back at once.
                m_combat.get(worst->id).heal(m.amount * (worst->type->boss ? 0.2f : 1.0f));
                Shot s;
                s.kind = "heal";
                s.team = 1;
                s.position = handR;
                const glm::vec3 to = worst->position + glm::vec3(0.0f, 1.0f, 0.0f);
                s.gravity = 9.8f;
                s.velocity = ballistic(handR, to, 0.6f, s.gravity);
                s.hit.damage = 0.0f;
                s.life = 0.6f;
                s.local = false; // only a sight
                fire(std::move(s));
                if (m_fx) m_fx->sparks(to, glm::vec3(0, 1, 0), 14, 2.0f);
            }
            break;
        }
        default:
            break;
        }
    }
    if (f.moveTime >= m.total()) finish();
}

void HordeModule::updateFoes(float dt) {
    using S = kke::Combatant::State;
    // Where the players are (a goblin goes for the nearest, and sticks).
    std::vector<glm::vec3> heroAt;
    std::vector<bool> heroUp;
    for (const auto& h : m_heroes) {
        heroAt.push_back(heroFeet(*h));
        heroUp.push_back(h->remote ? h->netState != static_cast<int>(S::Dead) : (h->id && m_combat.get(h->id).alive()));
    }
    const bool client = netClient();
    if (!client) {
        // The nearest `attackers` round each player go in; the rest wait.
        std::vector<std::vector<std::pair<float, Foe*>>> round(m_heroes.size());
        for (auto& fp : m_foes) {
            Foe& f = *fp;
            if (f.dead) continue;
            int best = -1;
            float bestD = 1e9f;
            for (size_t i = 0; i < m_heroes.size(); ++i) {
                if (!heroUp[i]) continue;
                float d = glm::length(glm::vec2(heroAt[i].x - f.position.x, heroAt[i].z - f.position.z));
                if (static_cast<int>(i) == f.target) d *= 0.7f; // stick with who it's after
                if (d < bestD) {
                    bestD = d;
                    best = static_cast<int>(i);
                }
            }
            f.target = best;
            if (best >= 0 && f.type->keep <= 0.0f && !f.type->boss) round[static_cast<size_t>(best)].push_back({ bestD, &f });
        }
        for (auto& list : round) {
            std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < list.size(); ++i) {
                Foe& f = *list[i].second;
                const bool waits = i >= static_cast<size_t>(m_roster.attackers);
                m_ai.setInput(f.agent, "crowded", waits ? 1.0f : 0.0f);
                if (waits) f.waitRadius = 3.2f + 0.8f * std::sqrt(static_cast<float>(i - static_cast<size_t>(m_roster.attackers)));
            }
        }
        bool anyUp = false;
        for (bool up : heroUp) anyUp = anyUp || up;
        for (auto& fp : m_foes) {
            if (fp->dead) continue;
            if (fp->type->keep > 0.0f || fp->type->boss) m_ai.setInput(fp->agent, "crowded", 0.0f);
            m_ai.setInput(fp->agent, "morale", anyUp ? m_morale : 1.0f);
            m_ai.setInput(fp->agent, "health", m_combat.get(fp->id).healthFraction());
        }
        m_ai.update(dt);
        (void)m_ai.takeEvents(); // the moves are chosen here (thinkFoe), not on the AI's swing timer
    }

    for (auto& fp : m_foes) {
        Foe& f = *fp;
        if (f.dead) {
            f.deadTime += dt;
            continue;
        }
        kke::Combatant& c = m_combat.get(f.id);
        f.haste = std::max(0.0f, f.haste - dt);
        f.stoneskin = std::max(0.0f, f.stoneskin - dt);
        if (client) {
            // Where the host says, smoothed.
            const float k = 1.0f - std::exp(-12.0f * dt);
            const glm::vec3 before = f.position;
            f.position += (f.netPos - f.position) * k;
            f.velocity = (f.position - before) / std::max(dt, 1e-4f);
            const float d = std::fmod(f.netYaw - f.yaw + 540.0f, 360.0f) - 180.0f;
            f.yaw += d * k;
            c.place(f.position, dirOf(f.yaw));
            m_ai.setTransform(f.agent, f.position, f.velocity, f.yaw);
            continue;
        }
        if (m_lineup) {
            // Standing in the row, facing the player.
            f.velocity = glm::vec3(0.0f);
            f.yaw = 180.0f;
            c.place(f.position, dirOf(f.yaw));
            m_ai.setTransform(f.agent, f.position, f.velocity, f.yaw);
            continue;
        }
        f.tactic = m_ai.actionName(f.agent);
        thinkFoe(f, dt);
        runFoeMove(f, dt);
        const kke::ai::Agent* a = m_ai.agent(f.agent);
        const Hero* h = f.target >= 0 ? m_heroes[static_cast<size_t>(f.target)].get() : nullptr;
        const glm::vec3 hp = h ? heroAt[static_cast<size_t>(f.target)] : glm::vec3(0.0f);
        const glm::vec3 to(hp.x - f.position.x, 0.0f, hp.z - f.position.z);
        const float dist = glm::length(to);
        const glm::vec3 toDir = dist > 1e-3f ? to / dist : dirOf(f.yaw);
        const float speedK = f.speedScale * (f.haste > 0.0f ? 1.45f : 1.0f);
        const Move* move = f.move >= 0 ? &f.type->moves[static_cast<size_t>(f.move)] : nullptr;

        glm::vec3 want(0.0f);
        switch (c.state()) {
        case S::Idle:
            if (move) {
                // Casting or shooting: stand and face it.
            } else if (f.tactic == "break" && !m_gates.empty()) {
                // Run for the trees out of the nearest gateway: fleeing straight
                // away from the players pins them against the fort's wall.
                const glm::vec2 at(f.position.x, f.position.z);
                glm::vec2 out(m_gates.front().x, m_gates.front().z);
                for (const glm::vec3& g : m_gates)
                    if (glm::dot(glm::vec2(g.x, g.z), at) > glm::dot(out, at)) out = glm::vec2(g.x, g.z);
                const glm::vec2 to = wayTo(m_gates, at, out * 40.0f) - at;
                if (glm::length(to) > 0.1f) want = glm::vec3(to.x, 0.0f, to.y) / glm::length(to) * 4.2f * speedK; // goblin.yml runSpeed
            } else if (!h) {
                if (a) want = a->desiredVelocity * speedK;
            } else if (f.type->keep > 0.0f && f.tactic != "break") {
                // Ranged: keep the distance, step aside now and then.
                const float keep = f.type->keep;
                const glm::vec3 side(toDir.z, 0.0f, -toDir.x);
                if (dist < keep * 0.6f) want = -toDir * 3.0f + side * ((f.agent % 2) ? 0.8f : -0.8f);
                else if (dist > keep * 1.25f && a) want = a->desiredVelocity * speedK;
                else want = side * ((f.agent % 2) ? 0.7f : -0.7f) + toDir * (dist - keep) * 0.4f;
            } else if (f.tactic == "wait_turn") {
                // Jeer from a few metres out, drifting round; from outside the
                // walls, come in through a gateway first.
                const glm::vec2 at(f.position.x, f.position.z), way = wayTo(m_gates, at, glm::vec2(hp.x, hp.z)) - at;
                const glm::vec3 side(toDir.z, 0.0f, -toDir.x);
                want = toDir * std::clamp((dist - f.waitRadius) * 1.5f, -1.5f, 2.5f) + side * ((f.agent % 2 == 0) ? 0.5f : -0.5f);
                if (way != glm::vec2(hp.x, hp.z) - at && glm::length(way) > 0.1f) want = glm::vec3(way.x, 0.0f, way.y) / glm::length(way) * 2.5f;
            } else if (a) {
                want = a->desiredVelocity * speedK;
                // Close enough to swing: don't push into the player.
                if (dist < c.stats().radius + 0.9f) want *= 0.2f;
            }
            break;
        case S::Windup:
            want = toDir * 0.5f;
            if (move && move->kind == Move::Kind::Dash) want = glm::vec3(0.0f);
            break;
        case S::Active:
            if (move && move->kind == Move::Kind::Dash) want = dirOf(f.yaw) * (move->lunge / std::max(0.1f, move->hit.active));
            break;
        default:
            break;
        }
        f.velocity += (want - f.velocity) * std::min(1.0f, (f.type->boss ? 6.0f : 10.0f) * dt);
        if (c.state() == S::Active && move && move->kind == Move::Kind::Dash) f.velocity = want;
        f.push *= std::exp(-5.0f * dt);
        f.position += (f.velocity + f.push) * dt;
        f.position.y = 0.0f;
        // Never through the ruins, never inside a player.
        const float r = c.stats().radius;
        for (const glm::vec4& o : m_obstacles) {
            const float hx = o.z + r, hz = o.w + r;
            const float dx = f.position.x - o.x, dz = f.position.z - o.y;
            if (std::abs(dx) < hx && std::abs(dz) < hz) {
                const float px = hx - std::abs(dx), pz = hz - std::abs(dz);
                if (px < pz) f.position.x += dx < 0.0f ? -px : px;
                else f.position.z += dz < 0.0f ? -pz : pz;
            }
        }
        for (size_t i = 0; i < heroAt.size(); ++i) {
            const glm::vec2 from(f.position.x - heroAt[i].x, f.position.z - heroAt[i].z);
            const float d = glm::length(from), minD = r + 0.4f;
            if (d < minD && d > 1e-4f) {
                f.position.x = heroAt[i].x + from.x / d * minD;
                f.position.z = heroAt[i].z + from.y / d * minD;
            }
        }
        // Face where it goes; the target when close or swinging.
        const float speed = glm::length(glm::vec2(f.velocity.x, f.velocity.z));
        float wantYaw = f.yaw;
        const bool lockedDash = move && move->kind == Move::Kind::Dash && c.state() == S::Active;
        if (h && !lockedDash && (c.state() == S::Windup || move || (dist < 5.0f && f.tactic != "break") || f.type->keep > 0.0f)) wantYaw = yawOf(toDir);
        else if (speed > 0.3f) wantYaw = yawOf(f.velocity);
        const float d = std::fmod(wantYaw - f.yaw + 540.0f, 360.0f) - 180.0f;
        // A swing turns slowly once it's coming (a player can step round it).
        const float turn = c.state() == S::Windup ? 3.0f : lockedDash ? 0.0f : 10.0f;
        f.yaw += d * std::min(1.0f, turn * dt);
        c.place(f.position, dirOf(f.yaw));
        m_ai.setTransform(f.agent, f.position, f.velocity, f.yaw);
        f.health = c.healthFraction();
    }

    // The dead: lie as ragdolls a while, then still, then sink away.
    // Runaways past the trees are gone too.
    for (size_t i = 0; i < m_foes.size();) {
        Foe& f = *m_foes[i];
        const bool fled = !client && !f.dead && f.tactic == "break" && glm::length(glm::vec2(f.position.x, f.position.z)) > 30.0f;
        if (f.dead && f.ragdoll && f.deadTime > 5.0f) {
            m_ragdolls->destroyRagdoll(f.ragdoll);
            f.ragdoll = 0;
            f.frozen = true;
            m_ragdolled.erase(std::remove(m_ragdolled.begin(), m_ragdolled.end(), f.agent), m_ragdolled.end());
        }
        if ((f.dead && f.deadTime > 9.0f) || fled) {
            releaseFoe(f);
            m_foes.erase(m_foes.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void HordeModule::animateFoe(Foe& f, float dt) {
    using S = kke::Combatant::State;
    Puppet& p = f.look;
    if (!p.model || !p.anim || !p.look) return;
    const Rig& r = *p.look->rig;
    if (f.dead) {
        if (f.ragdoll) {
            std::vector<glm::mat4> bodies;
            if (m_ragdolls->ragdollBodyTransforms(f.ragdoll, bodies))
                m_models->setBoneWorldOverride(p.model, kke::poseFromRagdoll(r.data, f.binding, bodies, glm::inverse(m_models->transform(p.model))));
        } else if (!f.frozen) {
            // No ragdoll (none left or none possible): the death clip.
            if (p.lastState != static_cast<int>(S::Dead) && p.death >= 0) p.anim->play(p.death, 0.1f, true);
            p.lastState = static_cast<int>(S::Dead);
            p.anim->update(dt);
            posePuppet(p, p.anim->pose(), p.xf);
        }
        if (f.deadTime > 7.0f) {
            // Into the ground.
            glm::mat4 xf = m_models->transform(p.model);
            xf = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.35f * dt, 0.0f)) * xf;
            m_models->setTransform(p.model, xf);
            for (kke::ModelModule::InstanceId i : { p.right, p.left, p.head, p.back })
                if (i) m_models->setTransform(i, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.35f * dt, 0.0f)) * m_models->transform(i));
        }
        return;
    }
    const kke::Combatant& c = m_combat.get(f.id);
    const glm::mat4 xf = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), f.position), glm::radians(f.yaw + r.yaw), glm::vec3(0, 1, 0)), glm::vec3(f.scale));
    kke::Animator& a = *p.anim;
    const bool client = netClient();
    const int state = client ? f.netState : static_cast<int>(c.state());
    const int moveIndex = client ? f.netMove : f.move;
    const int comboHit = client ? f.netComboHit : f.comboHit;
    const bool entered = state != p.lastState;
    p.lastState = state;
    const Move* move = moveIndex >= 0 && moveIndex < static_cast<int>(f.type->moves.size()) ? &f.type->moves[static_cast<size_t>(moveIndex)] : nullptr;
    auto moveState = [&](int k) {
        const size_t i = static_cast<size_t>(moveIndex) * kPerMove + static_cast<size_t>(k);
        return moveIndex >= 0 && i < p.moves.size() ? p.moves[i] : -1;
    };
    switch (static_cast<S>(state)) {
    case S::Windup:
        if (entered && move) {
            const int roar = move->kind == Move::Kind::Combo ? -1 : moveState(1);
            const int blow = moveState(move->kind == Move::Kind::Combo ? std::min(comboHit, kPerMove - 1) : 0);
            f.roared = roar < 0;
            if (roar >= 0) a.play(roar, 0.15f, true);
            else if (blow >= 0) a.play(blow, 0.08f, true);
        }
        // The super: done roaring, now the blow.
        if (move && !f.roared && c.stateTime() >= move->hit.windup * 0.7f) {
            f.roared = true;
            if (moveState(0) >= 0) a.play(moveState(0), 0.12f, true);
        }
        break;
    case S::Stunned:
        if (entered && p.hit >= 0) a.play(p.hit, 0.05f, true);
        break;
    case S::Knockdown:
        if (entered && p.knock >= 0) a.play(p.knock, 0.05f, true);
        if (!client && c.stateTime() > c.stateLength() - 0.8f && a.current() != p.getUp && p.getUp >= 0) a.play(p.getUp, 0.2f, true);
        break;
    case S::Idle: {
        // Casting has its own clip (played when it began); else walk / run.
        const bool casting = move && !usesCombat(move->kind);
        if (client && casting && entered) {
            const int aim = moveState(2);
            if ((aim >= 0 ? aim : moveState(0)) >= 0) a.play(aim >= 0 ? aim : moveState(0), 0.1f, true);
        }
        const bool busy = casting || (!a.finished() && a.current() != p.move && a.current() != p.idle && a.current() != p.cheer);
        if (!busy) {
            int want = p.move;
            const float speed = glm::length(glm::vec2(f.velocity.x, f.velocity.z)) / f.scale;
            if (f.tactic == "wait_turn" && speed < 0.4f && p.cheer >= 0) want = p.cheer;
            if (want >= 0 && a.current() != want) a.play(want, 0.25f);
            a.setParameter(speed);
        }
        break;
    }
    default:
        break;
    }
    if (m_lineup && !p.moves.empty()) {
        // Each move's states in turn, 2.5 s each.
        std::vector<int> states;
        for (int st : p.moves)
            if (st >= 0) states.push_back(st);
        const int want = states.empty() ? -1 : states[static_cast<size_t>(m_clock / 2.5f) % states.size()];
        if (want >= 0 && a.current() != want) a.play(want, 0.1f, true);
        else if (want >= 0 && a.finished()) a.play(want, 0.0f, true);
    }
    a.update(dt);
    kke::Pose pose = a.pose();
    if (!m_poseClip.empty())
        if (const int clip = r.clip(m_poseClip); clip >= 0) r.set->sample(clip, m_clock, true, pose);
    if (p.flinch > 0.0f) {
        const glm::vec3 awayModel = glm::vec3(glm::inverse(xf) * glm::vec4(p.flinchDir, 0.0f));
        applyFlinch(r.data, pose, r.spine, awayModel, p.flinch, f.type->boss ? 8.0f : 30.0f);
        p.flinch = std::max(0.0f, p.flinch - dt * 3.5f);
    }
    posePuppet(p, pose, xf);
    // Hurt: a red flash. Buffed: stone grey or war-cry red.
    glm::vec3 tint = p.tint;
    if (f.stoneskin > 0.0f) tint *= glm::vec3(0.75f, 0.78f, 0.85f);
    if (f.haste > 0.0f) tint *= glm::vec3(1.25f, 0.85f, 0.8f);
    if (p.hurtFlash > 0.0f) {
        p.hurtFlash = std::max(0.0f, p.hurtFlash - dt * 5.0f);
        tint = glm::mix(tint, glm::vec3(2.2f, 0.6f, 0.5f), p.hurtFlash);
    }
    m_models->setTint(p.model, tint);
}

} // namespace horde
