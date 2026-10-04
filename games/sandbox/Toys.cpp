// The sandbox's toys (Kees, 2026-09-28): hit anything, anywhere ("you
// should just pinpoint the spot where you want to hit them"), a gun, fire
// and melting, animals that tumble, props that roll and tumble instead of
// standing still, and Walk mode: being a person in the world who picks the
// bat up and does the stuff.
//
// Everything here is built from engine pieces a game would use the same
// way: Jolt bodies (kke::RigidWorld) for the props, ragdolls
// (kke::IRagdollPhysics) for people and animals, FEMFX (PhysicsModule) for
// what dents and breaks, kke::ParticleLibrary for flames, smoke and chips,
// ModelModule::setTint / setDeformedVertices for charring and melting,
// and kke::Locomotion + kke::CameraRig + kke::CharacterIk for the walker
// (Walker.h).

#include "SandboxModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/ParticleLibrary.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>

namespace kke_sandbox {

namespace {

constexpr float kGunCooldown = 0.18f;   // s between shots (held: automatic)
constexpr float kGunRange = 150.0f;     // m
constexpr float kGunPush = 14.0f;       // m/s given where a shot lands
constexpr float kBurnSeconds = 14.0f;   // a wooden prop burns this long
constexpr float kPersonBurnSeconds = 3.5f;
constexpr float kMeltStart = 0.35f;     // heat (0..1) a prop starts to sag at
constexpr float kDynamicMaxSize = 3.0f; // m: bigger props stay put (walls, cars)
constexpr int kDynamicPerFrame = 8;     // props turned into Jolt bodies per frame entering Play
constexpr float kPickupReach = 1.8f;    // m from your feet
constexpr float kAnimalDownSeconds = 5.0f;
constexpr float kMouseDegreesPerPixel = 0.12f;
constexpr float kStickDegreesPerSecond = 200.0f;

using Side = kke::CharacterIk::Side;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool nameHas(const std::string& name, std::initializer_list<const char*> words) {
    const std::string n = lower(name);
    return std::any_of(words.begin(), words.end(), [&](const char* w) { return n.find(w) != std::string::npos; });
}

// The level itself: never knocked about.
bool structural(const std::string& name) {
    return nameHas(name, { "floor", "wall", "ground", "road", "terrain", "bld_", "building", "env_", "tile", "stairs", "ramp",
                           "platform", "fence", "door", "window", "roof", "pillar", "column" });
}

bool flammable(const std::string& name) {
    kke::BreakKind kind;
    if (kke::guessBreakKind(name, kind) && kind == kke::BreakKind::Wood) return true;
    return nameHas(name, { "wood", "crate", "barrel", "hay", "tree", "bush", "plant", "grass", "chair", "table", "bench", "book",
                           "paper", "box", "cardboard", "log", "plank", "cloth", "rope", "sack", "bed", "sofa", "couch" });
}

// What it sounds like when hit (kke::AudioMaterialTable ids).
uint32_t soundFor(const std::string& name) {
    using M = kke::AudioMaterialTable;
    kke::BreakKind kind;
    if (kke::guessBreakKind(name, kind)) {
        switch (kind) {
        case kke::BreakKind::Wood: return M::Wood;
        case kke::BreakKind::Stone: return M::Stone;
        case kke::BreakKind::Glass: return M::Glass;
        case kke::BreakKind::Ceramic: return M::Stone;
        case kke::BreakKind::Metal: return M::Metal;
        }
    }
    if (nameHas(name, { "ball", "tyre", "tire", "rubber" })) return M::Rubber;
    if (nameHas(name, { "cone", "plastic", "bin", "bucket" })) return M::Plastic;
    if (nameHas(name, { "metal", "car", "veh_", "sign", "pipe", "can", "lamp" })) return M::Metal;
    if (nameHas(name, { "rock", "stone", "brick", "statue" })) return M::Stone;
    return M::Wood;
}

// The particle effect for chips flying off what was hit.
const char* chipsFor(uint32_t sound) {
    using M = kke::AudioMaterialTable;
    switch (sound) {
    case M::Wood: return "wood_chips";
    case M::Stone: return "stone_chips";
    case M::Glass: return "glass_shatter";
    case M::Metal: return "sparks";
    default: return "dust";
    }
}

// A ray against a box (min..max in the box's own space, placed in the
// world by `toWorld`, which may scale). t is in world metres along the
// (normalized) world ray; the normal faces the ray. A ray starting inside
// doesn't hit it (the walker's own capsule, a camera inside a crate).
bool rayBox(const kke::Ray& ray, const glm::mat4& toWorld, const glm::vec3& mn, const glm::vec3& mx, float& t, glm::vec3& normal) {
    const glm::mat4 inv = glm::inverse(toWorld);
    const glm::vec3 o(inv * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 d(inv * glm::vec4(ray.direction, 0.0f));
    float enter = -1e30f, leave = 1e30f;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
            continue;
        }
        float t1 = (mn[i] - o[i]) / d[i], t2 = (mx[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > enter) {
            enter = t1;
            axis = i;
        }
        leave = std::min(leave, t2);
        if (enter > leave) return false;
    }
    if (axis < 0 || enter < 0.0f) return false;
    t = enter;
    glm::vec3 n(0.0f);
    n[axis] = d[axis] > 0.0f ? -1.0f : 1.0f;
    normal = glm::normalize(glm::vec3(glm::transpose(inv) * glm::vec4(n, 0.0f)));
    return true;
}

glm::vec3 flat(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(0.0f, 0.0f, -1.0f)) {
    const glm::vec3 f(v.x, 0.0f, v.z);
    return glm::dot(f, f) > 1e-8f ? glm::normalize(f) : fallback;
}

glm::mat4 rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    return glm::mat4_cast(glm::quat(glm::normalize(from), glm::normalize(to)));
}

// A deterministic 0..1 per call (fire spreading, which side a burning person rolls to).
float chance(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

uint32_t g_fireDice = 0x5eed1234u;

} // namespace

// ---------------------------------------------------------------- set-up

void SandboxModule::initToys() {
    m_shell = m_app->getModule<kke::GameShellModule>();
    m_input = m_app->getModule<kke::InputModule>();
    m_fx = std::make_unique<kke::ParticleEffects>(*m_app, 6000);
    m_effects = std::make_unique<kke::ParticleLibrary>(*m_fx);

    if (m_input) {
        // Walk mode's controls: the usual character ones (WASD, mouse,
        // Space, Shift, C, V, E and a controller's sticks and buttons),
        // plus the sandbox's own. Rebindable in the menu's Controls.
        kke::InputMap& in = m_input->map(0);
        kke::InputModule::defineCharacterActions(in);
        in.defineAction({ "sandbox.getup", "Everyone up", "Sandbox", "game" });
        in.defineAction({ "sandbox.cursor", "Show the mouse (click the palette)", "Sandbox", "game" });
        in.defineAction({ "sandbox.prev", "Palette: previous", "Sandbox", "game" });
        in.defineAction({ "sandbox.next", "Palette: next", "Sandbox", "game" });
        in.defineAction({ "sandbox.use", "Palette: use the picture", "Sandbox", "game" });
        using IM = kke::InputModule;
        in.addBinding(IM::bind("sandbox.getup", IM::key(SDL_SCANCODE_R)));
        in.addBinding(IM::bind("sandbox.getup", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
        in.addBinding(IM::bind("sandbox.cursor", IM::key(SDL_SCANCODE_TAB)));
        in.addBinding(IM::bind("sandbox.prev", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)));
        in.addBinding(IM::bind("sandbox.next", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
        in.addBinding(IM::bind("sandbox.use", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        m_input->commitDefaults();
        // On a touch screen: a stick, the look drag and the buttons a
        // walker needs, only while walking (flying, a finger is the mouse).
        kke::TouchLayoutOptions touch;
        touch.buttons = { "fire", "jump", "interact", "crouch", "camera.toggle", "sandbox.getup" };
        m_input->setTouchLayout(touch);
        m_input->addTouchHider([this] { return m_mode != Mode::Play || m_view != View::Walk; });
    }

    // Who you are in Walk mode: the mannequin, or one of the people.
    m_walkPeople.clear();
    for (size_t i = 0; i < m_blocks.size(); ++i)
        if (m_blocks[i].kind == kke::PlayBlockKind::Character)
            for (const std::string& a : m_blockAssets[i])
                if (m_walkPeople.size() < 8) m_walkPeople.push_back(a);

    if (!m_shell) return;
    // The menus every game has (docs/GAME_SHELL.md): the title's two ways
    // to play, the pause menu's sandbox rows, and its settings.
    m_shell->addMode({ "fly", "Fly around", "Look at the world from above and drag things in" });
    m_shell->addMode({ "walk", "Walk around", "Be a person in the world: pick up the bat and play" });
    m_shell->onPlay = [this](const std::string& mode) {
        setMode(Mode::Play);
        setView(mode == "walk" ? View::Walk : View::Fly);
    };
    m_shell->onMainMenu = [this] { setView(View::Fly); };
    m_shell->blockPause = [this] { return escWouldCancel(); };
    // Build mode: Start is the editor's "back to Play"; Select and Esc pause.
    m_shell->startIsTheGames = [this] { return m_mode == Mode::Build; };
    m_shell->addPauseItem("Walk around", [this] { m_shell->closeMenu(); setMode(Mode::Play); setView(View::Walk); },
                          [this] { return m_view == View::Fly; });
    m_shell->addPauseItem("Fly around", [this] { m_shell->closeMenu(); setView(View::Fly); }, [this] { return m_view == View::Walk; });
    m_shell->addPauseItem("Build mode", [this] { m_shell->closeMenu(); setMode(Mode::Build); }, [this] { return m_mode == Mode::Play; });
    m_shell->addPauseItem("Back to Play", [this] { m_shell->closeMenu(); setMode(Mode::Play); }, [this] { return m_mode == Mode::Build; });
    m_shell->addPauseItem("Everyone up", [this] { m_shell->closeMenu(); standEveryoneUp(); });
    m_shell->addPauseItem("Clear everything", [this] {
        m_shell->closeMenu();
        pushUndo();
        clearAll();
    });
    std::vector<std::string> people{ "Mannequin" };
    for (const std::string& p : m_walkPeople) people.push_back(p);
    m_shell->settings("Sandbox")
        .toggle("Props break in Play", &m_autoBreakables)
        .toggle("Props roll in Play", &m_dynamicProps)
        .choice("Who you are (Walk)", &m_characterChoice, people, [this] {
            const size_t i = static_cast<size_t>(std::max(0, m_characterChoice));
            m_walkCharacter = i == 0 || i > m_walkPeople.size() ? std::string() : m_walkPeople[i - 1];
            if (!m_walker.useCharacter(&m_catalog, m_walkCharacter)) m_walkCharacter.clear();
        });
}

void SandboxModule::shutdownToys() {
    setCaptured(false);
    m_walker.end();
    if (m_effects) m_effects->stopAll();
    if (m_fx) m_fx->clear();
    for (Object& o : m_objects) {
        o.fire = 0;
        dropDynamic(o);
        if (o.ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(o.ragdoll);
        o.ragdoll = 0;
    }
    for (Pickup& p : m_pickups)
        if (p.instance) m_models->remove(p.instance);
    m_pickups.clear();
    for (ToolModel& t : m_toolModels)
        if (t.instance) m_models->remove(t.instance);
    // Before the renderer goes: its buffers are freed with the module.
    m_effects.reset();
    m_fx.reset();
}

// Back to how things were placed: Build mode edits the level, not what
// Play did to it.
void SandboxModule::resetToys() {
    m_toolTime = 0.0f;
    if (m_effects) {
        if (m_toolFx) m_effects->stop(m_toolFx);
        m_toolFx = 0;
    }
    for (Object& o : m_objects) {
        if (o.fire && m_effects) m_effects->stop(o.fire);
        o.fire = 0;
        o.burning = o.charred = o.heat = 0.0f;
        if (!o.meltFrom.empty()) {
            m_models->setDeformedVertices(o.instance, {}, {});
            o.meltFrom.clear();
            o.meltNormals.clear();
        }
        const bool wasMelted = o.melted > 0.0f;
        o.melted = 0.0f;
        m_models->setTint(o.instance, glm::vec3(1.0f));
        dropDynamic(o);
        o.bodyTried = false;
        if (o.animal && o.ragdoll) standAnimalUp(o);
        if (wasMelted) applyTransform(o);
    }
    for (Pickup& p : m_pickups)
        if (p.instance) m_models->remove(p.instance);
    m_pickups.clear();
}

kke::Ray SandboxModule::aimRay() const { return mouseRay(); }

// ---------------------------------------------------------------- where things are

bool SandboxModule::partBoxes(const Object& o, std::vector<std::pair<glm::mat4, glm::vec3>>& out) const {
    out.clear();
    if (!o.character && !o.animal) return false;
    if (o.ragdoll && m_ragdolls) {
        std::vector<glm::mat4> bodies;
        if (!m_ragdolls->ragdollBodyTransforms(o.ragdoll, bodies)) return false;
        for (size_t i = 0; i < bodies.size() && i < o.ragdollDesc.bodies.size(); ++i) out.push_back({ bodies[i], o.ragdollDesc.bodies[i].halfExtents });
        return !out.empty();
    }
    const kke::ModelData* d = m_models->model(o.model);
    if (!d || d->bones.empty()) return false;
    const glm::mat4 instance = m_models->transform(o.instance);
    std::vector<glm::mat4> world = m_models->boneWorld(o.instance);
    for (glm::mat4& w : world) w = instance * w;
    const kke::RagdollDesc desc = o.animal ? kke::buildQuadrupedRagdoll(*d, world) : kke::buildHumanoidRagdoll(*d, world);
    for (const kke::RagdollBody& b : desc.bodies) out.push_back({ b.transform, b.halfExtents });
    return !out.empty();
}

void SandboxModule::bodyBounds(const Object& o, glm::vec3& mn, glm::vec3& mx) const {
    if (o.ragdoll) {
        std::vector<std::pair<glm::mat4, glm::vec3>> parts;
        if (partBoxes(o, parts)) {
            mn = glm::vec3(1e30f);
            mx = glm::vec3(-1e30f);
            for (const auto& [t, half] : parts) {
                glm::vec3 a, b;
                kke::transformAabb(-half, half, t, a, b);
                mn = glm::min(mn, a);
                mx = glm::max(mx, b);
            }
            return;
        }
    }
    const kke::ModelData* d = m_models->model(o.model);
    if (d && (o.body != kke::RigidWorld::kNoBody || !o.meltFrom.empty())) {
        kke::transformAabb(d->boundsMin, d->boundsMax, m_models->transform(o.instance), mn, mx);
        if (!o.meltFrom.empty()) mx.y = std::max(mn.y + 0.05f, mx.y - (mx.y - mn.y) * o.melted * 0.85f);
        return;
    }
    worldBounds(o, mn, mx);
}

// The first thing along a ray: people and animals by their body parts
// (not the air between a T-pose's arms), props by their own box where
// they are now, the pieces of a broken prop by FEMFX, else the ground.
SandboxModule::Aim SandboxModule::aimAt(const kke::Ray& ray, float maxDistance) const {
    Aim best;
    best.distance = maxDistance;
    const float tGround = kke::rayPlaneY(ray, 0.0f);
    if (tGround >= 0.0f && tGround < best.distance) {
        best.hit = true;
        best.distance = tGround;
        best.point = ray.at(tGround);
        best.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    std::vector<std::pair<glm::mat4, glm::vec3>> parts;
    for (const Object& o : m_objects) {
        if (o.id == m_movingId) continue;
        glm::vec3 mn, mx;
        bodyBounds(o, mn, mx);
        const float coarse = kke::rayAabb(ray, mn - glm::vec3(0.05f), mx + glm::vec3(0.05f));
        if (coarse < 0.0f || coarse > best.distance) continue;
        float t = 0.0f;
        glm::vec3 n;
        if (o.character || o.animal) {
            if (!partBoxes(o, parts)) continue;
            for (const auto& [m, half] : parts)
                if (rayBox(ray, m, -half, half, t, n) && t < best.distance) best = { true, ray.at(t), n, o.id, t };
            continue;
        }
        const kke::ModelData* d = m_models->model(o.model);
        if (!d) continue;
#if KKE_ENABLE_FEMFX
        // Broken, a breakable is where its pieces are: FEMFX's own ray
        // test below. Whole, it is still where it was placed.
        if (o.proxy) {
            auto* physics = m_app->getModule<kke::PhysicsModule>();
            if (!physics || physics->pieceCount(o.proxy) > 1) continue;
        }
#endif
        const glm::mat4 m = o.body != kke::RigidWorld::kNoBody || !o.meltFrom.empty() ? m_models->transform(o.instance) : objectTransform(o);
        glm::vec3 lo = d->boundsMin, hi = d->boundsMax;
        if (!o.meltFrom.empty()) hi.y = std::max(lo.y + 0.02f, hi.y - (hi.y - lo.y) * o.melted * 0.85f);
        if (rayBox(ray, m, lo, hi, t, n) && t < best.distance) best = { true, ray.at(t), n, o.id, t };
    }
#if KKE_ENABLE_FEMFX
    if (auto* physics = m_app->getModule<kke::PhysicsModule>()) {
        const kke::IPhysicsWorld::Hit h = physics->physicsRaycast(ray.origin, ray.direction, best.distance);
        if (h.hit && h.distance < best.distance)
            for (const Object& o : m_objects)
                if (o.proxy && o.proxy == h.body) {
                    best = { true, h.point, h.normal, o.id, h.distance };
                    break;
                }
    }
#endif
    return best;
}

// ---------------------------------------------------------------- hitting

void SandboxModule::strike(uint32_t thing, const glm::vec3& point, const glm::vec3& push, uint32_t sound) {
    const float strength = glm::length(push);
    if (auto* audio = m_app->getModule<kke::AudioModule>()) audio->playImpact(point, sound, std::clamp(strength / 10.0f, 0.2f, 1.0f), thing);
    m_lastHitPoint[thing] = point;
    Object* o = find(thing);
    kke::ParticleLibrary::Spot spot;
    spot.position = point;
    spot.normal = strength > 1e-4f ? -push / strength : glm::vec3(0.0f, 1.0f, 0.0f);
    spot.scale = std::clamp(strength / 12.0f, 0.4f, 1.2f);
    if (!o) {
        if (m_effects) m_effects->play("impact_dirt", spot);
        return;
    }
    if (o->character || o->animal) {
        knockOver(*o, push, &point);
        return;
    }
    if (m_effects) m_effects->play(chipsFor(sound), spot);
#if KKE_ENABLE_FEMFX
    if (o->proxy) {
        // A dent or a crack right there: the tets near the spot get the
        // blow, the rest only a little of it.
        if (auto* physics = m_app->getModule<kke::PhysicsModule>()) {
            const float radius = 0.45f;
            physics->changeVertexVelocities(o->proxy, [&](const glm::vec3& p, const glm::vec3& v) {
                const float w = std::clamp(1.0f - glm::length(p - point) / radius, 0.0f, 1.0f);
                return v + push * (0.15f + w * w * 1.6f);
            });
        }
        return;
    }
#endif
#if KKE_ENABLE_JOLT
    if (o->body != kke::RigidWorld::kNoBody) {
        if (kke::RigidWorld* w = rigidWorld()) w->addImpulse(o->body, push * o->bodyMass * 0.5f, point);
        return;
    }
#endif
    // Too big to move (a wall): only the chips above.
}

void SandboxModule::knockOver(Object& o, const glm::vec3& push, const glm::vec3* point) {
    if (!m_ragdolls) {
        m_status = "Knocking things over needs a physics module (ragdolls)";
        return;
    }
    const bool wasDown = o.ragdoll && (o.animal || isDown(o));
    if (o.animal) {
        if (!o.ragdoll && !makeAnimalRagdoll(o)) return;
        o.downFor = 0.0f;
    } else if (o.character) {
        if (o.ragdoll && o.active.valid() && !isDown(o)) {
            o.active.knockOut();
            m_ragdolls->driveRagdoll(o.ragdoll, o.active.drive());
        } else if (!o.ragdoll && !makeRagdoll(o)) {
            return;
        }
    } else {
        return;
    }
    if (!point) {
        for (const char* body : { "torso", "head", "chest" }) {
            const int b = o.ragdollDesc.findBody(body);
            if (b >= 0) m_ragdolls->pushRagdollBody(o.ragdoll, b, push);
        }
        const int pelvis = o.ragdollDesc.findBody("pelvis");
        if (pelvis >= 0) m_ragdolls->pushRagdollBody(o.ragdoll, pelvis, push * 0.4f);
    } else {
        // The part that was hit takes the blow; the rest follows through
        // the joints (a hit on the shin sweeps the legs, on the head snaps
        // it back).
        std::vector<glm::mat4> bodies;
        if (!m_ragdolls->ragdollBodyTransforms(o.ragdoll, bodies) || bodies.size() != o.ragdollDesc.bodies.size()) {
            bodies.clear();
            for (const kke::RagdollBody& b : o.ragdollDesc.bodies) bodies.push_back(b.transform);
        }
        size_t nearest = 0;
        float best = 1e30f;
        for (size_t i = 0; i < bodies.size(); ++i) {
            const float d = glm::length(glm::vec3(bodies[i][3]) - *point);
            if (d < best) {
                best = d;
                nearest = i;
            }
        }
        for (size_t i = 0; i < bodies.size(); ++i)
            m_ragdolls->pushRagdollBody(o.ragdoll, static_cast<int>(i), i == nearest ? push : push * 0.35f);
    }
    if (!wasDown) queueFellOver(o.id);
}

bool SandboxModule::makeAnimalRagdoll(Object& o) {
    if (!m_ragdolls || o.ragdoll) return false;
    const kke::ModelData* d = m_models->model(o.model);
    if (!d) return false;
    const glm::mat4 instance = m_models->transform(o.instance);
    std::vector<glm::mat4> world = m_models->boneWorld(o.instance);
    for (glm::mat4& w : world) w = instance * w;
    // Roughly what it weighs: a sheep 60 kg, a horse 500.
    glm::vec3 mn, mx;
    worldBounds(o, mn, mx);
    const glm::vec3 size = mx - mn;
    const float mass = std::clamp(110.0f * size.x * size.y * size.z, 15.0f, 600.0f);
    std::string missing;
    o.ragdollDesc = kke::buildQuadrupedRagdoll(*d, world, mass, &missing);
    if (o.ragdollDesc.bodies.empty()) {
        m_status = o.asset + " can't tumble: no '" + missing + "' bone";
        kke::log::get(name())->warn("{}", m_status);
        return false;
    }
    o.binding = kke::bindSkeletonToRagdoll(*d, world, o.ragdollDesc);
    o.ragdoll = m_ragdolls->createRagdoll(o.ragdollDesc, glm::vec3(0.0f));
    if (o.ragdoll) kke::log::get(name())->info("'{}' tumbles ({} bodies, {:.0f} kg)", o.asset, o.ragdollDesc.bodies.size(), mass);
    return o.ragdoll != 0;
}

// Back on its feet where it landed, and the AI takes it from there.
void SandboxModule::standAnimalUp(Object& o) {
    if (!o.ragdoll || !m_ragdolls) return;
    std::vector<glm::mat4> bodies;
    const int pelvis = o.ragdollDesc.findBody("pelvis");
    if (pelvis >= 0 && m_ragdolls->ragdollBodyTransforms(o.ragdoll, bodies) && static_cast<size_t>(pelvis) < bodies.size()) {
        const glm::vec3 p(bodies[static_cast<size_t>(pelvis)][3]);
        if (std::isfinite(p.x) && std::isfinite(p.z)) o.position = glm::vec3(p.x, o.position.y, p.z);
    }
    if (o.fire && m_effects) m_effects->stop(o.fire);
    o.fire = 0;
    o.burning = 0.0f;
    m_ragdolls->destroyRagdoll(o.ragdoll);
    o.ragdoll = 0;
    o.downFor = 0.0f;
    m_models->setBoneWorldOverride(o.instance, {});
    m_models->setTransform(o.instance, objectTransform(o));
    queueStoodUp(o.id);
}

// ---------------------------------------------------------------- props that roll

void SandboxModule::makeDynamic(Object& o) {
    o.bodyTried = true;
    kke::RigidWorld* w = rigidWorld();
    const kke::ModelData* d = m_models->model(o.model);
    if (!w || !d || o.character || o.animal || o.proxy || o.ragdoll || structural(o.asset)) return;
    glm::vec3 mn, mx;
    worldBounds(o, mn, mx);
    const glm::vec3 size = mx - mn;
    if (std::max({ size.x, size.y, size.z }) > kDynamicMaxSize || std::min({ size.x, size.y, size.z }) < 0.02f) return;
    // The convex hull of its own vertices (where it is placed, around its
    // centre), a few hundred points at most.
    const glm::mat4 placed = objectTransform(o);
    const glm::vec3 center = (mn + mx) * 0.5f;
    size_t total = 0;
    for (const kke::ModelMesh& m : d->meshes) total += m.vertices.size();
    const size_t stride = std::max<size_t>(1, total / 300);
    kke::RigidWorld::BodyDesc b;
    b.shape = kke::RigidWorld::Shape::ConvexHull;
    size_t k = 0;
    for (const kke::ModelMesh& m : d->meshes)
        for (const kke::ModelVertex& v : m.vertices)
            if (k++ % stride == 0) b.points.push_back(glm::vec3(placed * glm::vec4(v.position, 1.0f)) - center);
    if (b.points.size() < 4) return;
    b.motion = kke::RigidWorld::Motion::Dynamic;
    b.position = center;
    b.material = soundFor(o.asset);
    // Light enough to send flying, heavy enough to knock people over.
    const float volume = size.x * size.y * size.z;
    b.mass = std::clamp(volume * 250.0f, 2.0f, 400.0f);
    b.friction = 0.7f;
    b.restitution = b.material == kke::AudioMaterialTable::Rubber ? 0.6f : 0.15f;
    o.body = w->add(b);
    if (o.body == kke::RigidWorld::kNoBody) return;
    o.bodyCenter = center;
    o.bodyMass = b.mass;
    dropCollider(o); // it is its own collider now
}

void SandboxModule::dropDynamic(Object& o) {
    if (o.body == kke::RigidWorld::kNoBody) return;
    if (kke::RigidWorld* w = rigidWorld()) w->remove(o.body);
    o.body = kke::RigidWorld::kNoBody;
    if (o.meltFrom.empty()) applyTransform(o); // back where it was placed, solid again
}

void SandboxModule::updateDynamicProps() {
    kke::RigidWorld* w = rigidWorld();
    if (!w) return;
    const bool on = m_mode == Mode::Play && m_dynamicProps;
    int budget = kDynamicPerFrame;
    for (Object& o : m_objects) {
        if (!on) {
            if (o.body != kke::RigidWorld::kNoBody) dropDynamic(o);
            o.bodyTried = false;
            continue;
        }
        if (o.body == kke::RigidWorld::kNoBody) {
            // After Play mode has tried making it breakable: a prop that
            // breaks doesn't also roll.
            const bool decided = o.autoTried || !m_autoBreakables || !m_hasFemfx;
            if (!o.bodyTried && decided && budget > 0 && o.id != m_movingId && !o.proxy && o.meltFrom.empty()) {
                makeDynamic(o);
                --budget;
            }
            continue;
        }
        if (o.id == m_movingId) continue; // carried: placing puts it down where it lands
        const glm::mat4 t = w->transform(o.body);
        if (t[3].y < -30.0f) { // fell off the world: back where it was placed
            dropDynamic(o);
            continue;
        }
        m_models->setTransform(o.instance, t * glm::translate(glm::mat4(1.0f), -o.bodyCenter) * objectTransform(o));
    }
}

// ---------------------------------------------------------------- the gun

SandboxModule::ToolModel* SandboxModule::toolModel(Tool tool) {
    const int i = tool == Tool::Gun ? 0 : tool == Tool::Fire ? 1 : tool == Tool::Melt ? 2 : -1;
    if (i < 0) return nullptr;
    ToolModel& t = m_toolModels[i];
    if (t.tried) return t.model ? &t : nullptr;
    t.tried = true;
    // Whatever the packs on disk have, most fitting first.
    static const std::vector<std::vector<const char*>> kWanted = {
        { "SM_Wep_Shotgun_01", "SM_Wep_Pistol_01", "SM_Wep_MusketPistol_01", "SM_Wep_Rifle_01" },
        { "SM_Prop_TorchStick_01", "SM_Prop_Torch_02", "SM_Prop_Torch_Ornate_01", "SM_Prop_BottleTorch_01" },
        { "SM_Wep_Pistol_01", "SM_Wep_MusketPistol_01", "SM_Wep_Shotgun_01" },
    };
    for (const char* name : kWanted[static_cast<size_t>(i)]) {
        if (!m_catalog.find(name)) continue;
        t.model = loadAsset(name);
        const kke::ModelData* d = t.model ? m_models->model(t.model) : nullptr;
        if (!d) {
            t.model = 0;
            continue;
        }
        std::vector<glm::vec3> points;
        for (const kke::ModelMesh& mesh : d->meshes)
            for (const kke::ModelVertex& v : mesh.vertices) points.push_back(v.position);
        t.axis = kke::findLongAxis(points);
        t.instance = m_models->spawn(t.model);
        m_models->setVisible(t.instance, false);
        m_models->setOverlayEnabled(t.instance, false);
        if (tool == Tool::Melt) m_models->setTint(t.instance, glm::vec3(1.6f, 0.7f, 0.35f)); // a hot-orange heat gun
        break;
    }
    return t.model ? &t : nullptr;
}

void SandboxModule::loadBat() {
    if (m_batModel || m_blocks.empty()) return;
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].kind != kke::PlayBlockKind::Tool || m_blockAssets[i].empty()) continue;
        m_batModel = loadAsset(m_blockAssets[i].front());
        if (const kke::ModelData* d = m_batModel ? m_models->model(m_batModel) : nullptr) {
            std::vector<glm::vec3> points;
            for (const kke::ModelMesh& mesh : d->meshes)
                for (const kke::ModelVertex& v : mesh.vertices) points.push_back(v.position);
            m_batAxis = kke::findLongAxis(points);
            m_bat = m_models->spawn(m_batModel);
            m_models->setVisible(m_bat, false);
        }
        break;
    }
}

// Where a tool is held while walking: the gun and the heat gun at the
// shoulder pointing where you look, the torch out in front.
bool SandboxModule::heldToolPose(Tool tool, glm::mat4& model, glm::vec3& tip, glm::vec3& direction) {
    if (!m_walker.active()) return false;
    const glm::vec3 feet = m_walker.feet();
    const glm::vec3 f = m_walker.facing();
    const glm::vec3 right(-f.z, 0.0f, f.x);
    const glm::vec3 chest = feet + glm::vec3(0.0f, 1.35f, 0.0f);
    const Aim aim = aimAt(aimRay(), 80.0f);
    const glm::vec3 look = glm::normalize(m_app->camera().target - m_app->camera().position);
    direction = aim.hit && glm::length(aim.point - chest) > 1.0f ? glm::normalize(aim.point - chest) : look;
    ToolModel* tm = toolModel(tool);
    const float length = tm && tm->axis.length > 0.0f ? tm->axis.length : 0.6f;
    if (tool == Tool::Fire) {
        const glm::vec3 hand = chest + right * 0.25f + f * 0.35f - glm::vec3(0.0f, 0.2f, 0.0f);
        const glm::vec3 up = glm::normalize(direction + glm::vec3(0.0f, 0.8f, 0.0f));
        tip = hand + up * length * 0.85f;
        if (tm) model = glm::translate(glm::mat4(1.0f), hand) * rotationBetween(tm->axis.axis, up) *
                        glm::translate(glm::mat4(1.0f), -(tm->axis.handle + tm->axis.axis * (length * 0.15f)));
        return true;
    }
    // Long guns: the narrow end (findLongAxis' handle) is the muzzle, the
    // wide end the stock at the shoulder.
    const glm::vec3 stock = chest + right * 0.2f - glm::vec3(0.0f, 0.05f, 0.0f) + direction * 0.05f;
    tip = stock + direction * length;
    if (tm) {
        const glm::vec3 stockModel = tm->axis.handle + tm->axis.axis * length;
        model = glm::translate(glm::mat4(1.0f), stock) * rotationBetween(-tm->axis.axis, direction) * glm::translate(glm::mat4(1.0f), -stockModel);
    }
    return true;
}

void SandboxModule::fireGun() {
    if (m_gunCooldown > 0.0f) return;
    m_gunCooldown = kGunCooldown;
    const kke::Ray ray = aimRay();
    glm::vec3 muzzle = ray.origin + ray.direction * 0.6f, dir = ray.direction;
    glm::mat4 unused;
    if (m_view == View::Walk && heldToolPose(Tool::Gun, unused, muzzle, dir)) {
        // From the muzzle toward what the crosshair is on.
        const Aim target = aimAt(ray, kGunRange);
        if (target.hit) dir = glm::normalize(target.point - muzzle);
    }
    const Aim aim = aimAt({ muzzle, dir }, kGunRange);
    if (m_effects) {
        kke::ParticleLibrary::Spot s;
        s.position = muzzle;
        s.normal = dir;
        s.scale = 0.6f;
        m_effects->play("muzzle_flash", s);
    }
    if (auto* audio = m_app->getModule<kke::AudioModule>()) audio->playImpact(muzzle, kke::AudioMaterialTable::Metal, 1.0f, 7);
    animalNoise(muzzle, 30.0f); // a bang: every animal around hears it
    if (!aim.hit) return;
    m_debug->line(muzzle, aim.point, glm::vec3(1.0f, 0.9f, 0.5f), 0.015f);
    const Object* o = find(aim.thing);
    strike(aim.thing, aim.point, dir * kGunPush, o ? soundFor(o->asset) : kke::AudioMaterialTable::Dirt);
}

// ---------------------------------------------------------------- fire

void SandboxModule::ignite(Object& o) {
    if (o.burning > 0.0f || !m_effects) return;
    glm::vec3 mn, mx;
    bodyBounds(o, mn, mx);
    kke::ParticleLibrary::Spot s;
    s.position = glm::vec3((mn.x + mx.x) * 0.5f, mx.y, (mn.z + mx.z) * 0.5f);
    s.floor = 0.0f;
    if (o.character || o.animal) {
        // Stop, drop and roll: down they go, flames and all, and it's out
        // a few seconds later.
        o.burning = kPersonBurnSeconds;
        s.position.y = (mn.y + mx.y) * 0.5f;
        s.scale = 0.6f;
        o.fire = m_effects->start("fire", s);
        const float side = chance(g_fireDice) < 0.5f ? -1.0f : 1.0f;
        const glm::vec3 away = flat(glm::vec3(s.position) - m_app->camera().position);
        knockOver(o, glm::vec3(-away.z, 0.0f, away.x) * (2.5f * side) + glm::vec3(0.0f, 1.0f, 0.0f), nullptr);
        return;
    }
    if (!flammable(o.asset)) {
        o.heat = std::min(1.0f, o.heat + 0.05f); // it only gets hot (and glows)
        return;
    }
    const glm::vec3 size = mx - mn;
    s.scale = std::clamp(std::max(size.x, size.z) * 1.2f, 0.4f, 2.0f);
    o.burning = kBurnSeconds;
    o.fire = m_effects->start("fire", s);
}

// Held on something: it catches (wood), heats up (metal, stone) or, a
// person or an animal, catches and rolls about.
void SandboxModule::useFire(float dt, bool held) {
    if (!held || !m_effects) {
        if (m_toolFx && m_effects) m_effects->stop(m_toolFx);
        m_toolFx = 0;
        m_toolTime = 0.0f;
        return;
    }
    glm::vec3 nozzle(0.0f), dir(0.0f);
    glm::mat4 unused;
    const bool walking = m_view == View::Walk && heldToolPose(Tool::Fire, unused, nozzle, dir);
    // A torch's reach in your hands; anywhere you point from the sky.
    const kke::Ray ray = aimRay();
    const float reach = walking ? glm::length(nozzle - ray.origin) + 4.0f : 400.0f;
    const Aim aim = aimAt(ray, reach);
    kke::ParticleLibrary::Spot s;
    s.position = aim.hit ? aim.point : (walking ? nozzle + dir * 1.5f : ray.at(8.0f));
    s.normal = aim.normal;
    s.scale = 0.45f;
    if (!m_toolFx) m_toolFx = m_effects->start("fire", s);
    else m_effects->move(m_toolFx, s);
    m_toolTime += dt;
    Object* o = aim.hit ? find(aim.thing) : nullptr;
    if (!o) return;
    if (o->character || o->animal) {
        if (m_toolTime > 0.3f) ignite(*o);
        return;
    }
    if (flammable(o->asset)) {
        if (m_toolTime > 0.5f) ignite(*o);
    } else {
        o->heat = std::min(0.3f, o->heat + dt * 0.25f); // glows, but a torch doesn't melt it
        applyTint(*o);
    }
}

void SandboxModule::applyTint(Object& o) {
    const float c = std::clamp(o.charred, 0.0f, 1.0f), h = std::clamp(o.heat, 0.0f, 1.0f);
    const glm::vec3 base = glm::mix(glm::vec3(1.0f), glm::vec3(0.12f, 0.1f, 0.09f), c);
    const glm::vec3 glow = glm::mix(glm::vec3(1.0f), glm::vec3(3.2f, 1.3f, 0.35f), h); // red hot: above 1 on purpose
    m_models->setTint(o.instance, base * glow);
}

void SandboxModule::updateFire(float dt) {
    std::vector<uint32_t> catching;
    for (Object& o : m_objects) {
        if (o.burning <= 0.0f) {
            if (o.heat > 0.0f && !(m_tool == Tool::Melt && m_toolTime > 0.0f)) {
                o.heat = std::max(0.0f, o.heat - dt * 0.05f); // cools down slowly
                applyTint(o);
            }
            continue;
        }
        o.burning -= dt;
        glm::vec3 mn, mx;
        bodyBounds(o, mn, mx);
        kke::ParticleLibrary::Spot s;
        const bool body = o.character || o.animal;
        s.position = glm::vec3((mn.x + mx.x) * 0.5f, body ? (mn.y + mx.y) * 0.5f : mx.y, (mn.z + mx.z) * 0.5f);
        s.scale = body ? 0.6f : std::clamp(std::max(mx.x - mn.x, mx.z - mn.z) * 1.2f, 0.4f, 2.0f) * std::min(1.0f, o.burning / 3.0f + 0.3f);
        if (o.fire && m_effects) m_effects->move(o.fire, s);
        if (!body) {
            o.charred = std::min(1.0f, o.charred + dt / kBurnSeconds * 1.3f);
            o.heat = std::min(0.5f, o.heat + dt * 0.1f);
            applyTint(o);
            // Wood burns down: past half charred it slumps into a heap.
            if (o.charred > 0.5f) {
                prepareMelt(o);
                o.melted = std::max(o.melted, std::min(0.8f, (o.charred - 0.5f) * 1.6f));
                meltShape(o);
            }
            // Flames reach what is next to it.
            for (const Object& other : m_objects) {
                if (other.id == o.id || other.burning > 0.0f || other.charred > 0.9f) continue;
                if (!(other.character || other.animal) && !flammable(other.asset)) continue;
                glm::vec3 a, b;
                bodyBounds(other, a, b);
                const bool near = a.x < mx.x + 0.5f && b.x > mn.x - 0.5f && a.z < mx.z + 0.5f && b.z > mn.z - 0.5f && a.y < mx.y + 0.5f &&
                                  b.y > mn.y - 0.2f;
                if (near && chance(g_fireDice) < dt * 0.35f) catching.push_back(other.id);
            }
        }
        if (o.burning <= 0.0f) {
            o.burning = 0.0f;
            if (o.fire && m_effects) m_effects->stop(o.fire);
            o.fire = 0;
            if (m_effects) {
                s.scale = std::max(0.5f, s.scale);
                m_effects->play("dust", s); // a last puff of smoke and ash
            }
        }
    }
    for (uint32_t id : catching)
        if (Object* o = find(id)) ignite(*o);
}

// ---------------------------------------------------------------- melting

// The shape melting starts from: the prop as it is drawn now, whole (a
// breakable is put back together first; a rolling one stops where it is).
void SandboxModule::prepareMelt(Object& o) {
    if (!o.meltFrom.empty()) return;
    if (o.proxy) restoreProp(o);
    o.autoTried = true; // and it stays a prop that melts, not one that breaks
    o.bodyTried = true;
    if (o.body != kke::RigidWorld::kNoBody) {
        if (kke::RigidWorld* w = rigidWorld()) w->remove(o.body);
        o.body = kke::RigidWorld::kNoBody;
    }
    const kke::ModelData* d = m_models->model(o.model);
    if (!d) return;
    const glm::mat4 m = m_models->transform(o.instance);
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    glm::vec3 mn(1e30f), mx(-1e30f);
    o.meltFrom.assign(d->meshes.size(), {});
    o.meltNormals.assign(d->meshes.size(), {});
    for (size_t i = 0; i < d->meshes.size(); ++i) {
        for (const kke::ModelVertex& v : d->meshes[i].vertices) {
            const glm::vec3 p(m * glm::vec4(v.position, 1.0f));
            o.meltFrom[i].push_back(p);
            o.meltNormals[i].push_back(glm::normalize(nm * v.normal));
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
        }
    }
    o.meltCenter = (mn + mx) * 0.5f;
    o.meltFloor = mn.y;
}

// Sags from the top and spreads at the bottom into a puddle.
void SandboxModule::meltShape(Object& o) {
    if (o.meltFrom.empty()) return;
    float top = o.meltFloor;
    for (const auto& part : o.meltFrom)
        for (const glm::vec3& p : part) top = std::max(top, p.y);
    const float height = std::max(0.01f, top - o.meltFloor);
    const float m = std::clamp(o.melted, 0.0f, 1.0f);
    std::vector<std::vector<glm::vec3>> pos(o.meltFrom.size()), nrm(o.meltFrom.size());
    for (size_t i = 0; i < o.meltFrom.size(); ++i) {
        pos[i].reserve(o.meltFrom[i].size());
        nrm[i].reserve(o.meltFrom[i].size());
        for (size_t k = 0; k < o.meltFrom[i].size(); ++k) {
            const glm::vec3& p = o.meltFrom[i][k];
            const float h = (p.y - o.meltFloor) / height; // 0 bottom .. 1 top
            const float y = o.meltFloor + (p.y - o.meltFloor) * (1.0f - m * (0.65f + 0.3f * h));
            const glm::vec2 out(p.x - o.meltCenter.x, p.z - o.meltCenter.z);
            const glm::vec2 xz = glm::vec2(p.x, p.z) + out * (m * 0.8f * (1.0f - 0.5f * h));
            pos[i].push_back(glm::vec3(xz.x, std::max(o.meltFloor + 0.01f, y), xz.y));
            nrm[i].push_back(glm::normalize(glm::mix(o.meltNormals[i][k], glm::vec3(0.0f, 1.0f, 0.0f), m * 0.6f)));
        }
    }
    m_models->setDeformedVertices(o.instance, pos, nrm);
    if (o.melted > 0.6f) dropCollider(o); // a puddle: people walk and roll over it
}

void SandboxModule::useMelt(float dt, bool held) {
    if (!held || !m_effects) {
        if (m_toolFx && m_effects) m_effects->stop(m_toolFx);
        m_toolFx = 0;
        m_toolTime = 0.0f;
        return;
    }
    glm::vec3 nozzle(0.0f), dir(0.0f);
    glm::mat4 unused;
    const bool walking = m_view == View::Walk && heldToolPose(Tool::Melt, unused, nozzle, dir);
    const kke::Ray ray = aimRay();
    const float reach = walking ? glm::length(nozzle - ray.origin) + 6.0f : 400.0f;
    const Aim aim = aimAt(ray, reach);
    m_toolTime += dt;
    kke::ParticleLibrary::Spot s;
    s.position = aim.hit ? aim.point : ray.at(8.0f);
    s.normal = aim.normal;
    s.scale = 0.5f;
    if (!m_toolFx) m_toolFx = m_effects->start("steam", s);
    else m_effects->move(m_toolFx, s);
    m_heatFxTime -= dt;
    if (m_heatFxTime <= 0.0f && aim.hit) {
        m_heatFxTime = 0.12f;
        m_effects->play("welding", s);
    }
    if (walking) m_debug->line(nozzle, s.position, glm::vec3(1.0f, 0.45f, 0.1f), 0.02f);
    Object* o = aim.hit ? find(aim.thing) : nullptr;
    if (!o) return;
    if (o->character || o->animal) {
        // Too hot to stand next to: they stagger away from it.
        if (std::fmod(m_toolTime, 0.6f) < dt) {
            const glm::vec3 away = flat(aim.point - ray.origin) * 2.5f;
            if (o->animal) knockOver(*o, away * 0.6f, &aim.point);
            else if (!stagger(*o, away)) knockOver(*o, away, &aim.point);
        }
        return;
    }
    o->heat = std::min(1.0f, o->heat + dt * 0.6f);
    applyTint(*o);
    if (o->heat < kMeltStart) return;
    prepareMelt(*o);
    o->melted = std::min(1.0f, o->melted + dt * 0.3f * o->heat);
    meltShape(*o);
}

void SandboxModule::updateMelt(float /*dt*/) {
    // Melted props stay as they are (meltShape draws them when they
    // change); the heat cools in updateFire. Nothing else moves them.
}

// ---------------------------------------------------------------- each frame

void SandboxModule::updateToys(float dt) {
    m_gunCooldown = std::max(0.0f, m_gunCooldown - dt);
    // Animals lie a few seconds, then get up and carry on.
    for (Object& o : m_objects) {
        if (!o.animal || !o.ragdoll) continue;
        o.downFor += dt;
        if (o.downFor > kAnimalDownSeconds && o.burning <= 0.0f) standAnimalUp(o);
    }
    updateDynamicProps();
    if (m_mode == Mode::Play) {
        // The tool in hand while the button is held (Fly mode: the mouse
        // over the world; Walk mode: useTool from updateWalker).
        if (m_view == View::Fly) {
            const bool held = m_app->window().mouseState().leftButtonDown && !mouseOverUi() && !m_touches.multiTouch() &&
                              !(m_shell && m_shell->menuOpen());
            if (m_tool == Tool::Gun && held) fireGun();
            if (m_tool == Tool::Fire) useFire(dt, held);
            else if (m_tool == Tool::Melt) useMelt(dt, held);
            else if (m_toolFx) useFire(dt, false);
        }
        updateWalker(dt);
    }
    updateFire(dt);
    updateMelt(dt);
    if (m_effects) m_effects->update(dt);

    // Aim markers (Fly mode): where a click lands.
    if (m_mode == Mode::Play && m_view == View::Fly && !mouseOverUi() &&
        (m_tool == Tool::Gun || m_tool == Tool::Fire || m_tool == Tool::Melt || (m_tool == Tool::Bat && !m_swing.active()))) {
        const Aim aim = aimAt(aimRay());
        if (aim.hit) {
            const glm::vec3 color = m_tool == Tool::Gun ? glm::vec3(1.0f, 0.3f, 0.2f) : m_tool == Tool::Bat ? glm::vec3(1.0f, 0.85f, 0.2f)
                                                                                                           : glm::vec3(1.0f, 0.55f, 0.1f);
            m_debug->cross(aim.point, aim.thing ? 0.18f : 0.3f, color);
        }
    }
}

void SandboxModule::renderToys(const kke::RenderContext& ctx) {
    if (m_fx) m_fx->draw(ctx);
}

void SandboxModule::renderTranslucent(const kke::RenderContext& ctx) { renderToys(ctx); }

// ---------------------------------------------------------------- Walk mode

void SandboxModule::setCaptured(bool on) {
    if (on == m_captured) return;
    m_captured = on;
    if (m_app) SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

bool SandboxModule::escWouldCancel() const {
    if (m_tool == Tool::Place || graphEditorOpen() || typingInUi()) return true;
    return m_mode == Mode::Build && (!m_selection.empty() || m_tool == Tool::Shoot);
}

void SandboxModule::setView(View view) {
    if (view == View::Walk) {
        kke::RigidWorld* w = rigidWorld();
        if (!w) {
            m_status = "Walking needs Jolt physics (KKE_ENABLE_JOLT)";
            return;
        }
        if (m_tool == Tool::Place) cancelPlacing();
        if (!m_walker.active()) {
            // Where the camera was looking, facing the way it looked.
            const kke::Camera& cam = m_app->camera();
            const glm::vec3 f = flat(cam.target - cam.position);
            const glm::vec3 feet(cam.target.x, 0.05f, cam.target.z);
            m_walker.begin(*m_app, *w, feet, glm::degrees(std::atan2(-f.x, -f.z)));
            if (!m_walkCharacter.empty() && !m_walker.useCharacter(&m_catalog, m_walkCharacter)) m_walkCharacter.clear();
        }
        m_view = View::Walk;
        setCaptured(true);
        m_swallowFire = true; // the click that started walking isn't a shot
        return;
    }
    if (m_walker.active()) {
        const glm::vec3 feet = m_walker.feet();
        m_walker.end();
        if (auto* camera = m_app->getModule<kke::OrbitCameraModule>()) camera->setTarget(feet + glm::vec3(0.0f, 0.8f, 0.0f));
    }
    if (m_toolFx && m_effects) m_effects->stop(m_toolFx);
    m_toolFx = 0;
    for (ToolModel& t : m_toolModels)
        if (t.instance) m_models->setVisible(t.instance, false);
    m_view = View::Fly;
    setCaptured(false);
}

// Paused: the menu needs the mouse. It's taken back when the game goes on.
void SandboxModule::frameStart(const kke::UpdateContext& ctx) {
    updateReplay(ctx.dt); // here, not in update(): a replay also drives the pause menu
    const bool menu = m_shell && m_shell->menuOpen();
    if (menu && m_captured) {
        setCaptured(false);
        m_recapture = true;
    } else if (!menu && m_recapture) {
        m_recapture = false;
        if (m_view == View::Walk && m_mode == Mode::Play) {
            setCaptured(true);
            m_swallowFire = true;
        }
    }
}

void SandboxModule::dropTool(Tool tool, const glm::vec3& at) {
    Pickup p{ tool, at, 0 };
    if (tool == Tool::Bat) {
        loadBat();
        if (m_batModel) {
            p.instance = m_models->spawn(m_batModel);
            m_models->setTransform(p.instance, glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, 0.05f, 0.0f)) *
                                                   rotationBetween(m_batAxis.axis, glm::vec3(1.0f, 0.0f, 0.0f)) *
                                                   glm::translate(glm::mat4(1.0f), -(m_batAxis.handle + m_batAxis.axis * (m_batAxis.length * 0.5f))));
        }
    } else if (ToolModel* tm = toolModel(tool)) {
        p.instance = m_models->spawn(tm->model);
        m_models->setOverlayEnabled(p.instance, false);
        if (tool == Tool::Melt) m_models->setTint(p.instance, glm::vec3(1.6f, 0.7f, 0.35f));
        m_models->setTransform(p.instance, glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, 0.06f, 0.0f)) *
                                               rotationBetween(tm->axis.axis, glm::vec3(1.0f, 0.0f, 0.0f)) *
                                               glm::translate(glm::mat4(1.0f), -(tm->axis.handle + tm->axis.axis * (tm->axis.length * 0.5f))));
    }
    m_pickups.push_back(p);
}

bool SandboxModule::pickUpNearby() {
    const glm::vec3 feet = m_walker.feet();
    size_t best = m_pickups.size();
    float nearest = kPickupReach;
    for (size_t i = 0; i < m_pickups.size(); ++i) {
        const float d = glm::length(glm::vec2(m_pickups[i].position.x - feet.x, m_pickups[i].position.z - feet.z));
        if (d < nearest) {
            nearest = d;
            best = i;
        }
    }
    const bool holding = m_tool == Tool::Bat || m_tool == Tool::Gun || m_tool == Tool::Fire || m_tool == Tool::Melt;
    if (best == m_pickups.size()) {
        // Nothing here: put down what's in your hands.
        if (!holding) return false;
        dropTool(m_tool, feet + m_walker.facing() * 0.7f);
        m_tool = Tool::Select;
        return true;
    }
    const Pickup p = m_pickups[best];
    if (p.instance) m_models->remove(p.instance);
    m_pickups.erase(m_pickups.begin() + static_cast<std::ptrdiff_t>(best));
    if (holding) dropTool(m_tool, feet + m_walker.facing() * 0.5f); // swap
    if (m_tool == Tool::Place) cancelPlacing();
    m_tool = p.tool;
    m_walker.face(p.position - feet);
    m_walker.playOnce("PickUp_Table", 1.6f);
    return true;
}

void SandboxModule::useTool(bool pressed, bool held, float dt) {
    switch (m_tool) {
    case Tool::Place:
        if (pressed) commitPlacement(false);
        break;
    case Tool::Shoot:
        if (pressed) throwBall();
        break;
    case Tool::Gun:
        if (held) fireGun();
        break;
    case Tool::Fire:
        useFire(dt, held);
        break;
    case Tool::Melt:
        useMelt(dt, held);
        break;
    case Tool::Bat: {
        if (!pressed || m_swing.active()) break;
        // A swing that passes through what the crosshair is on, at that
        // height: the head, the knees, the top of a crate.
        const glm::vec3 feet = m_walker.feet();
        const Aim aim = aimAt(aimRay(), 40.0f);
        glm::vec3 target = aim.hit ? aim.point : feet + m_walker.facing() * 1.2f + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 toward = flat(target - feet, m_walker.facing());
        float dist = glm::length(glm::vec2(target.x - feet.x, target.z - feet.z));
        if (dist > 2.2f) { // out of reach: a swing at the air that way
            dist = 1.3f;
            target = feet + toward * dist + glm::vec3(0.0f, std::clamp(target.y - feet.y, 0.3f, 1.8f), 0.0f);
        }
        target.y = std::clamp(target.y, feet.y + 0.15f, feet.y + 2.0f);
        kke::SwingSettings st;
        st.reach = std::clamp(dist / 0.8f, 0.9f, 1.9f);
        st.innerReach = 0.35f;
        m_swing = kke::BatSwing(st);
        m_swingThing = aim.hit && glm::length(aim.point - target) < 0.05f ? aim.thing : 0;
        m_swingPoint = target;
        const glm::vec3 pivot(feet.x, target.y, feet.z);
        m_walker.face(toward);
        if (m_swing.start(pivot, toward)) {
            m_swingHits.clear();
            loadBat();
            animalNoise(target, 12.0f);
        }
        break;
    }
    default:
        break;
    }
}

void SandboxModule::drawHeldTool() {
    const bool walking = m_view == View::Walk && m_walker.active() && m_mode == Mode::Play;
    for (int i = 0; i < 3; ++i) {
        const Tool tool = i == 0 ? Tool::Gun : i == 1 ? Tool::Fire : Tool::Melt;
        ToolModel& tm = m_toolModels[i];
        const bool show = walking && m_tool == tool;
        if (!show) {
            if (tm.instance) m_models->setVisible(tm.instance, false);
            continue;
        }
        glm::mat4 model(1.0f);
        glm::vec3 tip, dir;
        if (!heldToolPose(tool, model, tip, dir)) continue;
        ToolModel* loaded = toolModel(tool);
        const glm::vec3 base = tool == Tool::Fire ? tip - glm::normalize(dir + glm::vec3(0.0f, 0.8f, 0.0f)) * 0.5f : tip - dir * 0.6f;
        if (loaded) {
            m_models->setTransform(loaded->instance, model);
            m_models->setVisible(loaded->instance, !m_walker.firstPerson());
        } else {
            m_debug->line(base, tip, tool == Tool::Gun ? glm::vec3(0.2f) : glm::vec3(0.9f, 0.4f, 0.1f), 0.04f);
        }
        // Hands on it: the right on the grip, the left further along.
        if (tool == Tool::Fire) {
            m_walker.hand(Side::Right, base + (tip - base) * 0.1f);
        } else {
            m_walker.hand(Side::Right, base + (tip - base) * 0.25f);
            m_walker.hand(Side::Left, base + (tip - base) * 0.65f);
        }
    }
    // The bat: in the right hand while not swinging (the swing draws it).
    if (m_bat && m_batAxis.length > 0.0f && !m_swing.active()) {
        glm::mat4 palm;
        const bool show = walking && m_tool == Tool::Bat && !m_walker.firstPerson() && m_walker.palm(Side::Right, palm);
        if (show)
            m_models->setTransform(m_bat, palm * rotationBetween(m_batAxis.axis, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                              glm::translate(glm::mat4(1.0f), -(m_batAxis.handle + m_batAxis.axis * 0.08f)));
        m_models->setVisible(m_bat, show);
    }
}

void SandboxModule::updateWalker(float dt) {
    if (m_view != View::Walk || !m_walker.active() || !m_input) return;
    kke::InputMap& in = m_input->map(0);
    const bool typing = typingInUi();
    Walker::Controls c;
    if (!typing) {
        c.move = in.axis2("move");
        if (m_captured) c.look = in.axis2("look") * kMouseDegreesPerPixel;
        const glm::vec2 rate = in.axis2("look.rate");
        c.look += glm::vec2(rate.x, rate.y * 0.7f) * (kStickDegreesPerSecond * dt);
        c.jump = in.pressed("jump");
        c.sprint = in.held("sprint");
        c.walk = in.held("walk");
        c.crouch = in.held("crouch");
        c.toggleView = in.pressed("camera.toggle");
        c.zoom = in.axis("camera.zoom") * 0.5f;
        if (in.pressed("sandbox.cursor")) setCaptured(!m_captured);
        if (in.pressed("sandbox.getup")) standEveryoneUp();
        if (in.pressed("interact")) pickUpNearby();
        const int cells = static_cast<int>(m_cellIds.size());
        if (cells > 0) {
            if (in.pressed("sandbox.prev")) m_walkCell = (m_walkCell + cells - 1) % cells;
            if (in.pressed("sandbox.next")) m_walkCell = (m_walkCell + 1) % cells;
            m_walkCell = std::clamp(m_walkCell, 0, cells - 1);
            if (in.pressed("sandbox.use")) palettePressed(m_cellIds[static_cast<size_t>(m_walkCell)]);
        }
    }
    // The tool: the mouse only while it turns the view (else it's on the
    // palette), a controller's trigger any time.
    const bool fireHeld = !typing && in.held("fire") && (m_captured || !mouseOverUi());
    if (!fireHeld) m_swallowFire = false;
    const bool use = fireHeld && !m_swallowFire;
    useTool(use && in.pressed("fire"), use, dt);
    if (!use && m_toolFx) useFire(dt, false);

    drawHeldTool();
    m_walker.update(dt, m_app->fixedAlpha(), c, m_app->camera());

    // Tools lying about: a square around each, and what E would pick up.
    for (const Pickup& p : m_pickups) {
        m_debug->box(p.position - glm::vec3(0.35f, 0.0f, 0.35f), p.position + glm::vec3(0.35f, 0.03f, 0.35f), glm::vec3(1.0f, 0.85f, 0.25f), 0.02f);
        if (!p.instance) m_debug->cross(p.position + glm::vec3(0.0f, 0.15f, 0.0f), 0.2f, glm::vec3(1.0f, 0.5f, 0.1f));
    }
}

void SandboxModule::toysUi() {
    if (m_mode != Mode::Play || m_view != View::Walk || (m_shell && m_shell->menuOpen())) return;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const ImVec2 c(size.x * 0.5f, size.y * 0.5f);
    const float s = ImGui::GetFontSize() / 13.0f;
    if (m_captured) {
        fg->AddCircle(c, 5.0f * s, IM_COL32(20, 20, 30, 200), 0, 4.0f * s);
        fg->AddCircle(c, 5.0f * s, IM_COL32(255, 255, 255, 230), 0, 2.0f * s);
    }
    // Next to a tool lying on the ground: what E does (the nearest, as pickUpNearby).
    const glm::vec3 feet = m_walker.feet();
    const Pickup* nearest = nullptr;
    float best = kPickupReach;
    for (const Pickup& p : m_pickups) {
        const float d = glm::length(glm::vec2(p.position.x - feet.x, p.position.z - feet.z));
        if (d < best) {
            best = d;
            nearest = &p;
        }
    }
    if (nearest) {
        const Pickup& p = *nearest;
        const char* what = p.tool == Tool::Bat ? "the bat" : p.tool == Tool::Gun ? "the gun" : p.tool == Tool::Fire ? "the torch" : "the heat gun";
        const std::string text = std::string("E / Y: pick up ") + what;
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        const ImVec2 at(c.x - ts.x * 0.5f, c.y + 40.0f * s);
        fg->AddRectFilled(ImVec2(at.x - 8.0f * s, at.y - 4.0f * s), ImVec2(at.x + ts.x + 8.0f * s, at.y + ts.y + 4.0f * s), IM_COL32(20, 20, 30, 190),
                          6.0f * s);
        fg->AddText(at, IM_COL32(255, 235, 150, 255), text.c_str());
    }
}

} // namespace kke_sandbox
