// Collisions, damage and the Dogfight's guns (README.md "Collisions and
// damage", "Dogfight"): planes bump into each other and into buildings;
// a knock dents the plane where it landed and takes some of its health,
// a hard one (planes meeting at more than kExplodeSpeed, a wall at more
// than kWallSpeed, the ground) and it explodes: a fireball, bits of the
// plane flying, smoke. A hurt plane smokes. Bullets are simulated here
// (a tracer drawn along each), hit the planes' spheres (Combat.h), the
// buildings and the ground.
//
// Online, each plane's own screen decides what happens to it (FlyNet.h):
// a hit on someone else's plane goes to its screen as a Damage event, and
// the dents and who shot whom come back to everyone.

#include "FlyingModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/NetModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace flying {

namespace {

constexpr float kGravity = 9.81f;
constexpr float kExplodeSpeed = 18.0f; // m/s: two planes meeting faster than this both go up
constexpr float kWallSpeed = 10.0f;    // m/s into a wall: faster, and it's a crash
constexpr float kBumpCooldown = 0.35f; // s between bumps (both screens see one bump, online, once)
constexpr float kMuzzle = 480.0f;      // m/s out of the guns
constexpr float kFireRate = 11.0f;     // rounds a second
constexpr float kBulletLife = 1.6f;    // s: about 750 m
constexpr float kBulletDamage = 6.0f;  // health a hit: 17 hits bring a plane down
constexpr float kAimHelp = 3.5f;       // degrees: a plane this near the sight is led for you
constexpr float kFireballLife = 1.2f;
constexpr float kChunkLife = 10.0f;
constexpr size_t kMaxChunks = 200;
constexpr glm::vec3 kDebrisDark(0.16f, 0.15f, 0.14f);
constexpr float kOweEvery = 0.12f;     // s: damage to another screen's plane is sent this often
constexpr float kKillCredit = 10.0f;   // s: hurt that long ago still counts as the kill

// A box as 24 vertices (flat faces).
void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

// Vertex normals again after a dent: each triangle's normal added to its
// corners (area-weighted by the cross product's length).
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
        if (len > 1e-9f) n[i] = sum[i] / len;
    }
}

// Pushes the vertices within `radius` of `point` along `dir` (up to
// `depth`, less further out), never more than `most` from where they
// were made.
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

} // namespace

float FlyingModule::random01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xffffffu) / static_cast<float>(0x1000000);
}

glm::mat4 FlyingModule::planeToWorld(const Pilot& p) const {
    const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
    const glm::quat rot = p.remote ? p.drawnRotation : p.plane.rotation;
    return glm::translate(glm::mat4(1.0f), at) * glm::mat4_cast(rot);
}

// Another screen's plane is somewhere only once it has said where, this flight.
bool FlyingModule::present(const Pilot& p) const { return !p.remote || (p.netSeen && p.net.round == static_cast<uint8_t>(m_round & 0xFFu)); }

int FlyingModule::pilotOfNet(int netId) const {
    if (netId < 0 || netId == 255) return -1;
    for (size_t i = 0; i < m_pilots.size(); ++i)
        if (m_pilots[i].netId == netId) return static_cast<int>(i);
    return -1;
}

int FlyingModule::netOf(int pilot) const {
    if (pilot < 0 || pilot >= static_cast<int>(m_pilots.size())) return 255;
    const int id = m_pilots[static_cast<size_t>(pilot)].netId;
    return id < 0 ? 255 : id;
}

// ---- bumps

void FlyingModule::collide(float) {
    if (m_phase != Phase::Flying && m_phase != Phase::Results) return;
    for (size_t i = 0; i < m_pilots.size(); ++i) {
        Pilot& a = m_pilots[i];
        if (a.remote || down(a)) continue;
        // The buildings: out of the wall, and hurt (or a crash, flying into it).
        glm::vec3 normal(0.0f), point(0.0f);
        float deepest = 0.0f;
        for (const HitSphere& s : m_shape.spheres) {
            const glm::vec3 c = a.plane.position + a.plane.rotation * s.at;
            glm::vec3 n;
            float depth = 0.0f;
            if (m_town.touches(c, s.radius, n, depth) && depth > deepest) {
                deepest = depth;
                normal = n;
                point = c - n * s.radius;
            }
        }
        if (deepest > 0.0f) {
            const float into = -glm::dot(a.plane.velocity, normal);
            a.plane.position += normal * deepest;
            if (into > kWallSpeed) {
                if (m_fx) m_fx->sparks(point, normal, 40, 12.0f);
                hurt(a, 999.0f, -1, point, -normal, 0);
                continue;
            }
            if (into > 0.5f) a.plane.velocity += normal * (into * 1.3f);
            if (a.bumpCooldown <= 0.0f && into > 1.5f) {
                a.bumpCooldown = kBumpCooldown;
                if (m_fx) m_fx->sparks(point, normal, 12, 5.0f);
                if (m_audio) m_audio->playImpact(point, kke::AudioMaterialTable::Metal, std::min(1.0f, into / kWallSpeed), static_cast<uint32_t>(i), 0.9f);
                hurt(a, 4.0f + into * 3.0f, -1, point, -normal, 0);
            }
        }
        // The other planes.
        for (size_t j = 0; j < m_pilots.size(); ++j) {
            if (j == i) continue;
            Pilot& b = m_pilots[j];
            if (down(b) || !present(b) || (!b.remote && j < i)) continue; // two of ours: once
            const glm::vec3 b1 = b.remote ? b.net.position : b.plane.position;
            const PlaneContact c = planesTouch(m_shape, a.previous, a.plane.position, a.plane.rotation, b.previous, b1, b.remote ? b.drawnRotation : b.plane.rotation);
            if (!c.hit) continue;
            const glm::vec3 vb = b.remote ? b.net.velocity : b.plane.velocity;
            // How fast they came together along the line between them.
            const float speed = std::max(0.0f, glm::dot(vb - a.plane.velocity, c.normal));
            bump(a, &b, c.point, c.normal, speed, static_cast<int>(j));
        }
    }
}

// `normal` points from the other plane toward `a` (the way `a` is pushed).
void FlyingModule::bump(Pilot& a, Pilot* b, const glm::vec3& point, const glm::vec3& normal, float speed, int by) {
    const int ai = static_cast<int>(&a - m_pilots.data());
    const bool hard = speed > kExplodeSpeed;
    // Out of each other either way.
    a.plane.position += normal * 0.4f;
    if (b && !b->remote) b->plane.position -= normal * 0.4f;
    if (!hard && a.bumpCooldown > 0.0f) return;
    a.bumpCooldown = kBumpCooldown;
    if (m_fx) m_fx->sparks(point, normal, hard ? 50 : 16, hard ? 14.0f : 6.0f, a.plane.velocity * 0.5f);
    if (m_audio) m_audio->playImpact(point, kke::AudioMaterialTable::Metal, std::min(1.0f, 0.3f + speed / kExplodeSpeed), static_cast<uint32_t>(ai * 31 + by), 1.0f);
    const float amount = hard ? 999.0f : 3.0f + speed * 2.5f;
    if (!hard) a.plane.velocity += normal * (speed * 0.6f + 1.0f);
    if (b && !b->remote) {
        b->bumpCooldown = kBumpCooldown;
        if (!hard) b->plane.velocity -= normal * (speed * 0.6f + 1.0f);
    }
    if (b && b->remote && m_net && m_net->connected()) {
        // The other plane's screen does the same to it (it may have seen the
        // bump too; its cooldown makes it one).
        net::Damage d;
        d.target = static_cast<uint8_t>(netOf(by));
        d.from = static_cast<uint8_t>(netOf(ai));
        d.round = static_cast<uint8_t>(m_round & 0xFFu);
        d.bump = true;
        d.amount = std::min(amount, 255.0f);
        d.speed = std::min(speed, 255.0f);
        const glm::mat4 inv = glm::inverse(planeToWorld(*b));
        d.point = glm::clamp(glm::vec3(inv * glm::vec4(point, 1.0f)), glm::vec3(-8.0f), glm::vec3(8.0f));
        d.direction = glm::normalize(glm::vec3(inv * glm::vec4(-normal, 0.0f)));
        m_net->sendEvent(net::kEventDamage, net::encode(d));
    }
    hurt(a, amount, by, point, -normal, 2);
    if (b && !b->remote) hurt(*b, amount, ai, point, normal, 2);
}

// Health off a plane of ours (or, another screen's, owed to it), a dent
// where it was hit, and down at 0. `worldDirection`: into the plane.
void FlyingModule::hurt(Pilot& p, float amount, int by, const glm::vec3& worldPoint, const glm::vec3& worldDirection, int cause) {
    if (down(p) || amount <= 0.0f) return;
    const glm::mat4 inv = glm::inverse(planeToWorld(p));
    const glm::vec3 local = glm::vec3(inv * glm::vec4(worldPoint, 1.0f));
    const glm::vec3 len = glm::vec3(inv * glm::vec4(worldDirection, 0.0f));
    const glm::vec3 dir = glm::length(len) > 1e-6f ? glm::normalize(len) : glm::vec3(0.0f, -1.0f, 0.0f);
    if (p.remote) {
        // Bullets on another screen's plane: gathered, and sent a few times a second.
        p.owed += amount;
        p.owedBy = by;
        p.owedPoint = glm::clamp(local, glm::vec3(-8.0f), glm::vec3(8.0f));
        p.owedDirection = dir;
        return;
    }
    if (cause == 1 && p.shield > 0.0f) return;
    p.health -= amount;
    const int self = static_cast<int>(&p - m_pilots.data());
    if (by >= 0 && by != self) {
        p.lastBy = by;
        p.lastByAt = m_clock;
    }
    dent(p, local, dir, std::clamp(amount * 0.012f, 0.05f, 0.35f));
    if (p.health <= 0.0f) {
        p.health = 0.0f;
        p.downCause = cause;
        p.plane.crashed = true;
        crash(p);
    }
}

// ---- dents

void FlyingModule::dent(Pilot& p, const glm::vec3& planePoint, const glm::vec3& planeDirection, float depth, bool tell) {
    const float radius = 0.8f + depth * 3.0f;
    const float most = 0.45f;
    if (!p.parts.empty() && m_art.loaded) {
        // Synty: in the model's own space (its units, before the turn to plane space).
        const glm::mat4 toModel = glm::inverse(m_art.toPlane);
        const glm::vec3 at = glm::vec3(toModel * glm::vec4(planePoint, 1.0f));
        const glm::vec3 dir = glm::normalize(glm::vec3(toModel * glm::vec4(planeDirection, 0.0f)));
        const float k = 1.0f / std::max(m_art.scale, 1e-4f);
        if (p.dented.size() != m_art.positions.size()) {
            p.dented.assign(m_art.positions.size(), {});
            p.dentedNormals.assign(m_art.positions.size(), {});
        }
        for (size_t part = 0; part < m_art.positions.size(); ++part) {
            // The propeller spins and the surfaces swing: only the body and the wings dent.
            if (m_art.kinds[part] == Art::Kind::Prop || m_art.kinds[part] == Art::Kind::Stick) continue;
            if (p.dented[part].empty()) {
                p.dented[part] = m_art.positions[part];
                p.dentedNormals[part] = m_art.normals[part];
            }
            for (size_t mesh = 0; mesh < p.dented[part].size(); ++mesh)
                if (pushIn(p.dented[part][mesh], m_art.positions[part][mesh], at, dir, depth * k, radius * k, most * k))
                    recomputeNormals(p.dented[part][mesh], m_art.indices[part][mesh], p.dentedNormals[part][mesh]);
        }
        p.dentsChanged = true;
    } else if (p.blockPlane && !p.blockBase.empty()) {
        if (p.blockDented.empty()) p.blockDented = p.blockBase;
        std::vector<glm::vec3> verts, made;
        for (const kke::Vertex& v : p.blockDented) verts.push_back(v.position);
        for (const kke::Vertex& v : p.blockBase) made.push_back(v.position);
        pushIn(verts, made, planePoint, planeDirection, depth, radius, most);
        for (size_t i = 0; i < verts.size(); ++i) p.blockDented[i].position = verts[i];
        p.dentsChanged = true;
    }
    if (tell && !p.remote && p.netId >= 0 && m_net && m_net->connected()) {
        net::Dent d;
        d.plane = static_cast<uint8_t>(p.netId);
        d.round = static_cast<uint8_t>(m_round & 0xFFu);
        d.point = glm::clamp(planePoint, glm::vec3(-8.0f), glm::vec3(8.0f));
        d.direction = planeDirection;
        d.depth = std::clamp(depth, 0.0f, 1.0f);
        m_net->sendEvent(net::kEventDent, net::encode(d));
    }
}

void FlyingModule::clearDents(Pilot& p) {
    if (p.dented.empty() && p.blockDented.empty()) return;
    p.dented.clear();
    p.dentedNormals.clear();
    p.blockDented.clear();
    p.dentsChanged = true;
}

// ---- going down

int FlyingModule::chunkMesh(const glm::vec3& colour) {
    for (size_t i = 0; i < m_chunkColours.size(); ++i)
        if (glm::length(m_chunkColours[i] - colour) < 0.02f) return static_cast<int>(i);
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox(glm::vec3(0.0f), glm::vec3(0.5f, 0.2f, 0.7f), colour, v, idx);
    auto mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    mesh->upload(v, idx);
    m_chunkMeshes.push_back(std::move(mesh));
    m_chunkColours.push_back(colour);
    return static_cast<int>(m_chunkColours.size()) - 1;
}

// Made on the first explosion they would stall that frame (a mesh upload,
// a synthesised sound per variant), with every plane going up at once in
// the worst case: made at the start of the flight instead.
void FlyingModule::warmUpExplosions() {
    chunkMesh(kDebrisDark);
    for (const Pilot& p : m_pilots) chunkMesh(glm::clamp(p.tint, glm::vec3(0.0f), glm::vec3(1.0f)));
    if (!m_audio) return;
    for (uint32_t v = 0; v < 4; ++v) {
        for (float level : { 0.1f, 0.3f, 0.55f, 1.0f }) m_audio->impacts().get(kke::AudioMaterialTable::Metal, level, v);
        m_audio->impacts().get(kke::AudioMaterialTable::Stone, 1.0f, v);
    }
}

// A fireball, a burst of sparks, black smoke, and bits of the plane flying.
void FlyingModule::explode(const glm::vec3& at, const glm::vec3& velocity, const glm::vec3& tint) {
    for (int i = 0; i < 7; ++i) {
        Fireball f;
        const glm::vec3 r(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f);
        f.position = at + r * 3.0f;
        f.velocity = velocity * 0.25f + r * 8.0f + glm::vec3(0.0f, 2.0f, 0.0f);
        f.size = 2.5f + random01() * 3.0f;
        f.age = -0.08f * static_cast<float>(i); // one after another, a rolling boom
        m_fireballs.push_back(f);
    }
    if (m_fx) {
        m_fx->sparks(at, glm::vec3(0.0f, 1.0f, 0.0f), 70, 22.0f, velocity * 0.3f);
        for (int i = 0; i < 14; ++i) {
            kke::ParticleEffects::Particle s;
            s.position = at + glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) * 4.0f;
            s.velocity = velocity * 0.15f + glm::vec3(random01() - 0.5f, random01() * 0.6f, random01() - 0.5f) * 8.0f;
            s.color = glm::vec3(0.12f + random01() * 0.08f);
            s.radius = 1.6f + random01();
            s.growth = 1.4f;
            s.opacity = 0.75f;
            s.life = 4.0f + random01() * 2.0f;
            s.rise = 1.6f;
            s.drag = 0.9f;
            s.spin = random01() - 0.5f;
            s.seed = random01();
            m_fx->emit(s);
        }
    }
    const int dark = chunkMesh(kDebrisDark), coloured = chunkMesh(glm::clamp(tint, glm::vec3(0.0f), glm::vec3(1.0f)));
    for (int i = 0; i < 10; ++i) {
        if (m_chunks.size() >= kMaxChunks) m_chunks.erase(m_chunks.begin());
        Chunk c;
        const glm::vec3 r = glm::normalize(glm::vec3(random01() - 0.5f, random01() * 0.8f, random01() - 0.5f) + glm::vec3(0.0f, 0.2f, 0.0f));
        c.position = at + r;
        c.velocity = velocity * 0.45f + r * (8.0f + random01() * 16.0f);
        c.axis = glm::normalize(glm::vec3(random01() - 0.5f, random01() - 0.5f, random01() - 0.5f) + glm::vec3(0.01f));
        c.spin = 4.0f + random01() * 10.0f;
        c.size = 0.6f + random01() * 1.1f;
        c.mesh = i < 4 ? coloured : dark;
        m_chunks.push_back(c);
    }
    if (m_audio) {
        const uint32_t seed = m_rng;
        m_audio->playImpact(at, kke::AudioMaterialTable::Metal, 1.0f, seed, 1.5f);
        m_audio->playImpact(at, kke::AudioMaterialTable::Stone, 1.0f, seed + 1, 1.5f);
    }
}

// A plane of ours went down: a death, maybe someone's kill, and the feed.
void FlyingModule::wentDown(Pilot& p) {
    const int self = static_cast<int>(&p - m_pilots.data());
    // Shot down, or crashed with bullets in it: whoever fired them gets the
    // kill. A collision is nobody's kill (both planes usually go).
    const bool credit = p.downCause != 2 && p.lastBy >= 0 && p.lastBy != self && m_clock - p.lastByAt < kKillCredit;
    const int killer = credit ? p.lastBy : -1;
    if (m_mode == Mode::Dogfight && m_phase == Phase::Flying) {
        ++p.deaths;
        addKill(killer, self, p.downCause);
    }
    if (p.netId >= 0 && m_net && m_net->connected()) {
        net::Down d;
        d.plane = static_cast<uint8_t>(p.netId);
        d.by = static_cast<uint8_t>(netOf(killer));
        d.round = static_cast<uint8_t>(m_round & 0xFFu);
        d.cause = static_cast<uint8_t>(std::clamp(p.downCause, 0, 2));
        m_net->sendEvent(net::kEventDown, net::encode(d));
    }
}

// Who got whom: the killer's screen counts it (its kills go to everyone
// with its plane); everyone shows it.
void FlyingModule::addKill(int killer, int victim, int cause) {
    if (m_mode != Mode::Dogfight || victim < 0 || victim >= static_cast<int>(m_pilots.size())) return;
    const Pilot& v = m_pilots[static_cast<size_t>(victim)];
    if (killer < 0 || killer >= static_cast<int>(m_pilots.size()) || killer == victim) {
        m_flash = v.name + (cause == 2 ? " collided" : cause == 0 ? " crashed" : " went down");
        m_flashTime = 2.5f;
        return;
    }
    Pilot& k = m_pilots[static_cast<size_t>(killer)];
    if (!k.remote) ++k.kills;
    m_flash = k.name + (cause == 1 ? " shot down " : " forced down ") + v.name;
    m_flashTime = 3.0f;
    kke::log::get(name())->info("{} ({} kills)", m_flash, k.remote ? static_cast<int>(k.net.kills) + 1 : k.kills);
    if (k.seat >= 0 && !k.remote && m_audio) m_audio->playEarcon(kke::Earcon::ToggleOn, 0.8f);
}

// ---- guns

void FlyingModule::fireGuns(Pilot& p, float dt) {
    if (m_mode != Mode::Dogfight || m_phase != Phase::Flying || down(p) || (!p.remote && p.plane.onGround)) {
        p.firing = false;
        p.gunCooldown = 0.0f;
        return;
    }
    if (!p.firing) {
        p.gunCooldown = std::max(0.0f, p.gunCooldown - dt);
        return;
    }
    p.gunCooldown -= dt;
    const int self = static_cast<int>(&p - m_pilots.data());
    const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
    const glm::quat rot = p.remote ? p.drawnRotation : p.plane.rotation;
    const glm::vec3 vel = p.remote ? p.net.velocity : p.plane.velocity;
    const glm::vec3 nose = rot * glm::vec3(0.0f, 0.0f, -1.0f);
    while (p.gunCooldown <= 0.0f) {
        p.gunCooldown += 1.0f / kFireRate;
        // Two guns, one in each lower wing, firing in turn.
        const float side = (static_cast<int>(m_rng & 1u) == 0) ? -1.6f : 1.6f;
        random01();
        const glm::vec3 muzzle = at + rot * glm::vec3(side, 0.3f, -1.5f);
        // Aim help: a plane near the sight is led (where it will be when
        // the bullets get there), as long as it's within a few degrees.
        glm::vec3 dir = nose;
        float best = std::cos(glm::radians(kAimHelp));
        for (size_t j = 0; j < m_pilots.size(); ++j) {
            const Pilot& o = m_pilots[j];
            if (static_cast<int>(j) == self || down(o) || !present(o)) continue;
            const glm::vec3 oAt = o.remote ? o.net.position : o.plane.position;
            if (glm::length(oAt - at) > kMuzzle * kBulletLife) continue;
            const glm::vec3 lead = leadPoint(muzzle, oAt, (o.remote ? o.net.velocity : o.plane.velocity) - vel, kMuzzle);
            const glm::vec3 to = glm::normalize(lead - muzzle);
            const float c = glm::dot(to, nose);
            if (c > best) {
                best = c;
                dir = to;
            }
        }
        // A little spread (more for the easier CPU pilots).
        const float spread = glm::radians(0.3f + (p.cpu ? (3.0f - static_cast<float>(std::clamp(p.skill, 0, 3))) * 0.5f : 0.0f));
        const glm::vec3 side1 = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)) + glm::vec3(0.0f, 0.0f, 1e-4f));
        const glm::vec3 side2 = glm::cross(side1, dir);
        dir = glm::normalize(dir + side1 * ((random01() - 0.5f) * 2.0f * spread) + side2 * ((random01() - 0.5f) * 2.0f * spread));
        Bullet b;
        b.position = muzzle;
        b.velocity = vel + dir * kMuzzle;
        b.life = kBulletLife;
        b.owner = self;
        b.live = !p.remote;
        m_bullets.push_back(b);
        if (m_fx) m_fx->sparks(muzzle + nose * 0.5f, nose, 2, 6.0f, vel);
    }
    if (p.seat >= 0 && !p.remote) m_gunsHere = true;
}

void FlyingModule::updateBullets(float dt) {
    for (size_t i = 0; i < m_bullets.size();) {
        Bullet& b = m_bullets[i];
        const glm::vec3 from = b.position;
        b.velocity.y -= kGravity * dt;
        const glm::vec3 to = from + b.velocity * dt;
        b.life -= dt;
        float best = 2.0f;
        int hit = -1;
        glm::vec3 point(0.0f);
        if (b.live)
            for (size_t j = 0; j < m_pilots.size(); ++j) {
                const Pilot& o = m_pilots[j];
                if (static_cast<int>(j) == b.owner || down(o) || !present(o)) continue;
                float t = 0.0f;
                glm::vec3 at;
                if (bulletHits(m_shape, from, to, o.remote ? o.net.position : o.plane.position, o.remote ? o.drawnRotation : o.plane.rotation, t, at) && t < best) {
                    best = t;
                    hit = static_cast<int>(j);
                    point = at;
                }
            }
        float wall = 2.0f;
        const bool blocked = m_town.blocks(from, to, wall) && wall < best;
        const bool ground = to.y < m_island.surface(to.x, to.z);
        const glm::vec3 dir = glm::normalize(b.velocity);
        if (hit >= 0 && !blocked) {
            Pilot& o = m_pilots[static_cast<size_t>(hit)];
            if (m_fx) m_fx->sparks(point, -dir, 8, 7.0f, o.remote ? o.net.velocity : o.plane.velocity);
            if (b.owner >= 0 && b.owner < static_cast<int>(m_pilots.size())) m_pilots[static_cast<size_t>(b.owner)].hitMark = 0.25f;
            if (m_audio) m_audio->playImpact(point, kke::AudioMaterialTable::Metal, 0.35f, m_rng, 0.6f);
            hurt(o, kBulletDamage, b.owner, point, dir, 1);
        } else if (blocked || ground) {
            const glm::vec3 at = blocked ? from + (to - from) * wall : glm::vec3(to.x, m_island.surface(to.x, to.z), to.z);
            if (m_fx) {
                m_fx->sparks(at, -dir, 3, 4.0f);
                if (random01() < 0.3f) m_fx->smoke(at, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.62f, 0.58f, 0.5f), 0.4f, 1.2f, 0.4f);
            }
        }
        if (hit >= 0 || blocked || ground || b.life <= 0.0f) {
            m_bullets[i] = m_bullets.back();
            m_bullets.pop_back();
            continue;
        }
        b.position = to;
        // The tracer: a short-lived hot streak where the bullet is now.
        if (m_fx) {
            kke::ParticleEffects::Particle t;
            t.kind = kke::ParticleEffects::Kind::Spark;
            t.position = to;
            t.velocity = b.velocity;
            t.color = glm::vec3(7.0f, 4.6f, 1.6f);
            t.radius = 0.09f;
            t.life = dt * 1.5f;
            t.drag = 0.0f;
            t.stretch = 0.012f;
            m_fx->emit(t);
        }
        ++i;
    }
}

// Damage to other screens' planes, gathered: a few events a second each.
void FlyingModule::sendOwed(float dt) {
    for (size_t i = 0; i < m_pilots.size(); ++i) {
        Pilot& p = m_pilots[i];
        if (!p.remote || p.owed <= 0.0f) continue;
        p.owedAt += dt;
        if (p.owedAt < kOweEvery) continue;
        if (m_net && m_net->connected() && p.netId >= 0) {
            net::Damage d;
            d.target = static_cast<uint8_t>(p.netId);
            d.from = static_cast<uint8_t>(netOf(p.owedBy));
            d.round = static_cast<uint8_t>(m_round & 0xFFu);
            d.amount = std::min(p.owed, 255.0f);
            d.point = p.owedPoint;
            d.direction = p.owedDirection;
            m_net->sendEvent(net::kEventDamage, net::encode(d));
        }
        p.owed = 0.0f;
        p.owedAt = 0.0f;
    }
}

void FlyingModule::onDamage(const net::Damage& d) {
    if (d.round != static_cast<uint8_t>(m_round & 0xFFu) || m_phase == Phase::Lobby) return;
    const int t = pilotOfNet(d.target);
    if (t < 0) return;
    Pilot& p = m_pilots[static_cast<size_t>(t)];
    if (p.remote || down(p)) return; // not ours: its own screen deals with it
    const glm::mat4 world = planeToWorld(p);
    const glm::vec3 point = glm::vec3(world * glm::vec4(d.point, 1.0f));
    const glm::vec3 dir = glm::vec3(world * glm::vec4(d.direction, 0.0f));
    const int by = pilotOfNet(d.from);
    if (d.bump) {
        if (p.bumpCooldown > 0.0f && d.amount < 255.0f) return; // we felt this one already
        p.bumpCooldown = kBumpCooldown;
        if (d.amount < 255.0f) p.plane.velocity -= dir * (d.speed * 0.6f + 1.0f);
        if (m_fx) m_fx->sparks(point, -dir, 16, 6.0f, p.plane.velocity * 0.5f);
        hurt(p, d.amount >= 255.0f ? 999.0f : d.amount, by, point, dir, 2);
        return;
    }
    hurt(p, d.amount, by, point, dir, 1);
}

void FlyingModule::onDent(const net::Dent& d) {
    if (d.round != static_cast<uint8_t>(m_round & 0xFFu)) return;
    const int i = pilotOfNet(d.plane);
    if (i < 0 || !m_pilots[static_cast<size_t>(i)].remote) return;
    dent(m_pilots[static_cast<size_t>(i)], d.point, d.direction, d.depth, false);
}

void FlyingModule::onDown(const net::Down& d) {
    if (d.round != static_cast<uint8_t>(m_round & 0xFFu)) return;
    const int victim = pilotOfNet(d.plane);
    if (victim < 0 || !m_pilots[static_cast<size_t>(victim)].remote) return;
    addKill(pilotOfNet(d.by), victim, d.cause);
}

// ---- effects

void FlyingModule::updateEffects(float dt) {
    for (Pilot& p : m_pilots) {
        p.bumpCooldown = std::max(0.0f, p.bumpCooldown - dt);
        p.shield = std::max(0.0f, p.shield - dt);
        p.hitMark = std::max(0.0f, p.hitMark - dt);
        // A hurt plane smokes from its engine: grey, then black and burning.
        const float health = p.remote ? static_cast<float>(p.net.health) : p.health;
        if (down(p) || health >= 60.0f || !m_fx) continue;
        p.smokeTimer -= dt;
        if (p.smokeTimer > 0.0f) continue;
        p.smokeTimer = health < 30.0f ? 0.04f : 0.08f;
        const glm::mat4 world = planeToWorld(p);
        const glm::vec3 engine = glm::vec3(world * glm::vec4(0.0f, 0.5f, -2.2f, 1.0f));
        const glm::vec3 vel = p.remote ? p.net.velocity : p.plane.velocity;
        const float shade = health < 30.0f ? 0.1f : 0.45f;
        m_fx->smoke(engine, vel * 0.25f, glm::vec3(shade), 0.5f, health < 30.0f ? 2.6f : 1.8f, 0.6f);
        if (health < 30.0f && random01() < 0.5f) m_fx->sparks(engine, glm::vec3(0.0f, 1.0f, 0.0f), 2, 3.0f, vel * 0.8f);
    }
    for (size_t i = 0; i < m_fireballs.size();) {
        Fireball& f = m_fireballs[i];
        f.age += dt;
        f.position += f.velocity * dt;
        f.velocity *= std::exp(-2.0f * dt);
        f.velocity.y += 3.0f * dt; // hot air rises
        if (f.age > kFireballLife) {
            m_fireballs[i] = m_fireballs.back();
            m_fireballs.pop_back();
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i < m_chunks.size();) {
        Chunk& c = m_chunks[i];
        c.age += dt;
        if (c.age > kChunkLife) {
            m_chunks.erase(m_chunks.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (!c.resting) {
            c.velocity.y -= kGravity * dt;
            c.velocity *= std::exp(-0.25f * dt);
            c.position += c.velocity * dt;
            c.angle += c.spin * dt;
            const float floor = std::max(m_island.surface(c.position.x, c.position.z), m_town.roof(c.position.x, c.position.z)) + c.size * 0.2f;
            if (c.position.y < floor) {
                c.position.y = floor;
                if (c.velocity.y < -2.0f && floor > Island::kSea + 0.5f) {
                    c.velocity.y = -c.velocity.y * 0.3f;
                    c.velocity.x *= 0.5f;
                    c.velocity.z *= 0.5f;
                    c.spin *= 0.5f;
                } else {
                    c.resting = true; // settled (or sunk: the sea keeps it)
                }
            }
            // Smoking as it falls.
            if (m_fx && c.age < 1.8f && random01() < 0.4f) m_fx->smoke(c.position, glm::vec3(0.0f), glm::vec3(0.18f), 0.45f, 1.6f, 0.5f);
        }
        ++i;
    }
    if (m_fx) m_fx->update(dt, glm::vec3(1.5f, 0.0f, 0.6f));
}

} // namespace flying
