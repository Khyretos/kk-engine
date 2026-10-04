// Guns, grenades and explosions in kke_demo (Kees, 2026-10-03: "a gun to
// shoot things and see destruction", "guns, explosions"). Equip the rifle
// or the pistol in your right hand (the bag, or picking one up with the
// hand empty) and the trigger (RT, left click) fires it where the
// crosshair is; aim (LT, right click) brings it up to the eye and the
// camera in over the shoulder. Rounds come out of the bag as the gun
// reloads by itself. A grenade in the right hand is thrown instead.
//
// A bullet is a ray (Jolt and FEMFX: whichever is hit first). Jolt bodies
// get a push where it lands; a FEMFX object gets a small fast iron slug
// at that spot, so it breaks the way the yard's glass does when the iron
// ball hits it. Red barrels (Range.cpp) and grenades explode:
// kke::physicsBlast pushes everything in both physics engines, FEMFX
// walls crack, and nearby barrels go up after them.
//
// The sounds are made here (a crack, a body and a tail of noise for a
// shot, a long low roll for a blast) rather than loaded: no files needed.

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/PhysicsWorld.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/RigidBodyModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

constexpr float kAimRange = 400.0f;   // m the crosshair and bullets reach
constexpr float kGunUpAfterShot = 0.8f; // s the gun stays raised after firing from the hip
constexpr uint32_t kStone = 1, kWood = 2, kMetal = 3;

uint32_t nextRandom(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
float random01(uint32_t& s) { return static_cast<float>(nextRandom(s) >> 8) * (1.0f / 16777216.0f); }

std::shared_ptr<const kke::SoundBuffer> finish(kke::SoundBuffer& b) {
    float peak = 1e-6f;
    for (float s : b.samples) peak = std::max(peak, std::abs(s));
    for (float& s : b.samples) s *= 0.95f / peak;
    return std::make_shared<const kke::SoundBuffer>(std::move(b));
}

// A shot: a sharp crack, a body of filtered noise, a low thump and a
// tail (the range's echo). `body` and `thump` in seconds and Hz.
std::shared_ptr<const kke::SoundBuffer> synthShot(float body, float thump, float tail, uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    const int n = static_cast<int>(static_cast<float>(rate) * (0.15f + tail * 4.0f));
    b.samples.resize(static_cast<size_t>(n));
    float lp = 0.0f, lp2 = 0.0f;
    const float pi2 = 6.2831853f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float w = random01(seed) * 2.0f - 1.0f;
        lp += 0.18f * (w - lp);
        lp2 += 0.025f * (w - lp2);
        const float crack = w * std::exp(-t / 0.0035f);
        const float bodyPart = lp * 2.2f * std::exp(-t / body);
        const float boom = std::sin(pi2 * thump * t * (1.0f - t * 0.8f)) * std::exp(-t / 0.07f);
        const float echo = lp2 * 3.0f * std::exp(-t / tail) * std::min(1.0f, t / 0.03f);
        b.samples[static_cast<size_t>(i)] = crack * 0.9f + bodyPart + boom * 0.7f + echo * 0.35f;
    }
    return finish(b);
}

// An explosion: a crack, then a long rumble of low noise and a deep thump.
std::shared_ptr<const kke::SoundBuffer> synthBoom(uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    const int n = rate * 3;
    b.samples.resize(static_cast<size_t>(n));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float w = random01(seed) * 2.0f - 1.0f;
        lp += 0.08f * (w - lp);
        lp2 += 0.006f * (w - lp2);
        const float crack = w * std::exp(-t / 0.012f);
        const float roar = lp * 2.0f * std::exp(-t / 0.25f);
        const float rumble = lp2 * 14.0f * std::exp(-t / 0.9f) * std::min(1.0f, t / 0.02f);
        const float thump = std::sin(6.2831853f * 42.0f * t * (1.0f - t * 0.3f)) * std::exp(-t / 0.35f);
        b.samples[static_cast<size_t>(i)] = crack * 0.8f + roar + rumble + thump * 0.9f;
    }
    return finish(b);
}

// Metal clicks: `times` (s) each a short tick (an empty gun; a magazine going in).
std::shared_ptr<const kke::SoundBuffer> synthClicks(std::initializer_list<float> times, uint32_t seed) {
    kke::SoundBuffer b;
    const int rate = b.sampleRate;
    float last = 0.0f;
    for (float t : times) last = std::max(last, t);
    const int n = static_cast<int>(static_cast<float>(rate) * (last + 0.06f));
    b.samples.assign(static_cast<size_t>(n), 0.0f);
    for (float at : times) {
        const int start = static_cast<int>(at * static_cast<float>(rate));
        for (int i = 0; start + i < n && i < rate / 20; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(rate);
            const float w = random01(seed) * 2.0f - 1.0f;
            b.samples[static_cast<size_t>(start + i)] += w * std::exp(-t / 0.0015f) + std::sin(6.2831853f * 2600.0f * t) * 0.5f * std::exp(-t / 0.006f);
        }
    }
    return finish(b);
}

// Columns made unit length (bone matrices can carry the model's scale).
glm::mat3 unscaled(const glm::mat4& m) {
    return glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])));
}

} // namespace

void ShowcaseModule::buildGuns() {
    m_fx = std::make_unique<kke::ParticleEffects>(*m_app);
    m_fxLib = std::make_unique<kke::ParticleLibrary>(*m_fx);
    m_sndRifle = synthShot(0.05f, 70.0f, 0.35f, 0x1234567u);
    m_sndPistol = synthShot(0.03f, 110.0f, 0.22f, 0x7654321u);
    m_sndBoom = synthBoom(0x2468aceu);
    m_sndClick = synthClicks({ 0.0f }, 0x13579bdu);
    m_sndReload = synthClicks({ 0.0f, 0.32f, 0.5f }, 0x0badf00u);
}

const ShowcaseModule::GunDef* ShowcaseModule::gunInHand() const {
    // Muzzles where Items.cpp builds the barrels (item space, X along the barrel).
    static const GunDef kGuns[] = {
        { "rifle", "ammo_rifle", 30, 0.11f, true, 1.6f, 28.0f, { 0.68f, 0.085f, 0.0f } },
        { "pistol", "ammo_pistol", 12, 0.16f, false, 1.1f, 18.0f, { 0.145f, 0.062f, 0.0f } },
    };
    const kke::InventoryItem* it = m_inv.equipped(kke::EquipSlot::RightHand);
    if (!it || m_held.body != kke::RigidWorld::kNoBody) return nullptr;
    for (const GunDef& g : kGuns)
        if (it->id == g.id) return &g;
    return nullptr;
}

bool ShowcaseModule::grenadeInHand() const {
    const kke::InventoryItem* it = m_inv.equipped(kke::EquipSlot::RightHand);
    return it && it->id == "grenade" && m_held.body == kke::RigidWorld::kNoBody;
}

void ShowcaseModule::playSound(const std::shared_ptr<const kke::SoundBuffer>& sound, const glm::vec3& at, float gain, float range) {
    kke::AudioModule* audio = m_app->getModule<kke::AudioModule>();
    if (!audio || !sound) return;
    kke::VoiceDesc d;
    d.sound = sound;
    d.position = at;
    d.gain = gain;
    d.minDistance = 4.0f;
    d.maxDistance = range;
    d.category = kke::SoundCategory::Impact;
    audio->play(d);
}

// From readActions: the trigger and aim, while a gun or a grenade is in hand.
void ShowcaseModule::updateGuns(float dt, bool fireHeld, bool firePressed, bool aimHeld) {
    m_gunCooldown -= dt;
    m_sinceShot += dt;
    using State = kke::Locomotion::State;
    const State st = m_loco->state();
    const bool free = st == State::Ground || st == State::Air;
    const GunDef* gun = gunInHand();
    m_aimWanted = free && armed() && (aimHeld || m_sinceShot < kGunUpAfterShot);
    // The camera in over the shoulder while aim is held.
    const float want = free && armed() && aimHeld ? 1.0f : 0.0f;
    if (m_aimBlend <= 0.001f && want == 0.0f) {
        m_armBase = m_rig.settings.armLength; // the player's own zoom
        m_fovBase = m_rig.settings.fovDegrees;
    }
    m_aimBlend += (want - m_aimBlend) * (1.0f - std::exp(-12.0f * dt));
    if (m_aimBlend > 0.001f) {
        m_rig.settings.armLength = m_armBase + (1.5f - m_armBase) * m_aimBlend;
        m_rig.settings.fovDegrees = m_fovBase + (44.0f - m_fovBase) * m_aimBlend;
    }

    // Where the crosshair is: the camera's ray (the script sets it itself).
    if (!m_demoAim) {
        const kke::Camera& cam = m_app->camera();
        const glm::vec3 dir = glm::normalize(cam.target - cam.position);
        const kke::IPhysicsWorld::Hit h = kke::physicsRaycast(m_app->findCapability<kke::IPhysicsWorld>(), cam.position, dir, kAimRange);
        m_aimPoint = h.hit ? h.point : cam.position + dir * kAimRange;
    }

    // Reloading: the rounds come out of the bag when it's done.
    if (m_reloadLeft > 0.0f) {
        m_reloadLeft -= dt;
        if (m_reloadLeft <= 0.0f && gun) {
            int& loaded = m_loaded[gun->id];
            loaded += m_inv.removeById(gun->ammo, gun->magazine - loaded);
            m_invDirty = true;
            kke::log::get(name())->info("guns: {} reloaded, {} in it, {} left in the bag", gun->id, loaded, m_inv.count(gun->ammo));
        }
        return;
    }
    if (gun) {
        int& loaded = m_loaded[gun->id];
        const bool pull = gun->automatic ? fireHeld : firePressed;
        if (pull && m_gunCooldown <= 0.0f) {
            if (loaded > 0) {
                fireGun(*gun);
                --loaded;
                m_gunCooldown = gun->interval;
            } else if (firePressed) {
                playSound(m_sndClick, m_muzzleValid ? m_muzzle : m_aimPoint, 0.5f, 20.0f);
                if (m_inv.count(gun->ammo) == 0) {
                    const kke::ItemDef* ammo = m_items.find(gun->ammo);
                    toast(std::string("No ") + (ammo ? ammo->name : gun->ammo) + " in your bag");
                }
                m_gunCooldown = 0.3f;
            }
        }
        // Empty with rounds in the bag: reload.
        if (loaded == 0 && m_inv.count(gun->ammo) > 0 && m_gunCooldown <= 0.0f) {
            m_reloadLeft = gun->reload;
            playSound(m_sndReload, m_rigid->world().characterPosition(m_player) + glm::vec3(0, 1.2f, 0), 0.5f, 20.0f);
        }
    } else if (grenadeInHand() && firePressed && m_gunCooldown <= 0.0f) {
        throwGrenade();
        m_gunCooldown = 0.8f;
    }
}

void ShowcaseModule::fireGun(const GunDef& gun) {
    const kke::Camera& cam = m_app->camera();
    const glm::vec3 view = glm::normalize(cam.target - cam.position);
    // From the muzzle when the gun is drawn, else (first person) from just under the eye.
    const glm::vec3 right = glm::normalize(glm::cross(view, glm::vec3(0, 1, 0)));
    const glm::vec3 from = m_muzzleValid ? m_muzzle : cam.position + view * 0.6f + right * 0.15f - glm::vec3(0, 0.15f, 0);
    glm::vec3 dir = m_aimPoint - from;
    dir = glm::length(dir) > 0.5f ? glm::normalize(dir) : view;
    // From the hip it scatters a little; aimed, hardly at all.
    const float spread = glm::radians(m_aimBlend > 0.5f ? 0.25f : 1.6f);
    const glm::vec3 up = glm::normalize(glm::cross(right, dir));
    dir = glm::normalize(dir + right * ((random01(m_shotSeed) - 0.5f) * 2.0f * spread) + up * ((random01(m_shotSeed) - 0.5f) * 2.0f * spread));
    bulletHit(from, dir, gun);

    m_fxLib->play("muzzle_flash", { from, dir });
    m_flash = 0.05f;
    m_flashStrength = 8.0f;
    m_flashAt = from + dir * 0.2f;
    m_flashColor = glm::vec3(1.0f, 0.75f, 0.4f);
    // A tracer, every shot (the range is for seeing where they go).
    kke::ParticleEffects::Particle t;
    t.kind = kke::ParticleEffects::Kind::Spark;
    t.position = from + dir * 1.0f;
    t.velocity = dir * 320.0f;
    t.color = glm::vec3(7.0f, 4.5f, 1.8f);
    t.radius = 0.012f;
    t.life = std::min(0.6f, glm::length(m_aimPoint - from) / 320.0f);
    t.drag = 0.0f;
    t.rise = 0.0f;
    t.stretch = 0.01f;
    t.floor = -1000.0f;
    m_fx->emit(t);
    playSound(std::string(gun.id) == "rifle" ? m_sndRifle : m_sndPistol, from, 1.0f, 400.0f);
    // Kick: the view climbs a little.
    m_rig.addLook((random01(m_shotSeed) - 0.5f) * 0.5f, std::string(gun.id) == "rifle" ? 0.6f : 1.2f);
    m_sinceShot = 0.0f;
}

void ShowcaseModule::bulletHit(const glm::vec3& from, const glm::vec3& dir, const GunDef& gun) {
    kke::RigidWorld& w = m_rigid->world();
    const kke::RigidWorld::RayHit jolt = w.raycast(from, dir, kAimRange);
    kke::IPhysicsWorld::Hit soft;
#if KKE_ENABLE_FEMFX
    if (m_femfx) soft = m_femfx->physicsRaycast(from, dir, kAimRange);
#endif
    if (soft.hit && (!jolt.hit || soft.distance < jolt.distance)) {
#if KKE_ENABLE_FEMFX
        // A small iron slug just short of the surface, fast: FEMFX feels
        // the hit as an impact and breaks there (glass, plank, stone).
        kke::Material iron;
        iron.density = 7800.0f;
        iron.stiffness = 2.0e7f;
        iron.poissonsRatio = 0.3f;
        iron.fractureStressThreshold = 1.0e12f;
        iron.metallic = 0.9f;
        iron.roughness = 0.35f;
        iron.textureId = 2;
        const uint32_t h = m_femfx->spawnFracturableTetMesh(kke::PhysicsModule::buildSphere(2, 0.09f), soft.point - dir * 0.3f, iron, dir * 30.0f);
        if (h != kke::PhysicsModule::kInvalidHandle) m_slugs.push_back({ h, 0.0f });
        if (m_demoGuns >= 0.0f) kke::log::get(name())->info("guns: hit a FEMFX object at {:.2f} {:.2f} {:.2f}", soft.point.x, soft.point.y, soft.point.z);
        m_fxLib->play("stone_chips", { soft.point, soft.normal, glm::vec3(0.0f), 0.5f, groundHeight(soft.point.x, soft.point.z) });
#endif
        return;
    }
    if (!jolt.hit) return;
    const glm::vec3 p = jolt.point;
    if (m_demoGuns >= 0.0f) kke::log::get(name())->info("guns: hit body {} (material {}) at {:.2f} {:.2f} {:.2f}", jolt.body, jolt.material, p.x, p.y, p.z);
    // What it hit: a barrel lights, a plate rings, anything else moves.
    for (RangeThing& r : m_range)
        if (r.body == jolt.body && r.kind == RangeKind::Barrel && r.fuse < 0.0f) r.fuse = 0.1f;
    for (const SpawnDummy& dm : m_dummies)
        for (kke::RigidWorld::BodyId b : w.ragdollBodies(dm.ragdoll))
            if (b == jolt.body) w.addVelocity(b, dir * (gun.push * 0.25f));
    w.addImpulse(jolt.body, dir * gun.push, p); // dynamic bodies only (Jolt ignores the rest)
    const char* effect = jolt.material == kMetal ? "sparks" : jolt.material == kWood ? "wood_chips" : jolt.material == kStone ? "stone_chips" : "impact_dirt";
    m_fxLib->play(effect, { p, jolt.normal, glm::vec3(0.0f), jolt.material == kMetal ? 0.6f : 0.5f, groundHeight(p.x, p.z) });
    if (kke::AudioModule* audio = m_app->getModule<kke::AudioModule>())
        audio->playImpact(p, jolt.material ? jolt.material : kStone, jolt.material == kMetal ? 0.9f : 0.5f, nextRandom(m_shotSeed), 1.0f);
}

void ShowcaseModule::throwGrenade() {
    const kke::InventoryItem* it = m_inv.equipped(kke::EquipSlot::RightHand);
    if (!it) return;
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 chest = w.characterPosition(m_player) + glm::vec3(0, 1.5f, 0);
    const glm::vec3 from = m_muzzleValid ? m_muzzle : chest;
    glm::vec3 dir = m_aimPoint - from;
    dir = glm::length(dir) > 0.5f ? glm::normalize(dir) : m_loco->facing();
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Sphere;
    d.radius = 0.05f;
    d.mass = 0.45f;
    d.restitution = 0.3f;
    d.friction = 0.8f;
    d.material = kMetal;
    d.position = from + dir * 0.3f;
    d.velocity = dir * 14.0f + glm::vec3(0.0f, 3.5f, 0.0f) + w.characterVelocity(m_player);
    d.angularVelocity = glm::vec3(4.0f, 0.0f, 2.0f);
    const kke::RigidWorld::BodyId body = w.add(d);
    if (body == kke::RigidWorld::kNoBody) return;
    m_grenades.push_back({ body, 3.0f });
    // One fewer in the hand; the next from the bag takes its place.
    m_inv.remove(it->uid, 1);
    if (!m_inv.equipped(kke::EquipSlot::RightHand))
        for (const kke::InventoryItem& next : m_inv.items())
            if (next.id == "grenade" && next.slot < 0) {
                m_inv.equip(m_items, next.uid, kke::EquipSlot::RightHand);
                break;
            }
    syncEquipment();
    m_invDirty = true;
    m_sinceShot = 0.0f;
    kke::log::get(name())->info("guns: grenade thrown from {:.1f} {:.1f} {:.1f}, {} left", from.x, from.y, from.z, m_inv.count("grenade"));
}

// A blast at `at`: everything movable in both physics engines is pushed
// away (kke::physicsBlast), FEMFX walls crack, barrels near it go up
// after it, and there's a fireball, smoke, a flash and a boom.
void ShowcaseModule::explode(const glm::vec3& at, float power) {
    const float radius = 7.0f * power, speed = 16.0f * power;
    const size_t pushed = kke::physicsBlast(m_app->findCapability<kke::IPhysicsWorld>(), at, radius, speed);
    kke::RigidWorld& w = m_rigid->world();
    for (RangeThing& r : m_range) {
        if (r.kind != RangeKind::Barrel || r.fuse >= 0.0f || r.body == kke::RigidWorld::kNoBody) continue;
        const float d = glm::length(w.position(r.body) - at);
        if (d < radius * 0.75f) r.fuse = 0.15f + 0.25f * d / radius; // a chain, one after another
    }
    for (Grenade& g : m_grenades)
        if (glm::length(w.position(g.body) - at) < radius * 0.5f) g.fuse = std::min(g.fuse, 0.1f);
    const float floor = groundHeight(at.x, at.z);
    m_fxLib->play("explosion", { at, glm::vec3(0, 1, 0), glm::vec3(0.0f), 1.5f * power, floor });
    m_fxLib->play("impact_dirt", { at, glm::vec3(0, 1, 0), glm::vec3(0.0f), 2.5f * power, floor });
    m_fxLib->play("shockwave", { glm::vec3(at.x, floor + 0.1f, at.z), glm::vec3(0, 1, 0), glm::vec3(0.0f), 1.5f * power, floor });
    m_flash = 0.3f;
    m_flashStrength = 60.0f * power;
    m_flashAt = at + glm::vec3(0, 1.0f, 0);
    m_flashColor = glm::vec3(1.0f, 0.55f, 0.2f);
    playSound(m_sndBoom, at, 1.0f, 900.0f);
    const float d = glm::length(m_app->camera().position - at);
    m_shake = std::max(m_shake, std::clamp(1.0f - d / 60.0f, 0.0f, 1.0f) * power);
    kke::log::get(name())->info("guns: explosion at {:.1f} {:.1f} {:.1f}, pushed {} bodies and pieces", at.x, at.y, at.z, pushed);
}

// The aim pose: on top of the walk, the gun up in front of the eye along
// the crosshair, the left hand on the rifle's fore-end (both hands on a
// pistol); a grenade back over the shoulder. CharacterIk fades it in.
void ShowcaseModule::aimHands(const kke::Pose& pose, const glm::mat4& toWorld) {
    if (!m_aimWanted || !m_ik.arm(kke::CharacterIk::Right).valid()) return;
    using Side = kke::CharacterIk::Side;
    const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
    auto shoulder = [&](Side s) {
        const kke::HumanArm& arm = m_ik.arm(s);
        return arm.valid() ? glm::vec3(toWorld * bones[static_cast<size_t>(arm.chain.upper)][3]) : glm::vec3(toWorld[3]) + glm::vec3(0, 1.4f, 0);
    };
    const glm::vec3 sr = shoulder(Side::Right), sl = shoulder(Side::Left);
    glm::vec3 d = m_aimPoint - sr;
    if (glm::length(d) < 1.5f) d = m_app->camera().target - m_app->camera().position;
    d = glm::normalize(d);
    const glm::vec3 right = glm::normalize(glm::cross(d, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, d);
    const bool leftFree = !m_inv.equipped(kke::EquipSlot::LeftHand);
    if (grenadeInHand()) {
        const glm::vec3 hand = sr + up * 0.25f - d * 0.15f + right * 0.08f;
        m_ik.hand(Side::Right, hand, hand - up * 0.3f + right * 0.3f);
        return;
    }
    const GunDef* gun = gunInHand();
    if (!gun) return;
    if (std::string(gun->id) == "rifle") {
        // The stock in the shoulder, the grip a hand in front of it.
        const glm::vec3 grip = sr + d * 0.3f - up * 0.06f - right * 0.04f;
        m_ik.hand(Side::Right, grip, grip - up * 0.35f + right * 0.3f - d * 0.1f);
        if (leftFree) {
            const glm::vec3 fore = grip + d * 0.36f + up * 0.02f;
            m_ik.hand(Side::Left, fore, fore - up * 0.35f - right * 0.3f);
        }
    } else {
        // Arms out, both hands on it, in front of the chest's middle.
        const glm::vec3 mid = (sl + sr) * 0.5f;
        const glm::vec3 grip = mid + d * 0.5f - up * 0.1f + right * 0.03f;
        m_ik.hand(Side::Right, grip, grip - up * 0.35f + right * 0.3f);
        if (leftFree) {
            const glm::vec3 cup = grip - right * 0.045f - up * 0.03f;
            m_ik.hand(Side::Left, cup, cup - up * 0.35f - right * 0.3f);
        }
    }
}

// The gun in the right hand, turned to point along the crosshair as the
// aim pose fades in (the hand's own turn says little about where it aims).
glm::mat4 ShowcaseModule::aimedGun(const glm::mat4& inHand) const {
    const float wgt = m_ik.handWeight(kke::CharacterIk::Right);
    const glm::vec3 pos(inHand[3]);
    glm::vec3 d = m_aimPoint - pos;
    if (glm::length(d) < 1.0f) d = m_app->camera().target - m_app->camera().position;
    d = glm::normalize(d);
    const glm::vec3 z = glm::normalize(glm::cross(d, glm::vec3(0, 1, 0)));
    const glm::vec3 y = glm::cross(z, d);
    const glm::quat aimed = glm::quat_cast(glm::mat3(d, y, z));
    const glm::quat held = glm::quat_cast(unscaled(inHand));
    const glm::quat q = glm::slerp(held, aimed, std::clamp(wgt, 0.0f, 1.0f));
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(q);
}

// Physics time (fixedUpdate): grenades' and barrels' fuses burn as the
// world moves, so a slow frame doesn't set one off early in mid-air.
void ShowcaseModule::tickFuses(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    for (size_t k = 0; k < m_grenades.size();) {
        Grenade& g = m_grenades[k];
        g.fuse -= dt;
        if (g.fuse > 0.0f) {
            ++k;
            continue;
        }
        const glm::vec3 at = w.position(g.body);
        w.remove(g.body);
        m_grenades.erase(m_grenades.begin() + static_cast<std::ptrdiff_t>(k));
        explode(at + glm::vec3(0, 0.2f, 0), 0.8f);
    }
    for (RangeThing& r : m_range) {
        if (r.kind != RangeKind::Barrel || r.fuse < 0.0f || r.body == kke::RigidWorld::kNoBody) continue;
        r.fuse -= dt;
        if (r.fuse > 0.0f) continue;
        const glm::vec3 at = w.position(r.body);
        w.remove(r.body);
        r.body = kke::RigidWorld::kNoBody;
        explode(at, 1.0f);
    }
    for (Slug& s : m_slugs) s.age += dt;
}

// Every frame: slugs that have done their work, break effects, the flash
// light, the camera shake, the particles.
void ShowcaseModule::updateEffects(float dt) {
#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        std::erase_if(m_slugs, [this](const Slug& s) {
            if (s.age < 1.2f) return false;
            m_femfx->removeObject(s.handle);
            return true;
        });
        // Breaking makes chips and dust the colour of what broke.
        for (const kke::PhysicsModule::BreakEvent& b : m_femfx->frameBreaks()) {
            const int tex = b.material.textureId;
            const char* fx = tex == 4 ? "glass_shatter" : tex == 0 ? "wood_chips" : tex == 2 ? nullptr : "stone_chips";
            if (fx) m_fxLib->play(fx, { b.position, glm::vec3(0, 1, 0), glm::vec3(0.0f), std::clamp(b.size, 0.4f, 1.5f), groundHeight(b.position.x, b.position.z) });
        }
    }
#endif
    // The flash: a point light for a moment (lights[1]; the sun is [0]).
    kke::Light& l = m_app->lighting().lights[1];
    if (m_flash > 0.0f) {
        m_flash -= dt;
        l.enabled = true;
        l.isDirectional = false;
        l.position = m_flashAt;
        l.color = m_flashColor;
        l.intensity = m_flashStrength * std::clamp(m_flash / 0.08f, 0.0f, 1.0f);
    } else {
        l.enabled = false;
    }
    m_shake *= std::exp(-3.5f * dt);
    m_fxLib->update(dt, glm::vec3(1.0f, 0.0f, 0.4f));
}

void ShowcaseModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_fx && m_fx->count()) m_fx->draw(ctx);
}

} // namespace kke_showcase
