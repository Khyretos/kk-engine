// Guns, cannonballs, damage and the enemy captains (README.md "Battle").
//
// Broadsides the Black Flag way: hold aim and the guns on the side the
// camera looks at run out; the camera's pitch raises or lowers them and an
// arc of markers shows where the balls will fall (the same integration the
// balls use, so the arc is exact). Fire and the side rolls off gun by gun.
// Balls are plain ballistic shots (gravity, no drag): a hit dents the hull
// where it landed, throws splinters that float, can start a fire or snap a
// mast (which falls into the sea and floats), and lets water in; at no
// health the ship heels over and goes down.

#include "SeaDemoModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/NetModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke_sea {

namespace {

constexpr float kGravity = 9.81f;
constexpr float kBallLife = 9.0f;
constexpr float kShotGap = 0.09f;    // s between guns in a broadside
constexpr float kDamagePerKg = 9.0f; // a 5.4 kg ball takes ~49 health
constexpr size_t kMaxFires = 6;
constexpr float kAimRange = 320.0f;  // aim help and the enemies' guns reach this far

// Vertex normals again after a dent (area-weighted face normals).
void recomputeNormals(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& idx, std::vector<glm::vec3>& n) {
    std::vector<glm::vec3> sum(p.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        if (a >= p.size() || b >= p.size() || c >= p.size()) continue;
        const glm::vec3 fn = glm::cross(p[b] - p[a], p[c] - p[a]);
        sum[a] += fn;
        sum[b] += fn;
        sum[c] += fn;
    }
    for (size_t i = 0; i < p.size() && i < n.size(); ++i) {
        const float len = glm::length(sum[i]);
        if (len > 1e-12f) n[i] = sum[i] / len;
    }
}

// Pushes the vertices within `radius` of `point` along `dir`, never more
// than `most` from where they were made (the Flying demo's dents).
bool pushIn(std::vector<glm::vec3>& verts, const std::vector<glm::vec3>& made, const glm::vec3& point, const glm::vec3& dir, float depth, float radius,
            float most) {
    bool moved = false;
    for (size_t i = 0; i < verts.size(); ++i) {
        const float d = glm::length(verts[i] - point);
        if (d >= radius) continue;
        const float f = (1.0f - d / radius) * (1.0f - d / radius);
        glm::vec3 next = verts[i] + dir * (depth * f);
        if (i < made.size()) {
            const glm::vec3 off = next - made[i];
            const float len = glm::length(off);
            if (len > most) next = made[i] + off * (most / len);
        }
        verts[i] = next;
        moved = true;
    }
    return moved;
}

// Segment p0-p1 against the box [-h, h] (slabs): the entry fraction.
bool segmentBox(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& h, float& tHit) {
    const glm::vec3 d = p1 - p0;
    float t0 = 0.0f, t1 = 1.0f;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-8f) {
            if (p0[a] < -h[a] || p0[a] > h[a]) return false;
            continue;
        }
        float ta = (-h[a] - p0[a]) / d[a], tb = (h[a] - p0[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

float wrapPi(float a) {
    while (a > glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

float headingOf(const glm::quat& q) {
    const glm::vec3 f = glm::mat3_cast(q) * glm::vec3(0, 0, 1);
    return std::atan2(f.x, f.z);
}

} // namespace

// ---- aiming

float SeaDemoModule::solveElevation(float range, float muzzle, float height) {
    // The low arc that lands `range` away, `height` below (+) or above (-).
    const float v2 = muzzle * muzzle;
    const float disc = v2 * v2 - kGravity * (kGravity * range * range - 2.0f * height * v2);
    if (disc < 0.0f || range < 1e-3f) return 45.0f; // out of reach: the farthest it goes
    return glm::degrees(std::atan((v2 - std::sqrt(disc)) / (kGravity * range)));
}

int SeaDemoModule::aimSide(const Captain& c, const Ship& s) const {
    const glm::vec3 port = glm::mat3_cast(s.drawRot) * glm::vec3(1, 0, 0);
    const glm::vec3 view = glm::normalize(c.camera.target - c.camera.position);
    return glm::dot(glm::vec3(view.x, 0.0f, view.z), port) >= 0.0f ? 0 : 1;
}

int SeaDemoModule::nearestEnemy(const Ship& s, float maxRange) const {
    const glm::vec3 at = m_bodies.bodies()[s.body].position;
    int best = -1;
    float bestD = maxRange;
    for (size_t i = 0; i < m_ships.size(); ++i) {
        const Ship& o = m_ships[i];
        if (!o.alive || o.sinking || &o == &s) continue;
        if (o.team == s.team && !(m_friendlyFire && s.team == kPlayers)) continue;
        const float d = glm::length(o.drawPos - at);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

float SeaDemoModule::aimElevation(const Captain& c) const {
    // The camera's pitch raises the guns: level-ish from behind, more as
    // the camera looks up toward the horizon.
    float elevation = std::clamp(3.0f + (c.rig.pitch + 14.0f) * 0.55f, -4.0f, 20.0f);
    if (!m_aimHelp || c.ship < 0) return elevation;
    // Aim help: a ship off the side being aimed gets the range solved
    // (and led), the camera nudging it from there.
    const Ship& s = m_ships[static_cast<size_t>(c.ship)];
    const glm::mat3 R = glm::mat3_cast(s.drawRot);
    const glm::vec3 out = R * glm::vec3(c.aimSideNow == 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
    float bestScore = 1e9f;
    float solved = elevation;
    for (const Ship& o : m_ships) {
        if (!o.alive || o.sinking || &o == &s || (o.team == s.team && !m_friendlyFire)) continue;
        glm::vec3 d = o.drawPos - s.drawPos;
        d.y = 0.0f;
        const float dist = glm::length(d);
        if (dist < 5.0f || dist > kAimRange) continue;
        const float cosA = glm::dot(d / dist, glm::normalize(glm::vec3(out.x, 0.0f, out.z)));
        if (cosA < 0.88f) continue; // within ~28 degrees of the beam
        const float score = dist * (2.0f - cosA);
        if (score < bestScore) {
            bestScore = score;
            const float t = dist / m_muzzleSpeed;
            const glm::vec3 lead = o.drawPos + m_bodies.bodies()[o.body].velocity * t - s.drawPos;
            solved = solveElevation(glm::length(glm::vec2(lead.x, lead.z)), m_muzzleSpeed, s.drawPos.y - o.drawPos.y + 1.0f);
        }
    }
    if (bestScore < 1e8f) return solved + (elevation - 3.0f) * 0.15f;
    return elevation;
}

glm::vec3 SeaDemoModule::gunMuzzle(const Ship& s, int gun, int side, glm::vec3* direction, float elevationDegrees) const {
    const ShipArt& art = m_library.art(s.cls);
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    glm::vec3 local = art.guns[static_cast<size_t>(gun) % art.guns.size()];
    if (side == 1) local.x = -local.x;
    const glm::mat3 R = glm::mat3_cast(b.orientation);
    const glm::vec3 world = b.position + R * (local - art.hullCenter);
    if (direction) {
        // Out of the side, raised; the ship's roll adds to it (a gunner
        // fires on the up-roll for range, like the real thing).
        const float e = glm::radians(elevationDegrees);
        const glm::vec3 outLocal(side == 0 ? std::cos(e) : -std::cos(e), std::sin(e), 0.0f);
        *direction = glm::normalize(R * outLocal);
    }
    return world;
}

void SeaDemoModule::predictArc(const Ship& s, int side, float elevationDegrees, std::vector<glm::vec3>& out) const {
    out.clear();
    const ShipArt& art = m_library.art(s.cls);
    if (art.guns.empty()) return;
    glm::vec3 dir;
    // The middle gun of the side stands for the broadside.
    const int guns = shipClasses()[static_cast<size_t>(s.cls)].gunsPerSide;
    glm::vec3 p = gunMuzzle(s, guns / 2, side, &dir, elevationDegrees);
    glm::vec3 v = dir * m_muzzleSpeed + m_bodies.bodies()[s.body].velocity;
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < 300; ++i) {
        v.y -= kGravity * dt;
        p += v * dt;
        if (i % 2 == 0) out.push_back(p);
        if (p.y < m_waves.height({ p.x, p.z }, m_time)) {
            out.push_back(glm::vec3(p.x, m_waves.height({ p.x, p.z }, m_time), p.z));
            break;
        }
    }
}

// ---- firing

void SeaDemoModule::fire(Ship& s, int side, float elevationDegrees) {
    if (!s.alive || s.sinking || s.reload[side] > 0.0f) return;
    const ShipClass& c = shipClasses()[static_cast<size_t>(s.cls)];
    const ShipArt& art = m_library.art(s.cls);
    if (art.guns.empty()) return;
    s.reload[side] = c.reload;
    const size_t index = static_cast<size_t>(&s - m_ships.data());
    // Fore to aft, gun after gun; a second deck answers right behind.
    const int n = static_cast<int>(art.guns.size());
    for (int g = 0; g < n; ++g) {
        PendingShot p;
        p.ship = index;
        p.gun = g;
        p.side = side;
        p.elevation = elevationDegrees;
        p.delay = static_cast<float>(g % c.gunsPerSide) * kShotGap + static_cast<float>(g / c.gunsPerSide) * 0.04f;
        m_pending.push_back(p);
    }
}

void SeaDemoModule::fireGun(size_t ship, int gun, int side, float elevationDegrees) {
    if (ship >= m_ships.size()) return;
    Ship& s = m_ships[ship];
    if (!s.alive || s.sinking) return;
    const ShipClass& c = shipClasses()[static_cast<size_t>(s.cls)];
    // A gunner's error: a little for players, by skill for the enemies.
    const float spread = s.local ? 0.5f : (2.6f - 0.6f * static_cast<float>(s.skill));
    glm::vec3 dir;
    const glm::vec3 at = gunMuzzle(s, gun, side, &dir, elevationDegrees + (random01() - 0.5f) * spread);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 across = glm::normalize(glm::cross(dir, up));
    dir = glm::normalize(dir + across * ((random01() - 0.5f) * spread * 0.012f));
    kke::FloatingBody& b = m_bodies.bodies()[s.body];
    Ball ball;
    ball.pos = at + dir * 0.6f;
    ball.vel = dir * m_muzzleSpeed + b.velocity;
    ball.team = s.team;
    ball.owner = static_cast<int>(ship);
    ball.mass = c.ballMass;
    ball.life = kBallLife;
    m_balls.push_back(ball);
    muzzle(at, dir, b.velocity);
    // Recoil: the ship heels away from its own broadside.
    m_bodies.applyForce(s.body, -dir * (c.ballMass * m_muzzleSpeed * 60.0f * 6.0f), at);
    sendShot(ball.pos, ball.vel, ball.mass);
}

void SeaDemoModule::stepBalls(float dt) {
    for (Ship& s : m_ships)
        for (float& r : s.reload) r = std::max(0.0f, r - dt);
    for (size_t i = 0; i < m_pending.size();) {
        PendingShot& p = m_pending[i];
        p.delay -= dt;
        if (p.delay <= 0.0f) {
            const PendingShot shot = p;
            m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
            fireGun(shot.ship, shot.gun, shot.side, shot.elevation);
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i < m_balls.size();) {
        Ball& b = m_balls[i];
        const glm::vec3 from = b.pos;
        b.vel.y -= kGravity * dt;
        b.pos += b.vel * dt;
        b.life -= dt;
        bool gone = b.life <= 0.0f;
        if (!gone && hitShip(b, from, b.pos)) gone = true;
        if (!gone && b.live && fortHit(b, from, b.pos)) gone = true;
        if (!gone) {
            // An island: dust and stones.
            for (const Island& isl : m_islands) {
                const glm::vec2 d(b.pos.x - isl.center.x, b.pos.z - isl.center.z);
                if (glm::length(d) < isl.radius && b.pos.y < isl.center.y + isl.radius * 0.25f) {
                    if (m_fx) {
                        m_fx->sparks(b.pos, glm::vec3(0, 1, 0), 10, 8.0f);
                        for (int k = 0; k < 6; ++k)
                            m_fx->smoke(b.pos, glm::vec3(random01() - 0.5f, random01(), random01() - 0.5f) * 3.0f, { 0.72f, 0.64f, 0.5f }, 0.8f, 3.0f, 0.6f);
                    }
                    gone = true;
                    break;
                }
            }
        }
        if (!gone && b.pos.y < m_waves.height({ b.pos.x, b.pos.z }, m_time)) {
            bigSplash(glm::vec3(b.pos.x, m_waves.height({ b.pos.x, b.pos.z }, m_time), b.pos.z));
            gone = true;
        }
        if (gone) {
            m_balls[i] = m_balls.back();
            m_balls.pop_back();
            continue;
        }
        if (m_fx && random01() < 0.35f) m_fx->smoke(b.pos, glm::vec3(0.0f), { 0.82f, 0.82f, 0.8f }, 0.18f, 0.8f, 0.25f);
        ++i;
    }
}

bool SeaDemoModule::hitShip(Ball& ball, const glm::vec3& from, const glm::vec3& to) {
    if (!ball.live) {
        // Another screen's shot: it decides its hits; here it only stops at a hull.
        for (const Ship& s : m_ships) {
            if (!s.alive) continue;
            const ShipArt& art = m_library.art(s.cls);
            const kke::FloatingBody& b = m_bodies.bodies()[s.body];
            const glm::mat3 Rt = glm::transpose(glm::mat3_cast(s.remote ? s.drawRot : b.orientation));
            const glm::vec3 c = s.remote ? s.drawPos : b.position;
            float t;
            if (segmentBox(Rt * (from - c), Rt * (to - c), art.hullHalf * 1.05f, t)) return true;
        }
        return false;
    }
    for (size_t i = 0; i < m_ships.size(); ++i) {
        Ship& s = m_ships[i];
        if (!s.alive || static_cast<int>(i) == ball.owner) continue;
        if (s.team == ball.team && !(m_friendlyFire && s.team == kPlayers)) continue;
        const ShipArt& art = m_library.art(s.cls);
        const kke::FloatingBody& b = m_bodies.bodies()[s.body];
        const glm::quat rot = s.remote ? s.drawRot : b.orientation;
        const glm::vec3 centre = s.remote ? s.drawPos : b.position;
        const glm::mat3 Rt = glm::transpose(glm::mat3_cast(rot));
        const glm::vec3 p0 = Rt * (from - centre), p1 = Rt * (to - centre);
        // Masts first (they stand above the hull box).
        for (size_t m = 0; m < art.masts.size(); ++m) {
            if (m < s.mastUp.size() && !s.mastUp[m]) continue;
            const glm::vec3 base = art.masts[m].base - art.hullCenter;
            const glm::vec3 top = base + glm::vec3(0.0f, art.masts[m].height, 0.0f);
            glm::vec3 a, c;
            kke::FloatingBodies::closestPoints(p0, p1, base, top, a, c);
            if (glm::length(a - c) < 0.9f) {
                const glm::vec3 world = centre + glm::mat3_cast(rot) * a;
                if (m_fx) m_fx->sparks(world, -glm::normalize(ball.vel), 14, 6.0f);
                splinters(world, glm::normalize(ball.vel), 4);
                if (!s.remote && m < s.mastHealth.size()) {
                    s.mastHealth[m] -= ball.mass * kDamagePerKg * 1.4f * m_damageScale;
                    if (s.mastHealth[m] <= 0.0f) snapMast(i, static_cast<int>(m));
                }
                return true;
            }
        }
        float t;
        if (!segmentBox(p0, p1, art.hullHalf * 1.03f, t)) continue;
        const glm::vec3 world = from + (to - from) * t;
        s.lastHitBy = ball.owner;
        damageShip(i, world, glm::normalize(ball.vel), ball.mass * kDamagePerKg, false);
        return true;
    }
    return false;
}

// ---- damage

void SeaDemoModule::damageShip(size_t index, const glm::vec3& worldPoint, const glm::vec3& worldDirection, float amount, bool fromNet) {
    Ship& s = m_ships[index];
    if (!s.alive) return;
    const ShipArt& art = m_library.art(s.cls);
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    const glm::quat rot = s.remote ? s.drawRot : b.orientation;
    const glm::vec3 centre = s.remote ? s.drawPos : b.position;
    const glm::mat3 Rt = glm::transpose(glm::mat3_cast(rot));
    const glm::vec3 shipPoint = Rt * (worldPoint - centre) + art.hullCenter;
    const glm::vec3 shipDir = Rt * worldDirection;
    dent(s, shipPoint, shipDir, std::clamp(amount / 90.0f, 0.15f, 0.6f));
    splinters(worldPoint, worldDirection, 5 + static_cast<int>(amount / 12.0f));
    if (m_fx) {
        m_fx->sparks(worldPoint, -worldDirection, 10, 5.0f);
        for (int k = 0; k < 3; ++k)
            m_fx->smoke(worldPoint, -worldDirection * 2.0f + glm::vec3(0, 1, 0), { 0.45f, 0.42f, 0.38f }, 0.7f, 2.5f, 0.55f);
    }
    if (s.remote) {
        // Its owner decides what the hit does (and sends everyone the dent).
        if (!fromNet) sendHit(s.netId, shipPoint, shipDir, amount);
        return;
    }
    if (s.sinking) return;
    s.health -= amount * m_damageScale;
    m_bodies.applyForce(s.body, worldDirection * (amount * 4000.0f), worldPoint);
    if (s.fires.size() < kMaxFires && s.health < s.maxHealth * 0.7f && random01() < 0.3f) s.fires.push_back(shipPoint + glm::vec3(0.0f, 0.4f, 0.0f));
    if (s.health <= 0.0f) {
        s.health = 0.0f;
        if (s.lastHitBy >= 0)
            if (Captain* c = captainOf(static_cast<size_t>(s.lastHitBy))) ++c->sunk;
        startSinking(index);
    }
}

void SeaDemoModule::dent(Ship& s, const glm::vec3& shipPoint, const glm::vec3& shipDir, float depth) {
    const ShipArt& art = m_library.art(s.cls);
    if (!art.loaded || art.hullPositions.empty()) return;
    // In the model's own space (its units, before the scale to metres).
    const glm::mat4 toModel = glm::inverse(art.modelToShip);
    const glm::vec3 at = glm::vec3(toModel * glm::vec4(shipPoint, 1.0f));
    const glm::vec3 dir = glm::normalize(glm::vec3(toModel * glm::vec4(shipDir, 0.0f)));
    const float k = 1.0f / std::max(art.modelScale, 1e-4f);
    if (s.dented.size() != art.hullPositions.size()) {
        s.dented = art.hullPositions;
        s.dentedNormals = art.hullNormals;
    }
    for (size_t mesh = 0; mesh < s.dented.size(); ++mesh)
        if (pushIn(s.dented[mesh], art.hullPositions[mesh], at, dir, depth * k, (0.9f + depth * 2.0f) * k, 0.55f * k))
            recomputeNormals(s.dented[mesh], art.hullIndices[mesh], s.dentedNormals[mesh]);
    s.dentsChanged = true;
}

void SeaDemoModule::snapMast(size_t ship, int mast) {
    Ship& s = m_ships[ship];
    if (mast < 0 || mast >= static_cast<int>(s.mastUp.size()) || !s.mastUp[static_cast<size_t>(mast)]) return;
    s.mastUp[static_cast<size_t>(mast)] = false;
    const ShipArt& art = m_library.art(s.cls);
    const ShipArt::Mast& m = art.masts[static_cast<size_t>(mast)];
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    const glm::mat3 R = glm::mat3_cast(b.orientation);
    const glm::vec3 centreShip = m.base + glm::vec3(0.0f, m.height * 0.5f, 0.0f);
    const glm::vec3 centre = b.position + R * (centreShip - art.hullCenter);
    Floater f;
    f.bit = Bit::Mast;
    f.kind = s.cls;
    f.mast = mast;
    f.life = 45.0f;
    f.body = m_bodies.add(glm::vec3(0.45f, m.height * 0.5f, 0.45f), 520.0f, centre, b.orientation);
    kke::FloatingBody& mb = m_bodies.bodies()[f.body];
    mb.pushesSmallBits = false;
    // It topples over the side: a push at the top, turning about the keel line.
    const float way = random01() < 0.5f ? -1.0f : 1.0f;
    mb.velocity = b.velocity + R * glm::vec3(way * 2.0f, 0.5f, 0.0f);
    mb.angularVelocity = R * glm::vec3(0.0f, 0.0f, -way * 0.9f);
    if (art.loaded && m_models) {
        f.artOffset = glm::translate(glm::mat4(1.0f), -centreShip);
        for (const ShipArt::Part& p : art.parts)
            if ((p.role == ShipArt::Role::Mast || p.role == ShipArt::Role::Sail) && p.mast == mast)
                f.instances.push_back(m_models->spawn(p.model, mb.transform() * f.artOffset * art.modelToShip));
    }
    m_floaters.push_back(std::move(f));
    if (m_fx) {
        m_fx->sparks(centre, glm::vec3(0, 1, 0), 30, 7.0f);
        for (int k = 0; k < 8; ++k)
            m_fx->smoke(centre + glm::vec3(0.0f, (random01() - 0.5f) * m.height, 0.0f), glm::vec3(random01() - 0.5f, 0.6f, random01() - 0.5f) * 2.0f,
                        { 0.55f, 0.5f, 0.44f }, 1.0f, 3.0f, 0.5f);
    }
    // The ship takes some of it too.
    s.health -= s.maxHealth * 0.06f;
    if (s.health <= 0.0f && !s.sinking) startSinking(ship);
}

void SeaDemoModule::startSinking(size_t index) {
    Ship& s = m_ships[index];
    if (!s.alive || s.sinking) return;
    s.sinking = true;
    s.sinkTime = 0.0f;
    s.sail = 0;
    const ShipArt& art = m_library.art(s.cls);
    kke::FloatingBody& b = m_bodies.bodies()[s.body];
    // Down by the bow or the stern, heeling over to one side.
    b.centerOfMassOffset += glm::vec3((random01() - 0.5f) * art.hullHalf.x * 1.2f, 0.0f, (random01() < 0.5f ? -1.0f : 1.0f) * art.hullHalf.z * 0.35f);
    for (int k = 0; k < 2 && s.fires.size() < kMaxFires; ++k)
        s.fires.push_back(art.hullCenter + glm::vec3((random01() - 0.5f) * art.hullHalf.x, art.hullHalf.y, (random01() - 0.5f) * art.hullHalf.z * 1.6f));
    if (Captain* c = captainOf(index)) ++c->lost;
    kke::log::get("SeaDemo")->info("{} is sinking", s.name.empty() ? shipClasses()[static_cast<size_t>(s.cls)].name : s.name.c_str());
}

// ---- enemies

void SeaDemoModule::spawnEnemies() {
    // Where the players are: enemies come in from the horizon around them.
    glm::vec3 centre(0.0f);
    int n = 0;
    for (const Captain& c : m_captains)
        if (c.ship >= 0 && m_ships[static_cast<size_t>(c.ship)].alive) {
            centre += m_bodies.bodies()[m_ships[static_cast<size_t>(c.ship)].body].position;
            ++n;
        }
    if (n > 0) centre /= static_cast<float>(n);
    for (size_t e = 0; e < m_enemyShips.size(); ++e) {
        if (m_enemyShips[e] >= 0 && m_ships[static_cast<size_t>(m_enemyShips[e])].alive) continue;
        const int cls = m_enemyClass > 0 ? m_enemyClass - 1 : 1 + static_cast<int>(random01() * 3.99f);
        glm::vec3 at(0.0f);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const float a = random01() * 6.2831853f;
            const float r = m_mode == kModeTargets ? 90.0f + random01() * 60.0f : 170.0f + random01() * 60.0f;
            at = centre + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            bool clear = true;
            for (const Island& isl : m_islands)
                if (glm::length(glm::vec2(at.x - isl.center.x, at.z - isl.center.z)) < isl.radius + 40.0f) clear = false;
            if (clear) break;
        }
        at.y = 0.5f;
        const glm::vec3 toward = centre - at;
        const float heading = std::atan2(toward.x, toward.z) + (random01() - 0.5f) * 1.2f;
        const int si = spawnShip(cls, kEnemies, at, heading, false);
        Ship& s = m_ships[static_cast<size_t>(si)];
        s.skill = e < m_enemySkill.size() ? m_enemySkill[e] : 1;
        s.name = "Enemy " + std::to_string(e + 1);
        if (m_mode == kModeTargets) s.sail = 0;
        m_enemyShips[e] = si;
    }
}

void SeaDemoModule::thinkEnemy(size_t index, float dt) {
    Ship& s = m_ships[index];
    if (s.sinking) {
        s.rudder = 0.0f;
        return;
    }
    const ShipClass& c = shipClasses()[static_cast<size_t>(s.cls)];
    const kke::FloatingBody& b = m_bodies.bodies()[s.body];
    if (m_mode == kModeTargets) {
        // Target practice: at anchor, sails furled.
        s.sail = 0;
        s.throttle = 0.0f;
        s.rudder = 0.0f;
        return;
    }
    s.aiThink -= dt;
    const glm::mat3 R = glm::mat3_cast(b.orientation);
    const glm::vec3 port = R * glm::vec3(1, 0, 0), fwd = R * glm::vec3(0, 0, 1);
    const float heading = headingOf(b.orientation);
    const int t = nearestEnemy(s, 600.0f);
    float want = heading;
    float drive = 0.5f;
    if (t < 0) {
        // Nobody about: back toward where it came from.
        const glm::vec3 d = s.home - b.position;
        if (glm::length(glm::vec2(d.x, d.z)) > 30.0f) want = std::atan2(d.x, d.z);
    } else {
        const Ship& target = m_ships[static_cast<size_t>(t)];
        glm::vec3 d = target.drawPos - b.position;
        d.y = 0.0f;
        const float dist = glm::length(d);
        const float toward = std::atan2(d.x, d.z);
        const float range = 45.0f + 10.0f * static_cast<float>(s.cls);
        if (dist > range * 1.9f) {
            want = toward;
            drive = 1.0f;
        } else if (dist < range * 0.55f) {
            want = toward + glm::pi<float>();
            drive = 1.0f;
        } else {
            // Beam on: whichever side needs the smaller turn.
            const float a = wrapPi(toward + glm::half_pi<float>() - heading), bb = wrapPi(toward - glm::half_pi<float>() - heading);
            want = heading + (std::abs(a) < std::abs(bb) ? a : bb);
            drive = 0.5f;
        }
        // Fire when the target is on the beam and loaded.
        const int side = glm::dot(d, port) >= 0.0f ? 0 : 1;
        const float off = dist > 1e-3f ? std::abs(glm::dot(d / dist, fwd)) : 1.0f;
        if (off < 0.34f && dist < kAimRange && s.reload[side] <= 0.0f && s.aiThink <= 0.0f) {
            const float flight = dist / m_muzzleSpeed;
            const glm::vec3 lead = target.drawPos + m_bodies.bodies()[target.body].velocity * flight - b.position;
            const float e = solveElevation(glm::length(glm::vec2(lead.x, lead.z)), m_muzzleSpeed, b.position.y - target.drawPos.y + 1.0f);
            const float error = (3.0f - static_cast<float>(s.skill)) * 0.6f * (random01() - 0.4f);
            fire(s, side, e + error);
            s.aiThink = 1.0f + random01() * (3.0f - static_cast<float>(s.skill)) * 0.8f;
        }
    }
    // Keep off the islands: turn away from one ahead.
    for (const Island& isl : m_islands) {
        const glm::vec3 d = isl.center - b.position;
        const float dist = glm::length(glm::vec2(d.x, d.z));
        if (dist > isl.radius + 70.0f) continue;
        const float bearing = wrapPi(std::atan2(d.x, d.z) - heading);
        if (std::abs(bearing) < 1.1f) want = heading - (bearing >= 0.0f ? 1.0f : -1.0f) * 1.2f;
    }
    const float err = wrapPi(want - heading);
    s.rudder += (std::clamp(-err * 2.0f, -1.0f, 1.0f) - s.rudder) * std::min(1.0f, dt * 2.0f);
    if (c.oars) s.throttle = drive;
    else s.sail = drive > 0.75f ? 2 : 1;
}

} // namespace kke_sea
