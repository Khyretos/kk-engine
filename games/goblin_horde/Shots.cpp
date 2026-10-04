// What flies and what bursts: arrows and bolts (the players' and the
// archers'), fireballs, zaps and boulders; the warnings drawn on the ground
// before something lands; and what happens to whoever gets hit.

#include "HordeModule.h"

#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace horde {

namespace {

constexpr float kPi = 3.14159265f;

// Where along segment a-b a sphere of `r` at `c` is first touched (0..1), or -1.
float segmentSphere(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float r) {
    const glm::vec3 d = b - a;
    const float len2 = glm::dot(d, d);
    float t = len2 > 1e-8f ? glm::clamp(glm::dot(c - a, d) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::distance(a + d * t, c) <= r ? t : -1.0f;
}

bool isArrow(const std::string& kind) { return kind == "arrow" || kind == "bolt"; }

} // namespace

glm::vec3 HordeModule::ballistic(const glm::vec3& from, const glm::vec3& to, float time, float gravity) const {
    const float t = std::max(0.1f, time);
    return (to - from) / t + glm::vec3(0.0f, 0.5f * gravity * t, 0.0f);
}

void HordeModule::fire(Shot s) {
    if (isArrow(s.kind) && m_arrowModel) {
        if (!m_arrowPool.empty()) {
            s.model = m_arrowPool.back();
            m_arrowPool.pop_back();
            m_models->setVisible(s.model, true);
        } else {
            s.model = m_models->spawn(m_arrowModel, glm::mat4(1.0f));
            m_models->setOverlayEnabled(s.model, false);
        }
    }
    if (s.local && (netHost() || (netClient() && s.team == 0))) sendFx(&s, nullptr, nullptr);
    m_shots.push_back(std::move(s));
}

int HordeModule::mark(Mark m) {
    m.id = m_nextMark++;
    m.time = 0.0f;
    if (netHost()) sendFx(nullptr, &m, nullptr);
    m_markList.push_back(m);
    return m.id;
}

void HordeModule::unmark(int id) {
    m_markList.erase(std::remove_if(m_markList.begin(), m_markList.end(), [id](const Mark& m) { return m.id == id; }), m_markList.end());
}

void HordeModule::blast(const Blast& b) {
    if (netHost()) sendFx(nullptr, nullptr, &b);
    m_blasts.push_back(b);
}

// Something bursts: fire, rock or just force, on the other team round it.
void HordeModule::burst(const Blast& b) {
    if (m_fx) {
        const bool fire = b.element == "fire";
        for (int i = 0; i < 18; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / 18.0f;
            const glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
            m_fx->smoke(b.center + out * b.radius * 0.4f + glm::vec3(0.0f, 0.3f, 0.0f), out * b.radius * 0.6f + glm::vec3(0.0f, 1.2f, 0.0f),
                        fire ? glm::vec3(0.25f, 0.2f, 0.18f) : glm::vec3(0.45f, 0.4f, 0.33f), 0.45f * std::sqrt(b.radius), 1.8f, 0.5f);
        }
        m_fx->sparks(b.center + glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(0, 1, 0), fire ? 60 : 24, fire ? 7.0f : 5.0f);
    }
    if (b.hit.damage <= 0.0f) return;
    if (b.team == 1) {
        for (auto& h : m_heroes) {
            const glm::vec3 p = heroFeet(*h);
            if (glm::length(glm::vec2(p.x - b.center.x, p.z - b.center.z)) <= b.radius + 0.3f && std::abs(p.y - b.center.y) < 2.5f)
                hurtHero(*h, b.hit, b.center, p + glm::vec3(0.0f, 1.0f, 0.0f));
        }
    } else {
        for (auto& f : m_foes)
            if (!f->dead && glm::length(glm::vec2(f->position.x - b.center.x, f->position.z - b.center.z)) <= b.radius + 0.3f)
                hurtFoe(*f, b.hit, b.center, f->position + glm::vec3(0.0f, 1.0f, 0.0f), -1);
    }
}

void HordeModule::hurtHero(Hero& h, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point) {
    if (!h.id) return;
    if (h.remote) {
        // Their screen decides (a block, a parry, a roll).
        sendHurt(h, a, from, point);
        return;
    }
    kke::Combatant& c = m_combat.get(h.id);
    // Rolling, on the ground or just up: it passes over, as the melee rules
    // have it (a hit on the ground would knock them down again, for ever).
    if (!c.alive() || h.upGrace > 0.0f || c.state() == kke::Combatant::State::Dodging || c.state() == kke::Combatant::State::Knockdown) return;
    kke::Combatant attacker(0, 1, kke::CombatStats::grunt());
    attacker.place(from, point - from);
    onHit(c.receive(attacker, a, point));
}

void HordeModule::hurtFoe(Foe& f, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point, int byHero) {
    if (f.dead || !f.id) return;
    kke::Combatant& c = m_combat.get(f.id);
    const kke::CombatantId by = byHero >= 0 && byHero < static_cast<int>(m_heroes.size()) ? m_heroes[static_cast<size_t>(byHero)]->id : 0;
    kke::Combatant attacker(by, 0, kke::CombatStats::fighter());
    attacker.place(from, point - from);
    onHit(c.receive(attacker, a, point));
}

// Every hit, from a swing (CombatWorld::step), a shot or a burst.
void HordeModule::onHit(const kke::HitEvent& e) {
    using O = kke::HitOutcome;
    const glm::vec3 awayDir = glm::length(glm::vec2(e.push.x, e.push.z)) > 1e-4f ? glm::normalize(glm::vec3(e.push.x, 0.0f, e.push.z)) : glm::vec3(0.0f);
    if (Hero* h = heroByCombatant(e.target)) {
        if (h->remote) {
            // The host's copy of an online player took a goblin's swing:
            // pass it on, their screen decides.
            if (const kke::Combatant* a = m_combat.find(e.attacker))
                if (a->team() == 1) sendHurt(*h, a->currentAttack(), a->feet(), e.point);
            m_combat.get(h->id).reset(); // the copy never goes down here
            return;
        }
        if (e.outcome == O::Parried) {
            if (m_fx) m_fx->sparks(e.point, awayDir, 24, 5.0f);
            return;
        }
        if (e.outcome == O::Blocked || e.outcome == O::GuardBroke) {
            if (m_fx) m_fx->sparks(e.point, -awayDir, 10, 3.0f);
            h->push += e.push * 0.5f;
            return;
        }
        h->push += glm::vec3(e.push.x, 0.0f, e.push.z);
        h->look.flinch = 1.0f;
        h->look.flinchDir = awayDir;
        h->look.hurtFlash = 1.0f;
        h->drawing = false;
        h->heavyHeld = -1.0f;
        h->charged = false;
        if (e.outcome == O::Killed) kke::log::get(name())->info("{} is down (wave {})", h->name, m_wave);
        return;
    }
    Foe* f = foeByCombatant(e.target);
    if (!f) return;
    Hero* by = heroByCombatant(e.attacker);
    const int byIndex = [&] {
        for (size_t i = 0; i < m_heroes.size(); ++i)
            if (m_heroes[i].get() == by) return static_cast<int>(i);
        return -1;
    }();
    if (e.outcome == O::Parried || e.outcome == O::Blocked) {
        if (m_fx) m_fx->sparks(e.point, -awayDir, 8, 3.0f);
        return;
    }
    const float bossK = f->type->boss ? 0.15f : 1.0f;
    f->push += glm::vec3(e.push.x, 0.0f, e.push.z) * bossK;
    f->look.flinch = f->type->boss ? 0.4f : 1.0f;
    f->look.flinchDir = awayDir;
    f->look.hurtFlash = 1.0f;
    if (by) ++m_heroHits;
    if (m_fx) m_fx->sparks(e.point, awayDir, 6, 2.5f);
    if (netClient()) {
        // The host's horde decides; tell it what this screen's player did.
        if (by && !by->remote) {
            kke::AttackDesc a;
            a.name = e.attack;
            a.damage = e.damage;
            a.knockback = glm::length(glm::vec2(e.push.x, e.push.z));
            a.poiseDamage = e.damage;
            sendHeroHit(f->netId, a, by->position, e.point, byIndex);
        }
        m_combat.get(f->id).reset();
        return;
    }
    // Stone skin: the stone takes half.
    if (f->stoneskin > 0.0f && e.outcome != O::Killed) m_combat.get(f->id).heal(e.damage * 0.5f);
    if (e.outcome == O::Killed) {
        if (by) ++by->kills;
        killFoe(*f, glm::vec3(e.push.x, std::max(1.0f, e.push.y), e.push.z) * (f->type->boss ? 0.3f : 1.0f));
    }
}

void HordeModule::updateShots(float dt) {
    kke::RigidWorld& world = m_rigid->world();
    for (Shot& s : m_shots) {
        s.life -= dt;
        if (s.stuck) {
            s.stuckTime += dt;
            continue;
        }
        // Homing (a zap): turn toward the target, a little each frame.
        if (s.homing > 0.0f && s.target >= 0 && s.target < static_cast<int>(m_heroes.size())) {
            const glm::vec3 to = heroFeet(*m_heroes[static_cast<size_t>(s.target)]) + glm::vec3(0.0f, 1.2f, 0.0f) - s.position;
            const float speed = glm::length(s.velocity);
            if (glm::length(to) > 0.5f && speed > 0.0f) {
                const glm::vec3 want = glm::normalize(to) * speed;
                s.velocity += (want - s.velocity) * std::min(1.0f, s.homing * dt);
                s.velocity = glm::normalize(s.velocity) * speed;
            }
        }
        s.velocity.y -= s.gravity * dt;
        const glm::vec3 a = s.position, b = s.position + s.velocity * dt;
        s.position = b;
        if (s.kind == "fireball" && m_fx) m_fx->smoke(a, glm::vec3(0.0f, 0.4f, 0.0f), { 0.3f, 0.25f, 0.2f }, 0.18f, 0.6f, 0.35f);
        if (!s.local) {
            if (s.position.y < groundAt(s.position) || s.life <= 0.0f) s.life = std::min(s.life, 0.0f);
            continue;
        }
        bool done = false;
        glm::vec3 at = b;
        // Who it hits on the way.
        if (s.team == 1) {
            for (auto& h : m_heroes) {
                if (!h->id || (!h->remote && !m_combat.get(h->id).alive())) continue;
                const glm::vec3 feet = heroFeet(*h);
                const float t = segmentSphere(a, b, feet + glm::vec3(0.0f, 1.0f, 0.0f), 0.55f);
                if (t < 0.0f) continue;
                at = a + (b - a) * t;
                if (s.splash <= 0.0f) hurtHero(*h, s.hit, a, at);
                done = true;
                break;
            }
        } else {
            for (auto& fp : m_foes) {
                Foe& f = *fp;
                if (f.dead) continue;
                const float r = m_combat.get(f.id).stats().radius + 0.1f;
                const float hgt = m_combat.get(f.id).stats().height;
                // Head first (a headshot), then the body.
                const glm::vec3 head = f.look.model ? f.look.headPos + glm::vec3(0.0f, 0.06f * f.scale, 0.0f) : f.position + glm::vec3(0.0f, hgt * 0.92f, 0.0f);
                float t = segmentSphere(a, b, head, 0.17f * f.scale);
                bool headshot = t >= 0.0f;
                if (!headshot) {
                    const glm::vec3 d = b - a;
                    for (float k = 0.0f; k <= 1.0f && t < 0.0f; k += 0.25f)
                        if (kke::distanceToCapsule(a + d * k, f.position, r, hgt) <= 0.0f) t = k;
                }
                if (t < 0.0f) continue;
                at = a + (b - a) * t;
                kke::AttackDesc hit = s.hit;
                if (headshot) {
                    hit.damage *= s.headshot;
                    hit.poiseDamage *= s.headshot;
                    if (m_fx) m_fx->sparks(at, -glm::normalize(s.velocity), 16, 3.0f);
                }
                hurtFoe(f, hit, a - glm::normalize(s.velocity) * 2.0f, at, s.hero);
                done = true;
                break;
            }
        }
        // The ruins and the ground.
        if (!done) {
            const glm::vec3 d = b - a;
            const float len = glm::length(d);
            if (len > 1e-4f) {
                const auto ray = world.raycast(a, d / len, len);
                if (ray.hit) {
                    at = a + d / len * ray.distance;
                    done = true;
                }
            }
            if (!done && b.y < groundAt(b) - 0.02f) {
                at = glm::vec3(b.x, groundAt(b), b.z);
                done = true;
            }
        }
        if (!done) continue;
        if (s.markId) unmark(s.markId);
        s.position = at;
        if (s.splash > 0.0f) {
            Blast bl;
            bl.center = at;
            bl.radius = s.splash;
            bl.hit = s.hit;
            bl.team = s.team;
            bl.element = s.kind == "fireball" ? "fire" : "";
            burst(bl);
            s.life = 0.0f;
        } else if (isArrow(s.kind)) {
            s.stuck = true; // stays where it hit a while
            s.life = std::min(s.life, 3.0f);
        } else {
            if (m_fx) m_fx->sparks(at, glm::vec3(0, 1, 0), 8, 2.0f);
            s.life = 0.0f;
        }
    }
    // Gone: arrows back to the pool.
    for (size_t i = 0; i < m_shots.size();) {
        Shot& s = m_shots[i];
        if (s.life > 0.0f) {
            if (s.model && glm::length(s.velocity) > 1e-3f && !s.stuck) {
                const glm::vec3 z = glm::normalize(s.velocity);
                const glm::vec3 up = std::abs(z.y) > 0.98f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                const glm::vec3 x = glm::normalize(glm::cross(up, z)), y = glm::cross(z, x);
                // The arrow lies along its +z, the tip forward.
                m_models->setTransform(s.model, glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(s.position, 1)));
            }
            ++i;
            continue;
        }
        if (s.markId) unmark(s.markId);
        if (s.model) {
            m_models->setVisible(s.model, false);
            m_arrowPool.push_back(s.model);
        }
        m_shots.erase(m_shots.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void HordeModule::updateMarks(float dt) {
    for (Mark& m : m_markList) m.time += dt;
    m_markList.erase(std::remove_if(m_markList.begin(), m_markList.end(), [](const Mark& m) { return m.time > m.total + 0.15f; }), m_markList.end());
    for (Blast& b : m_blasts) b.delay -= dt;
    for (size_t i = 0; i < m_blasts.size();) {
        if (m_blasts[i].delay > 0.0f) {
            ++i;
            continue;
        }
        const Blast b = m_blasts[i];
        m_blasts.erase(m_blasts.begin() + static_cast<std::ptrdiff_t>(i));
        if (!netClient()) burst(b);
        else if (m_fx) m_fx->sparks(b.center, glm::vec3(0, 1, 0), 40, 6.0f);
    }
    buildMarkMesh();
}

// The warnings, drawn flat on the ground and glowing: an outline where it
// will land and a fill that grows to the outline as the moment comes (a
// player reads "when" from how full it is).
void HordeModule::buildMarkMesh() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& color, float glow) {
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (const glm::vec3& p : { a, b, c, d }) v.push_back({ p, color, glm::vec3(0, 1, 0), glm::vec2(glow, 0.0f) });
        idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    };
    for (const Mark& m : m_markList) {
        const float k = std::clamp(m.time / std::max(0.05f, m.total), 0.0f, 1.0f);
        const float pulse = m.super ? 0.5f + 0.5f * std::sin(m.time * 14.0f) : 0.0f;
        const glm::vec3 edge = m.color * (1.2f + pulse);
        const glm::vec3 fill = m.color * 0.55f;
        const float y = m.center.y + 0.04f;
        const float ring = m.super ? 0.22f : 0.12f;
        if (m.shape == Mark::Shape::Line) {
            const glm::vec3 f = m.dir, side(m.dir.z, 0.0f, -m.dir.x);
            const glm::vec3 o(m.center.x, y, m.center.z);
            const float w = m.radius;
            auto at = [&](float along, float across) { return o + f * along + side * across; };
            quad(at(0, -w), at(0, w), at(m.length * k, w), at(m.length * k, -w), fill, 0.8f);
            quad(at(0, -w), at(0, -w + ring), at(m.length, -w + ring), at(m.length, -w), edge, 2.0f);
            quad(at(0, w - ring), at(0, w), at(m.length, w), at(m.length, w - ring), edge, 2.0f);
            quad(at(m.length - ring, -w), at(m.length - ring, w), at(m.length, w), at(m.length, -w), edge, 2.0f);
            continue;
        }
        // Circle, or a cone (`radius` is its half angle in degrees, `length` its reach).
        const bool cone = m.shape == Mark::Shape::Cone;
        const float r = cone ? m.length : m.radius;
        const float half = cone ? glm::radians(m.radius) : kPi;
        const float mid = std::atan2(m.dir.x, m.dir.z);
        const int n = cone ? 14 : 40;
        const glm::vec3 o(m.center.x, y, m.center.z);
        auto at = [&](float ang, float rad) { return o + glm::vec3(std::sin(ang), 0.0f, std::cos(ang)) * rad; };
        for (int i = 0; i < n; ++i) {
            const float a0 = mid - half + 2.0f * half * static_cast<float>(i) / static_cast<float>(n);
            const float a1 = mid - half + 2.0f * half * static_cast<float>(i + 1) / static_cast<float>(n);
            quad(at(a0, 0.0f), at(a1, 0.0f), at(a1, r * k), at(a0, r * k), fill, 0.8f);
            quad(at(a0, r - ring), at(a1, r - ring), at(a1, r), at(a0, r), edge, 2.0f);
        }
        if (cone)
            for (float a : { mid - half, mid + half }) {
                const glm::vec3 side(std::cos(a), 0.0f, -std::sin(a));
                quad(o, o + side * ring, at(a, r) + side * ring, at(a, r), edge, 2.0f);
            }
    }
    m_markVerts = v.size();
    if (!v.empty()) m_marks->upload(v, idx);
}

} // namespace horde
