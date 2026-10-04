// The players: who they are (picked in the start menu), how they fight
// with each weapon, and how they move and look. A player at this screen
// reads their own input map (InputModule player from the lobby seat); an
// online player is drawn from what their screen sends (Net.cpp).

#include "HordeModule.h"

#include "Flinch.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace horde {

namespace {

constexpr float kRun = 4.6f, kBlockWalk = 1.6f, kAimWalk = 2.4f, kRollSpeed = 6.5f;
const glm::vec3 kColors[] = { { 0.95f, 0.72f, 0.25f }, { 0.35f, 0.65f, 1.0f }, { 0.4f, 0.85f, 0.4f }, { 0.9f, 0.4f, 0.75f } };

float yawOf(const glm::vec3& v) { return glm::degrees(std::atan2(v.x, v.z)); }

glm::vec3 flatDir(glm::vec3 v, const glm::vec3& fallback) {
    v.y = 0.0f;
    const float l = glm::length(v);
    return l > 1e-4f ? v / l : fallback;
}

// The weapon's moves, in the order of Puppet::moves.
enum MoveSlot { kCombo1 = 0, kCombo2, kCombo3, kHeavy, kSpin, kKick, kSlots };

} // namespace

const Weapon& HordeModule::weaponOf(const Hero& h) const {
    return m_roster.weapons[static_cast<size_t>(std::clamp(h.weapon, 0, static_cast<int>(m_roster.weapons.size()) - 1))];
}

glm::vec3 HordeModule::heroFeet(const Hero& h) const {
    if (h.remote || !h.body) return h.position;
    return m_rigid->world().characterDrawPosition(h.body, m_app->fixedAlpha());
}

HordeModule::Hero* HordeModule::heroByCombatant(kke::CombatantId id) {
    for (auto& h : m_heroes)
        if (h->id == id) return h.get();
    return nullptr;
}

// The players at this screen: the lobby's joined seats (one player
// without a lobby), with what they picked.
std::vector<HordeModule::Entry> HordeModule::wantedHeroes() const {
    std::vector<Entry> out;
    std::vector<int> seats = { 0 };
    if (m_lobby) seats = m_lobby->lobby().joinedSeats();
    if (seats.empty()) seats = { 0 };
    for (int seat : seats) {
        Entry e;
        e.seat = seat;
        std::vector<int> look;
        if (m_lobby) look = m_lobby->lobby().seat(seat).look;
        auto pick = [&](size_t i, int count) { return i < look.size() ? std::clamp(look[i], 0, std::max(0, count - 1)) : 0; };
        e.name = m_lobby ? m_lobby->lobby().seatName(seat) : "Player 1";
        e.character = pick(1, static_cast<int>(m_roster.characters.size()));
        e.skin = pick(2, 6);
        e.accessory = pick(3, static_cast<int>(m_roster.accessories.size()));
        e.weapon = pick(4, static_cast<int>(m_roster.weapons.size()));
        e.color = kColors[pick(5, 4)];
        if (seat == seats.front() && !m_startWeapon.empty() && m_roster.weaponIndex(m_startWeapon) >= 0) e.weapon = m_roster.weaponIndex(m_startWeapon);
        out.push_back(e);
    }
    if (netHost() || netClient())
        for (const kke::net::RemotePlayer& p : m_net->remotePlayers()) {
            Entry e;
            e.remote = true;
            e.netId = p.id;
            e.name = p.name;
            // "character,skin,accessory,weapon,colour" (Net.cpp).
            int v[5] = { 0, 0, 0, 0, 0 };
            std::sscanf(p.character.c_str(), "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
            e.character = std::clamp(v[0], 0, static_cast<int>(m_roster.characters.size()) - 1);
            e.skin = std::clamp(v[1], 0, 5);
            e.accessory = std::clamp(v[2], 0, static_cast<int>(m_roster.accessories.size()) - 1);
            e.weapon = std::clamp(v[3], 0, static_cast<int>(m_roster.weapons.size()) - 1);
            e.color = kColors[std::clamp(v[4], 0, 3)];
            out.push_back(e);
        }
    return out;
}

void HordeModule::buildHeroes(const std::vector<Entry>& entries) {
    for (auto& h : m_heroes) removeHero(*h);
    m_heroes.clear();
    int slot = 0;
    for (const Entry& e : entries) {
        auto h = std::make_unique<Hero>();
        h->seat = e.seat;
        h->remote = e.remote;
        h->netId = e.netId;
        h->name = e.name;
        h->color = e.color;
        h->character = e.character;
        h->skin = e.skin;
        h->accessory = e.accessory;
        h->weapon = e.weapon;
        if (!e.remote) {
            h->slot = slot++;
            h->player = m_lobby && e.seat >= 0 ? std::max(0, m_lobby->playerOf(e.seat)) : 0;
            h->bot = m_bot && h->slot == 0;
        }
        const float a = 6.283f * static_cast<float>(m_heroes.size()) / static_cast<float>(std::max<size_t>(1, entries.size()));
        spawnHero(*h, glm::vec3(std::sin(a) * 1.6f, 0.05f, 2.0f + std::cos(a) * 1.6f));
        m_heroes.push_back(std::move(h));
    }
    if (m_net && !netClient() && !netHost()) m_net->playerName = entries.empty() ? "Player" : entries.front().name;
}

void HordeModule::spawnHero(Hero& h, const glm::vec3& at) {
    kke::CombatStats st = kke::CombatStats::fighter();
    st.maxHealth = 260.0f;
    st.maxStamina = 120.0f;
    st.staminaRegen = 40.0f;
    st.maxPoise = 120.0f;
    st.dodgeTime = 0.45f;
    st.dodgeCost = 16.0f;
    st.knockdownTime = 1.6f;
    st.blockAngle = 80.0f;
    h.id = m_combat.add(0, st);
    h.position = at;
    if (!h.remote) {
        kke::RigidWorld::CharacterDesc cd;
        cd.radius = 0.35f;
        cd.height = 1.8f;
        cd.position = at;
        h.body = m_rigid->world().addCharacter(cd);
    }
    h.agent = m_nextHeroAgent++;
    m_ai.addActor(h.agent, "hero", at);
    m_ai.setTeam(h.agent, 1);
    h.rig.mode = kke::CameraRig::Mode::ThirdPerson;
    h.rig.settings.armLength = 4.6f;
    h.rig.settings.pivotHeight = 1.7f;
    h.rig.settings.shoulderOffset = 0.0f;
    h.rig.settings.fovDegrees = 60.0f;
    h.rig.pitch = -18.0f;
    h.rig.yaw = 180.0f;
    dressHero(h);
}

void HordeModule::removeHero(Hero& h) {
    undressPuppet(h.look);
    if (h.body) m_rigid->world().removeCharacter(h.body);
    h.body = 0;
    if (h.id) m_combat.remove(h.id);
    h.id = 0;
    m_ai.remove(h.agent);
}

void HordeModule::dressHero(Hero& h) {
    const HeroCharacter& ch = m_roster.characters[static_cast<size_t>(std::clamp(h.character, 0, static_cast<int>(m_roster.characters.size()) - 1))];
    const Look* look = ch.art.empty() ? nullptr : m_art->character(ch.art, h.skin);
    glm::vec3 tint(1.0f);
    if (!look) {
        // The mannequin, in the player's colour (a skin is a shade of it).
        look = m_art->mannequin(h.color * (1.0f - 0.12f * static_cast<float>(h.skin % 4)));
    }
    dressPuppet(h.look, look, tint, 1.0f);
    if (!h.look.model) return;
    const Rig& r = *look->rig;
    const Accessory& acc = m_roster.accessories[static_cast<size_t>(std::clamp(h.accessory, 0, static_cast<int>(m_roster.accessories.size()) - 1))];
    if (!acc.art.empty())
        if (const kke::ModelModule::ModelId m = m_art->prop(acc.art)) {
            const bool onBack = acc.on == "back";
            kke::ModelModule::InstanceId& slot = onBack ? h.look.back : h.look.head;
            slot = m_models->spawn(m, glm::mat4(1.0f));
            m_models->setOverlayEnabled(slot, false);
            (onBack ? h.look.backAt : h.look.headAt) = m_art->attach(r, onBack ? r.chest : r.head);
        }
    h.ik = std::make_unique<kke::CharacterIk>(r.data);
    equip(h, h.weapon);
}

void HordeModule::equip(Hero& h, int weapon) {
    h.weapon = std::clamp(weapon, 0, static_cast<int>(m_roster.weapons.size()) - 1);
    h.combo = 0;
    h.queued = false;
    h.drawing = false;
    h.draw = 0.0f;
    h.reload = 0.0f;
    h.heavyHeld = -1.0f;
    h.charged = false;
    Puppet& p = h.look;
    for (kke::ModelModule::InstanceId* i : { &p.right, &p.left })
        if (*i) {
            m_models->remove(*i);
            *i = 0;
        }
    if (!p.model || !p.look || !p.anim) return;
    const Weapon& w = weaponOf(h);
    const Rig& r = *p.look->rig;
    if (const kke::ModelModule::ModelId m = m_art->prop(w.prop)) {
        const bool left = w.kind == Weapon::Kind::Bow;
        kke::ModelModule::InstanceId& slot = left ? p.left : p.right;
        slot = m_models->spawn(m, glm::mat4(1.0f));
        m_models->setOverlayEnabled(slot, false);
        (left ? p.leftGrip : p.rightGrip) = m_art->grip(r, w.prop.grip, left);
    }
    p.twoHanded = w.kind == Weapon::Kind::TwoHand || w.kind == Weapon::Kind::Crossbow;
    // A state per move, timed to the move.
    kke::Animator& a = *p.anim;
    p.moves.assign(kSlots, -1);
    auto add = [&](int slot, const Move& m, const std::string& state) {
        const int c = r.clip(m.clips);
        if (c < 0) return;
        const float t = m.total() > 0.0f ? m.total() : r.set->duration(c);
        p.moves[static_cast<size_t>(slot)] = a.addClipState(state + std::to_string(h.weapon), c, false, r.set->duration(c) / t);
    };
    for (size_t i = 0; i < w.combo.size() && i < 3; ++i) add(kCombo1 + static_cast<int>(i), w.combo[i], "combo" + std::to_string(i));
    add(kHeavy, w.heavy, "heavy");
    add(kSpin, w.spin, "spin");
    add(kKick, w.kick, "kick");
}

void HordeModule::startMove(Hero& h, const Move& m, int state) {
    kke::Combatant& c = m_combat.get(h.id);
    if (!c.canAct()) return;
    // Swings aim at the nearest goblin in front (a soft lock), else ahead.
    const glm::vec3 feet = heroFeet(h);
    const glm::vec3 ahead = glm::length(h.wish) > 0.2f ? glm::normalize(h.wish) : h.facing;
    float best = 3.5f;
    glm::vec3 dir = ahead;
    for (const auto& f : m_foes) {
        if (f->dead) continue;
        const glm::vec3 to = f->position - feet;
        const float d = glm::length(glm::vec2(to.x, to.z)) - (f->type && f->type->boss ? 1.5f : 0.0f);
        if (d < best && glm::dot(flatDir(to, ahead), ahead) > 0.2f) {
            best = d;
            dir = flatDir(to, ahead);
        }
    }
    h.facing = dir;
    c.place(feet, h.facing);
    if (!c.attack(m.hit)) return;
    h.moveState = state;
    h.attack = m.hit.name;
    h.sinceSwing = 0.0f;
    h.spin = 0.0f;
}

void HordeModule::shootArrow(Hero& h, float drawFraction) {
    const Weapon& w = weaponOf(h);
    const bool bow = w.kind == Weapon::Kind::Bow;
    const float f = std::clamp(drawFraction, 0.0f, 1.0f);
    // Where the camera aims (what's under the crosshair), from the bow.
    glm::vec3 target;
    if (h.bot) {
        const Foe* best = nullptr;
        float bestD = 30.0f;
        for (const auto& fo : m_foes)
            if (!fo->dead) {
                const float d = glm::distance(fo->position, heroFeet(h));
                if (d < bestD) {
                    bestD = d;
                    best = fo.get();
                }
            }
        target = best ? best->position + glm::vec3(0.0f, 1.0f * best->scale, 0.0f) : heroFeet(h) + h.facing * 20.0f + glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        const glm::vec3 eye = h.camera.position;
        const glm::vec3 look = glm::normalize(h.camera.target - h.camera.position);
        float t = 80.0f;
        const auto hit = m_rigid->world().raycast(eye + look * 1.0f, look, 80.0f);
        if (hit.hit) t = hit.distance + 1.0f;
        // A goblin under the crosshair (a little help on a controller).
        const bool pad = m_input->promptStyle(h.player) != kke::PromptStyle::Keyboard;
        float bestCos = pad ? std::cos(glm::radians(4.0f)) : 2.0f;
        for (const auto& fo : m_foes) {
            if (fo->dead) continue;
            const glm::vec3 chest = fo->position + glm::vec3(0.0f, 1.0f * fo->scale, 0.0f);
            const glm::vec3 to = chest - eye;
            const float d = glm::length(to);
            if (d > t || d < 1.0f) continue;
            const float cosA = glm::dot(to / d, look);
            if (cosA > bestCos) {
                bestCos = cosA;
                t = d;
                target = chest;
            }
        }
        if (bestCos > 1.5f || !pad || bestCos < std::cos(glm::radians(4.0f))) target = eye + look * t;
    }
    Shot s;
    s.kind = bow ? "arrow" : "bolt";
    s.team = 0;
    for (size_t i = 0; i < m_heroes.size(); ++i)
        if (m_heroes[i].get() == &h) s.hero = static_cast<int>(i);
    s.from = h.id;
    const glm::vec3 feet = heroFeet(h);
    const glm::vec3 start = (h.look.model ? (bow ? h.look.offhandPos : h.look.handPos) : feet + glm::vec3(0.0f, 1.45f, 0.0f)) + h.facing * 0.3f;
    const float speed = bow ? glm::mix(w.minSpeed, w.maxSpeed, f) : w.speed;
    s.position = start;
    s.velocity = glm::normalize(target - start) * speed;
    s.gravity = bow ? 9.8f * 0.5f : 9.8f * 0.25f;
    s.hit = kke::AttackDesc::light();
    s.hit.name = bow ? "arrow" : "bolt";
    s.hit.damage = bow ? glm::mix(w.minDamage, w.maxDamage, f * f) : w.damage;
    s.hit.poiseDamage = s.hit.damage * 0.8f;
    s.hit.knockback = bow ? 1.0f + 3.0f * f : 3.0f;
    s.hit.hitStun = 0.35f;
    s.headshot = w.headshot;
    s.life = 5.0f;
    fire(std::move(s));
    ++m_shotsFired;
}

void HordeModule::heroBot(Hero& h, glm::vec2& move, bool& light, bool& heavyDown, bool& heavyHeld, bool& block, bool& roll, bool& aim) {
    // For headless runs and the attract mode: stand near the middle and
    // cut down whatever comes; the heavy or the spin when it's crowded,
    // back off to breathe when out of stamina. With a bow: keep shooting.
    const kke::Combatant& c = m_combat.get(h.id);
    const glm::vec3 feet = heroFeet(h);
    const Foe* nearest = nullptr;
    float best = 1e9f;
    int close = 0;
    bool windup = false;
    for (const auto& f : m_foes) {
        if (f->dead) continue;
        const float d = glm::length(glm::vec2(f->position.x - feet.x, f->position.z - feet.z)) - (f->type->boss ? 1.5f : 0.0f);
        if (d < 2.4f) ++close;
        if (d < 2.0f && m_combat.get(f->id).state() == kke::Combatant::State::Windup) windup = true;
        if (d < best) {
            best = d;
            nearest = f.get();
        }
    }
    const Weapon& w = weaponOf(h);
    glm::vec3 want(0.0f);
    if (w.ranged()) {
        // Keep some room, shoot what's nearest.
        if (nearest && best < 5.0f) want = feet - nearest->position;
        else if (glm::length(glm::vec2(feet.x, feet.z)) > 3.0f) want = -feet;
        aim = nearest && best < 30.0f;
        if (aim) h.facing = flatDir(nearest->position - feet, h.facing);
        const bool ready = w.kind == Weapon::Kind::Bow ? (h.drawing ? h.draw < w.draw * 0.9f : true) : h.reload <= 0.0f;
        light = aim && ready;
        heavyDown = nearest && best < 1.3f;
        heavyHeld = false;
    } else {
        // Inside the walls; archers and shamans wherever they stand.
        const bool nearMiddle = nearest && (glm::length(glm::vec2(nearest->position.x, nearest->position.z)) < 12.0f || nearest->type->keep > 0.0f);
        if (nearMiddle && best > 1.4f && c.staminaFraction() > 0.2f) want = nearest->position - feet;
        else if (glm::length(glm::vec2(feet.x, feet.z)) > 2.5f) want = -feet;
        // Hold heavy for a spin when surrounded.
        const bool spinNow = close >= 4 && c.staminaFraction() > 0.5f;
        heavyHeld = spinNow && (h.heavyHeld < 0.0f || !h.charged);
        heavyDown = spinNow && h.heavyHeld < 0.0f;
        light = !spinNow && h.heavyHeld < 0.0f && nearest && best < 1.9f && c.staminaFraction() > 0.15f;
        aim = false;
    }
    want.y = 0.0f;
    if (glm::length(want) > 0.2f) want = glm::normalize(want);
    // In the camera's frame (forward = away from the camera), as a stick would be.
    move = glm::vec2(glm::dot(want, h.rig.right()), glm::dot(want, flatDir(h.rig.forward(), glm::vec3(0, 0, -1))));
    block = windup && !light && !heavyDown && !w.ranged();
    roll = false;
}

void HordeModule::updateHero(Hero& h, float dt) {
    using S = kke::Combatant::State;
    if (h.remote || !h.id) {
        if (h.id) {
            kke::Combatant& c = m_combat.get(h.id);
            c.place(h.position, h.facing);
        }
        m_ai.setTransform(h.agent, h.position, h.velocity, yawOf(h.facing));
        return;
    }
    kke::RigidWorld& w = m_rigid->world();
    kke::Combatant& c = m_combat.get(h.id);
    kke::InputMap& in = m_input->map(h.player);
    const glm::vec3 feet = w.characterPosition(h.body);
    h.position = feet;
    h.velocity = w.characterVelocity(h.body);
    h.sinceSwing += dt;
    const bool down = c.state() == kke::Combatant::State::Knockdown;
    if (h.wasDown && !down) h.upGrace = 1.2f;
    h.wasDown = down;
    h.upGrace = std::max(0.0f, h.upGrace - dt);
    const Weapon& wpn = weaponOf(h);

    glm::vec2 move(0.0f);
    bool light = false, lightHeld = false, lightReleased = false, heavyDown = false, heavyHeld = false, block = false, roll = false, aim = false;
    const bool inMenu = m_inventoryHero >= 0 && m_heroes[static_cast<size_t>(m_inventoryHero)].get() == &h;
    const bool playing = m_phase != Phase::Lobby && m_phase != Phase::Overrun && c.alive() && !inMenu;
    if (playing) {
        if (h.bot) {
            heroBot(h, move, light, heavyDown, heavyHeld, block, roll, aim);
            lightHeld = light;
            lightReleased = h.drawing && !light;
        } else {
            move = in.axis2("move");
            const bool mouseOk = h.slot != 0 || m_captured || m_input->promptStyle(h.player) != kke::PromptStyle::Keyboard;
            light = in.pressed("horde.attack") && mouseOk;
            lightHeld = in.held("horde.attack") && mouseOk;
            lightReleased = h.drawing && !lightHeld;
            heavyDown = in.pressed("horde.heavy");
            heavyHeld = in.held("horde.heavy");
            block = in.held("horde.block");
            roll = in.pressed("horde.roll");
            aim = in.held("horde.aim");
            if (in.pressed("horde.swap")) equip(h, (h.weapon + 1) % static_cast<int>(m_roster.weapons.size()));
            if (in.pressed("horde.inventory")) {
                for (size_t i = 0; i < m_heroes.size(); ++i)
                    if (m_heroes[i].get() == &h) openInventory(static_cast<int>(i));
            }
        }
    }
    const glm::vec3 camFwd = flatDir(h.rig.forward(), glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 wish = camFwd * move.y + h.rig.right() * move.x;
    wish.y = 0.0f;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    h.wish = wish;

    if (!c.alive()) {
        h.downTime += dt;
        h.drawing = h.aiming = h.charged = false;
    }
    // ---- What the weapon does with the buttons.
    h.aiming = false;
    if (wpn.ranged() && playing) {
        h.reload = std::max(0.0f, h.reload - dt);
        h.aiming = aim || h.drawing || (wpn.kind == Weapon::Kind::Crossbow && lightHeld);
        if (wpn.kind == Weapon::Kind::Bow) {
            if (lightHeld && !h.drawing && c.canAct() && h.reload <= 0.0f) {
                h.drawing = true;
                h.draw = 0.0f;
            }
            if (h.drawing) {
                h.draw += dt;
                if (lightReleased || !c.canAct()) {
                    if (c.canAct()) {
                        shootArrow(h, h.draw / wpn.draw);
                        h.reload = 0.25f; // the next arrow to the string
                    }
                    h.drawing = false;
                    h.draw = 0.0f;
                }
            }
        } else if (light && h.reload <= 0.0f && c.canAct()) {
            shootArrow(h, 1.0f);
            h.reload = wpn.reload;
        }
        if (heavyDown && c.canAct()) startMove(h, wpn.kick, kKick);
    } else if (playing) {
        // Melee: a press during a swing queues the next hit of the combo.
        const bool swinging = c.state() == S::Windup || c.state() == S::Active || c.state() == S::Recovery;
        if (light && swinging) h.queued = true;
        if (c.canAct() && h.heavyHeld < 0.0f) {
            if (h.sinceSwing > 0.9f || h.combo >= static_cast<int>(wpn.combo.size())) h.combo = 0;
            if ((light || h.queued) && !wpn.combo.empty()) {
                const int step = std::min(h.combo, static_cast<int>(wpn.combo.size()) - 1);
                startMove(h, wpn.combo[static_cast<size_t>(step)], kCombo1 + step);
                ++h.combo;
            }
            h.queued = false;
        }
        // Heavy: a tap swings heavy, held long enough it charges a spin.
        if (heavyDown && c.canAct()) {
            h.heavyHeld = 0.0f;
            h.charged = false;
        }
        if (h.heavyHeld >= 0.0f) {
            if (heavyHeld) {
                h.heavyHeld += dt;
                if (!h.charged && h.heavyHeld >= std::max(0.2f, wpn.spin.charge)) {
                    h.charged = true;
                    if (m_fx && h.look.model) m_fx->sparks(h.look.handPos, glm::vec3(0, 1, 0), 18, 3.0f);
                }
            } else {
                if (h.charged) startMove(h, wpn.spin, kSpin);
                else startMove(h, wpn.heavy, kHeavy);
                h.heavyHeld = -1.0f;
                h.charged = false;
                h.combo = 0;
            }
        }
        c.setBlocking(block && h.heavyHeld < 0.0f);
    }
    if (!playing) {
        h.heavyHeld = -1.0f;
        h.charged = false;
        h.drawing = false;
        c.setBlocking(false);
    }
    if (roll && c.canAct() && c.dodge()) {
        if (glm::length(wish) > 0.2f) h.facing = glm::normalize(wish);
        h.drawing = false;
        h.heavyHeld = -1.0f;
        h.charged = false;
    }

    // ---- Footwork per state.
    glm::vec3 vel(0.0f);
    switch (c.state()) {
    case S::Idle: {
        const float speed = c.blocking() ? kBlockWalk : (h.aiming || h.heavyHeld >= 0.0f) ? kAimWalk : kRun;
        vel = wish * speed;
        if (h.aiming) {
            h.facing = camFwd; // strafe while aiming
        } else if (glm::length(wish) > 0.1f && !c.blocking()) {
            const glm::vec3 want = glm::normalize(wish);
            h.facing = flatDir(h.facing + (want - h.facing) * (1.0f - std::exp(-12.0f * dt)), want);
        }
        break;
    }
    case S::Windup:
    case S::Active: {
        const bool spin = h.attack.find("_spin") != std::string::npos;
        vel = h.facing * (spin ? 1.2f : h.attack.find("_combo") != std::string::npos ? 1.4f : 0.3f) + (spin ? wish * 2.0f : glm::vec3(0.0f));
        if (spin && c.state() == S::Active) h.spin = std::min(360.0f, h.spin + 360.0f * dt / std::max(0.1f, c.currentAttack().active));
        break;
    }
    case S::Dodging:
        vel = h.facing * kRollSpeed;
        break;
    default:
        break;
    }
    if (c.state() != S::Active && c.state() != S::Windup) h.spin = 0.0f;
    h.push *= std::exp(-6.0f * dt);
    vel += h.push;
    kke::RigidWorld::CharacterInput ci;
    ci.move = vel;
    w.setCharacterInput(h.body, ci);
    c.place(feet, h.facing);
    // A spin hits all round (the sphere on the player); a swing where the
    // blade is, else the move's reach in front.
    if (c.state() == S::Active && h.attack.find("_spin") != std::string::npos) c.setStrikePoint(feet + glm::vec3(0.0f, 1.0f, 0.0f));
    else if (h.look.model && c.state() == S::Active && h.look.handPos != glm::vec3(0.0f) && c.currentAttack().reach > 0.5f)
        c.setStrikePoint(h.look.handPos + h.facing * 0.5f);
    else c.clearStrikePoint();
    m_ai.setTransform(h.agent, feet, h.velocity, yawOf(h.facing));
    // Down: the goblins stop caring.
    m_ai.setEnabled(h.agent, c.alive());
}

void HordeModule::animateHero(Hero& h, float dt) {
    using S = kke::Combatant::State;
    Puppet& p = h.look;
    if (!p.model || !p.anim || !p.look) return;
    const Rig& r = *p.look->rig;
    const kke::Combatant& c = m_combat.get(h.id);
    const glm::vec3 feet = heroFeet(h);
    const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yawOf(h.facing) + r.yaw + h.spin), glm::vec3(0, 1, 0));

    kke::Animator& a = *p.anim;
    const int state = h.remote ? h.netState : static_cast<int>(c.state());
    const bool entered = state != p.lastState;
    p.lastState = state;
    auto playMove = [&](int slot) {
        if (slot >= 0 && slot < static_cast<int>(p.moves.size()) && p.moves[static_cast<size_t>(slot)] >= 0)
            a.play(p.moves[static_cast<size_t>(slot)], 0.06f, true);
    };
    const bool busy = !a.finished() && a.current() != p.move && a.current() != p.idle && a.current() != p.strafeL && a.current() != p.strafeR &&
                      a.current() != p.back_ && a.current() != p.block;
    switch (static_cast<S>(state)) {
    case S::Windup:
        if (entered) playMove(h.moveState);
        break;
    case S::Dodging:
        if (entered && p.roll >= 0) a.play(p.roll, 0.05f, true);
        break;
    case S::Stunned:
        if (entered && p.hit >= 0) a.play(p.hit, 0.05f, true);
        break;
    case S::Knockdown:
        if (entered && p.knock >= 0) a.play(p.knock, 0.05f, true);
        if (!h.remote && c.stateTime() > c.stateLength() - 0.8f && a.current() != p.getUp && p.getUp >= 0) a.play(p.getUp, 0.2f, true);
        break;
    case S::Dead:
        if (entered && p.death >= 0) a.play(p.death, 0.1f, true);
        break;
    case S::Idle: {
        const glm::vec3 v = h.remote ? h.velocity : m_rigid->world().characterVelocity(h.body);
        const float speed = glm::length(glm::vec2(v.x, v.z));
        int want = p.move;
        if (h.aiming && speed > 0.5f) {
            // Strafing: which way the legs go, relative to the facing.
            const glm::vec3 dir = glm::normalize(glm::vec3(v.x, 0.0f, v.z));
            const float fwd = glm::dot(dir, h.facing), side = glm::dot(dir, glm::vec3(h.facing.z, 0.0f, -h.facing.x));
            if (fwd < -0.5f && p.back_ >= 0) want = p.back_;
            else if (std::abs(side) > 0.6f) want = side > 0.0f ? (p.strafeL >= 0 ? p.strafeL : p.move) : (p.strafeR >= 0 ? p.strafeR : p.move);
        }
        if (c.blocking() && p.block >= 0) {
            if (a.current() != p.block) a.play(p.block, 0.08f, true);
        } else if (!busy && a.current() != want && want >= 0) {
            a.play(want, 0.2f);
        }
        a.setParameter(speed);
        break;
    }
    default:
        break;
    }
    a.update(dt);
    kke::Pose pose = a.pose();
    // Aiming a bow or crossbow, reloading: the arms on top of the legs.
    if (h.look.look) {
        const Weapon& w = weaponOf(h);
        const bool reloading = w.kind == Weapon::Kind::Crossbow && h.reload > 0.0f;
        if ((h.aiming || reloading) && static_cast<S>(state) == S::Idle) {
            const int clip = reloading ? r.clip(std::vector<std::string>{ "Pistol_Reload", "Interact" })
                                       : h.drawing && h.draw < 0.3f ? r.clip("Bow_Notch") : r.clip(std::vector<std::string>{ "Bow_Aim_Neutral", "Pistol_Aim_Neutral" });
            if (clip >= 0) {
                kke::Pose upper;
                const float t = reloading ? (1.0f - h.reload / std::max(0.1f, w.reload)) * r.set->duration(clip) : h.drawing && h.draw < 0.3f ? h.draw : m_clock;
                r.set->sample(clip, t, !reloading, upper);
                pose = overlay(r, pose, upper, 1.0f);
            }
        }
    }
    if (p.flinch > 0.0f) {
        const glm::vec3 awayModel = glm::vec3(glm::inverse(xf) * glm::vec4(p.flinchDir, 0.0f));
        applyFlinch(r.data, pose, r.spine, awayModel, p.flinch, 14.0f);
        p.flinch = std::max(0.0f, p.flinch - dt * 4.0f);
    }
    // Contacts: feet on the ground, the second hand on a two-handed haft.
    if (h.ik && h.ik->valid()) {
        kke::RigidWorld& world = m_rigid->world();
        if (p.twoHanded && p.right && r.handR >= 0 && static_cast<S>(state) != S::Dead && static_cast<S>(state) != S::Knockdown) {
            const std::vector<glm::mat4> bones = kke::poseToModel(r.data, pose);
            const glm::mat4 weapon = xf * bones[static_cast<size_t>(r.handR)] * p.rightGrip;
            // Further up the haft; under the front of a crossbow's stock.
            const bool crossbow = weaponOf(h).kind == Weapon::Kind::Crossbow;
            const glm::vec3 grip = crossbow ? glm::vec3(weapon[3]) + h.facing * 0.25f - glm::vec3(0.0f, 0.04f, 0.0f)
                                            : glm::vec3(weapon * glm::vec4(0.0f, 0.32f, 0.0f, 1.0f));
            h.ik->hand(kke::CharacterIk::Left, grip);
        }
        h.ik->apply(r.data, pose, xf, [&world](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            const auto ray = world.raycast(from, glm::vec3(0.0f, -1.0f, 0.0f), 3.0f);
            if (!ray.hit) return false;
            hit = from + glm::vec3(0.0f, -ray.distance, 0.0f);
            normal = ray.normal;
            return true;
        }, h.remote ? h.velocity : m_rigid->world().characterVelocity(h.body), dt);
    }
    posePuppet(p, pose, xf);
    if (p.hurtFlash > 0.0f) {
        p.hurtFlash = std::max(0.0f, p.hurtFlash - dt * 5.0f);
        m_models->setTint(p.model, glm::mix(glm::vec3(1.0f), glm::vec3(2.2f, 0.6f, 0.5f), p.hurtFlash));
    }
}

} // namespace horde
