#include "FarmModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/ModelAsset.h"
#include "kke/ai/Clips.h"
#include "kke/modules/InputModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <typeindex>

namespace farm {

namespace fs = std::filesystem;
using kke::ai::AgentId;

namespace {

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

float flatLen(const glm::vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

// Every triangle of a box, for the navmesh.
void addBox(std::vector<glm::vec3>& v, std::vector<uint32_t>& idx, const glm::mat4& t, glm::vec3 mn, glm::vec3 mx) {
    const uint32_t b = uint32_t(v.size());
    const glm::vec3 p[8] = { { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
                             { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
    for (const glm::vec3& q : p) v.push_back(glm::vec3(t * glm::vec4(q, 1.0f)));
    const uint32_t f[12][3] = { { 4, 7, 6 }, { 4, 6, 5 }, { 0, 1, 5 }, { 0, 5, 4 }, { 1, 2, 6 }, { 1, 6, 5 },
                                { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 }, { 0, 2, 1 }, { 0, 3, 2 } };
    for (const auto& tri : f) idx.insert(idx.end(), { b + tri[0], b + tri[1], b + tri[2] });
}

// A clip from an animation-only file for the same skeleton, matched by
// bone name (bones the file doesn't move stay at rest).
bool appendClip(kke::ModelData& target, const std::string& file, const std::string& name) {
    kke::ModelLoadOptions o;
    o.allowNoMeshes = true;
    kke::ModelData src;
    try {
        src = kke::loadModel(file, o);
    } catch (const std::exception&) {
        return false;
    }
    if (src.animations.empty()) return false;
    const kke::ModelAnimation& a = src.animations.front();
    std::vector<int> from(target.bones.size(), -1);
    for (size_t i = 0; i < target.bones.size(); ++i) from[i] = src.findBone(target.bones[i].name);
    kke::ModelAnimation out;
    out.name = name;
    out.duration = a.duration;
    out.sampleRate = a.sampleRate;
    out.frames.reserve(a.frames.size());
    for (const std::vector<glm::mat4>& f : a.frames) {
        std::vector<glm::mat4> frame(target.bones.size());
        for (size_t i = 0; i < target.bones.size(); ++i)
            frame[i] = from[i] >= 0 && size_t(from[i]) < f.size() ? f[size_t(from[i])] : target.bones[i].localRest;
        out.frames.push_back(std::move(frame));
    }
    target.animations.push_back(std::move(out));
    return true;
}

} // namespace

std::vector<kke::ModuleDependency> FarmModule::dependencies() const {
    return { { std::type_index(typeid(kke::ModelModule)), true, "draws the farm and the animals" },
             { std::type_index(typeid(kke::InputModule)), true, "moving the dog, rebindable" } };
}

void FarmModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_input = app.getModule<kke::InputModule>();
    m_autopilot = envOn("KKE_FARM_AUTOPILOT");
    m_showDebug = envOn("KKE_FARM_DEBUG");
    m_lineup = envOn("KKE_FARM_LINEUP");
    m_showNav = envOn("KKE_FARM_NAV");

    // Controls: the usual character actions; Space barks.
    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in);
    in.clearBindings("jump");
    using IM = kke::InputModule;
    in.defineAction({ "bark", "Bark", "Dog", "game" });
    in.defineAction({ "farm.debug", "Show what the animals think", "Farm", "game" });
    in.defineAction({ "farm.nav", "Show where they can walk", "Farm", "game" });
    in.addBinding(IM::bind("bark", IM::key(SDL_SCANCODE_SPACE)));
    in.addBinding(IM::bind("bark", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    in.addBinding(IM::bind("farm.debug", IM::key(SDL_SCANCODE_F1)));
    in.addBinding(IM::bind("farm.debug", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
    in.addBinding(IM::bind("farm.nav", IM::key(SDL_SCANCODE_F2)));
    // Teaching by example: pick a lesson, show it to the nearest animal
    // ("interact": E), then let them learn.
    in.defineAction({ "farm.lesson", "Next lesson", "Farm", "game" });
    in.defineAction({ "farm.learn", "Let them learn", "Farm", "game" });
    in.addBinding(IM::bind("farm.lesson", IM::key(SDL_SCANCODE_TAB)));
    in.addBinding(IM::bind("farm.lesson", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
    in.addBinding(IM::bind("farm.learn", IM::key(SDL_SCANCODE_L)));
    in.addBinding(IM::bind("farm.learn", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
    m_input->commitDefaults();

    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.settings.pivotHeight = 0.9f;
    m_rig.settings.armLength = 5.5f;
    m_rig.settings.shoulderOffset = 0.0f;
    m_rig.pitch = -14.0f;

    if (!loadLevel()) return;
    buildNavMesh();
    setupAi();
}

bool FarmModule::loadLevel() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    const std::string exeDir = base ? base : "";
    const std::string scenePath = (fs::path(exeDir) / "scenes" / "farm.scene.json").string();
    try {
        m_scene = kke::SceneFile::load(scenePath);
    } catch (const std::exception& e) {
        m_status = std::string("Couldn't read the farm: ") + e.what();
        log->error("{}", m_status);
        return false;
    }
    std::string moodError;
    if (!m_scene.mood.empty() && !m_app->setMood(m_scene.mood, &moodError)) log->warn("{}", moodError);
    kke::Lighting& light = m_app->lighting();
    if (m_scene.hasSun) {
        light.lights[0].enabled = true;
        light.lights[0].isDirectional = true;
        light.lights[0].direction = m_scene.sunDirection;
        light.lights[0].color = m_scene.sunColor;
        light.lights[0].intensity = m_scene.sunIntensity;
    }
    if (m_scene.hasAmbient) light.ambientColor = m_scene.ambient;

    // The grass under everything.
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    {
        const glm::vec2 h = m_scene.groundSize * 0.5f;
        const glm::vec3 c = m_scene.groundColor;
        const glm::vec3 n(0, 1, 0);
        std::vector<kke::Vertex> v{ { { -h.x, 0, -h.y }, c, n, glm::vec2(0) },
                                    { { -h.x, 0, h.y }, c, n, glm::vec2(0) },
                                    { { h.x, 0, h.y }, c, n, glm::vec2(0) },
                                    { { h.x, 0, -h.y }, c, n, glm::vec2(0) } };
        m_ground->upload(v, { 0, 1, 2, 0, 2, 3 });
    }

    std::vector<std::string> searched;
    m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, exeDir, &searched);
    if (!m_packDir.empty()) m_catalog = kke::AssetCatalog::scan(m_packDir);
    if (m_packDir.empty() || !m_catalog.find("SM_Bld_Barn_01")) {
        m_status = "POLYGON Farm isn't installed: put the extracted pack in assets/synty/ or set KKE_ASSETS_DIR (see README.md). "
                   "The animals still run on the bare field.";
        log->info("{}", m_status); // optional pack; the HUD says so too
    } else {
        m_loaded = kke::loadScene(m_scene, m_catalog, *m_models, nullptr);
        for (const std::string& m : m_loaded.missing) log->warn("farm: asset '{}' not found", m);
    }
    m_pos = m_scene.spawn;
    m_yaw = m_scene.spawnYaw;
    m_rig.yaw = 180.0f - m_yaw; // behind the dog, looking the way it faces (rig yaw 0 looks down -Z)
    return true;
}

void FarmModule::buildNavMesh() {
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    // The ground, then everything the scene marks as solid, in the order
    // loadScene placed it.
    const glm::vec2 h = m_scene.groundSize * 0.5f;
    v.insert(v.end(), { { -h.x, 0, -h.y }, { -h.x, 0, h.y }, { h.x, 0, h.y }, { h.x, 0, -h.y } });
    idx.insert(idx.end(), { 0, 1, 2, 0, 2, 3 });
    size_t next = 0;
    std::map<std::string, bool> found;
    for (const kke::SceneObject& o : m_scene.objects) {
        const std::string key = o.pack + "/" + o.asset;
        if (!found.count(key)) found[key] = m_catalog.find(o.asset, m_scene.packs) != nullptr;
        if (!found[key] || m_loaded.instances.empty()) continue;
        for (int cell = 0; cell < o.gridCount.x * o.gridCount.y; ++cell) {
            if (next >= m_loaded.instances.size()) break;
            const kke::ModelModule::InstanceId inst = m_loaded.instances[next++];
            if (o.collision == kke::SceneObject::Collision::None) continue;
            const kke::ModelData* d = m_models->model(m_models->instanceModel(inst));
            if (!d) continue;
            const glm::mat4 t = m_models->transform(inst);
            if (o.collision == kke::SceneObject::Collision::Box) {
                // Trees: the trunk is what's in the way, not the canopy.
                const glm::vec3 c = (d->boundsMin + d->boundsMax) * 0.5f;
                const glm::vec3 half = (d->boundsMax - d->boundsMin) * glm::vec3(0.18f, 0.5f, 0.18f);
                addBox(v, idx, t, { c.x - half.x, d->boundsMin.y, c.z - half.z }, { c.x + half.x, d->boundsMax.y, c.z + half.z });
                continue;
            }
            for (const kke::ModelMesh& m : d->meshes) {
                if (m.skinned) continue;
                const uint32_t b = uint32_t(v.size());
                for (const kke::ModelVertex& mv : m.vertices) v.push_back(glm::vec3(t * glm::vec4(mv.position, 1.0f)));
                for (uint32_t i : m.indices) idx.push_back(b + i);
            }
        }
    }
    kke::ai::NavMeshSettings s;
    s.cellSize = 0.25f;
    s.agentRadius = 0.35f;
    s.agentHeight = 1.0f;
    s.agentMaxClimb = 0.3f;
    std::string error;
    const auto t0 = SDL_GetTicksNS();
    if (m_nav.build(v, idx, s, &error)) {
        m_navStatus = std::to_string(m_nav.polygonCount()) + " polygons from " + std::to_string(idx.size() / 3) + " triangles in " +
                      std::to_string((SDL_GetTicksNS() - t0) / 1000000) + " ms";
        kke::log::get(name())->info("navmesh: {}", m_navStatus);
    } else {
        m_navStatus = "navmesh failed: " + error;
        kke::log::get(name())->warn("{}", m_navStatus);
    }
}

kke::ModelModule::ModelId FarmModule::loadDog(const std::string& breed) {
    const kke::CatalogAsset* asset = m_catalog.find("Unity_SK_Animals_Dog_01");
    if (!asset) return 0;
    kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *asset);
    opts.loadAnimations = true;
    kke::ModelData d;
    try {
        d = kke::loadModel(asset->path, opts);
    } catch (const std::exception& e) {
        kke::log::get(name())->warn("dog: {}", e.what());
        return 0;
    }
    // The file holds every breed on one skeleton: keep this one's parts.
    std::vector<kke::ModelMesh> keep;
    for (kke::ModelMesh& m : d.meshes) {
        const std::string& mat = m.material < d.materials.size() ? d.materials[m.material].name : std::string();
        if (mat.rfind(breed + "_", 0) == 0) keep.push_back(std::move(m));
    }
    if (keep.empty()) return 0;
    d.meshes = std::move(keep);
    d.animations.clear();
    const fs::path anims = fs::path(asset->path).parent_path() / "Animations";
    const std::pair<const char*, const char*> clips[] = {
        { "idle", "Locomotion/_POLYGON_Dog_Locomotion_Standing.fbx" },
        { "walk", "Locomotion/_POLYGON_Dog_Locomotion_Walking.fbx" },
        { "run", "Locomotion/_POLYGON_Dog_Locomotion_Running.fbx" },
        { "eat", "Actions_Standing/_POLYGON_Dog_Action_Standing_Eat.fbx" },
        { "drink", "Actions_Standing/_POLYGON_Dog_Action_Standing_Drink.fbx" },
        { "sniff", "Actions_Standing/_POLYGON_Dog_Action_Standing_Sniff.fbx" },
        { "bark", "Actions_Standing/_POLYGON_Dog_Action_Standing_Bark.fbx" },
        { "alert", "Actions_Standing/_POLYGON_Dog_Action_Standing_TailWag.fbx" },
        { "attack", "Attack/_POLYGON_Dog_Attack_Bite.fbx" },
        { "rest", "Sleep/_POLYGON_Dog_Sleep_Idle.fbx" },
    };
    for (const auto& [anim, file] : clips)
        if (!appendClip(d, (anims / file).string(), anim)) kke::log::get(name())->warn("dog clip '{}' missing ({})", anim, file);
    return m_models->add(std::move(d), "farm-dog:" + breed);
}

bool FarmModule::makeLook(const std::string& species, Look& look) {
    struct Spec { const char* species; const char* asset; float scale; };
    // Quaternius' Farm Animals (CC0) for the livestock, POLYGON Dogs for
    // the dog and the fox.
    static const Spec specs[] = {
        { "sheep", "Sheep", 0.23f }, { "cow", "Cow", 0.3f }, { "pig", "Pig", 0.2f }, { "horse", "Horse", 0.28f },
    };
    if (species == "dog" || species == "fox") {
        look.model = loadDog(species == "dog" ? "GermanShepherd" : "Fox");
        look.scale = species == "fox" ? 0.8f : 1.0f;
        return look.model != 0;
    }
    for (const Spec& s : specs) {
        if (species != s.species) continue;
        const kke::CatalogAsset* asset = m_catalog.find(s.asset);
        if (!asset) return false;
        kke::ModelLoadOptions opts = kke::packLoadOptions(m_catalog, *asset);
        opts.loadAnimations = true;
        look.model = m_models->load(asset->path, opts);
        if (!look.model) return false;
        look.scale = s.scale; // Quaternius' animals are made a few metres tall
        return true;
    }
    return false;
}

void FarmModule::spawnAnimal(const std::string& species, const glm::vec3& at, float yaw) {
    const AgentId id = m_nextId++;
    glm::vec3 p = at;
    if (m_nav.valid()) m_nav.nearestPoint(at, p);
    if (!m_ai.addAgent(id, species, p, yaw)) return;
    Animal a;
    a.id = id;
    a.species = species;
    auto it = m_looks.find(species);
    if (it == m_looks.end()) {
        Look look;
        if (!makeLook(species, look)) look.model = 0;
        it = m_looks.emplace(species, look).first;
    }
    if (it->second.model) a.instance = m_models->spawn(it->second.model);
    m_animals.push_back(a);
}

void FarmModule::setupAi() {
    m_ai.setNavMesh(m_nav.valid() ? &m_nav : nullptr);
    // Farm tweaks on the built-in species: animals stay in their fields.
    auto tweak = [&](const char* id, float homeRadius) {
        kke::ai::Species s = *m_ai.species(id);
        s.homeRadius = homeRadius;
        s.actions.clear();
        m_ai.defineSpecies(s);
    };
    tweak("sheep", 11.0f);
    tweak("cow", 13.0f);
    tweak("pig", 3.5f);
    tweak("horse", 14.0f);
    tweak("fox", 18.0f);

    m_ai.addActor(kPlayer, "dog", m_pos);
    // Water in the troughs, grain at the hay pile.
    for (const kke::SceneObject& o : m_scene.objects) {
        if (o.asset == "SM_Prop_Trough_01") m_ai.addPlace("water", o.position, 2.2f);
        if (o.asset == "SM_Prop_Hay_Pile_01") m_ai.addPlace("grain", o.position, 2.0f);
    }

    uint32_t seed = 7;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.0f;
    };
    auto herd = [&](const char* species, int n, glm::vec3 centre, float spread) {
        for (int i = 0; i < n; ++i)
            spawnAnimal(species, centre + glm::vec3((rnd() - 0.5f) * spread, 0.0f, (rnd() - 0.5f) * spread), rnd() * 360.0f);
    };
    herd("sheep", 14, { -25, 0, 15 }, 10.0f);
    herd("cow", 5, { 27, 0, 18 }, 12.0f);
    herd("pig", 4, { 0, 0, -27 }, 5.0f);
    herd("horse", 3, { 0, 0, 45 }, 10.0f);
    herd("fox", 1, { -50, 0, 48 }, 2.0f);

    // The player's dog.
    if (makeLook("dog", m_dogLook)) m_dog = m_models->spawn(m_dogLook.model);
    kke::log::get(name())->info("farm: {} animals", m_animals.size());
}

void FarmModule::showAnim(kke::ModelModule::InstanceId instance, const Look& look, const std::string& anim, std::string& playing) {
    const kke::ModelData* d = instance ? m_models->model(look.model) : nullptr;
    if (!d || anim == playing) return;
    playing = anim;
    const kke::ai::ClipChoice c = kke::ai::clipForAnim(*d, anim);
    m_models->playAnimation(instance, c.clip, anim != "attack" && anim != "bark", c.speed);
}

void FarmModule::updatePlayer(float dt) {
    kke::InputMap& in = m_input->map(0);
    glm::vec2 move = in.axis2("move");
    bool fast = in.held("sprint");
    bool bark = in.pressed("bark");
    if (m_autopilot) {
        // Trot to the meadow gate, sprint into the flock, circle, bark.
        static const glm::vec3 route[] = { { -25, 0, -4 }, { -25, 0, 6 }, { -24, 0, 18 }, { -16, 0, 22 }, { -30, 0, 10 }, { -25, 0, -4 }, { 0, 0, 8 } };
        const int leg = std::min(int(m_time / 6.0f), 6);
        glm::vec3 to = route[leg] - m_pos;
        to.y = 0.0f;
        glm::vec3 dir = flatLen(to) > 0.5f ? to / flatLen(to) : glm::vec3(0.0f);
        const glm::vec3 fwd = m_rig.forward(), right = m_rig.right();
        move = glm::vec2(glm::dot(dir, right), glm::dot(dir, fwd));
        fast = leg >= 1 && leg <= 4;
        bark = std::fmod(m_time, 6.0f) < dt && leg == 2;
    }
    glm::vec3 want = m_rig.forward() * move.y + m_rig.right() * move.x;
    want.y = 0.0f;
    const float amount = std::min(1.0f, glm::length(move));
    if (flatLen(want) > 1e-3f) want = want / flatLen(want) * amount * (fast ? 7.0f : 3.2f);
    else want = glm::vec3(0.0f);
    m_vel += (want - m_vel) * (1.0f - std::exp(-8.0f * dt));
    glm::vec3 next = m_pos + m_vel * dt;
    if (m_nav.valid()) next = m_nav.moveAlongSurface(m_pos, next);
    else next.y = 0.0f;
    m_vel = (next - m_pos) / std::max(dt, 1e-4f);
    m_vel.y = 0.0f;
    m_pos = next;
    if (flatLen(m_vel) > 0.3f) {
        const float target = glm::degrees(std::atan2(m_vel.x, m_vel.z));
        const float diff = std::remainder(target - m_yaw, 360.0f);
        m_yaw += std::clamp(diff, -540.0f * dt, 540.0f * dt);
    }

    m_barkTimer = std::max(0.0f, m_barkTimer - dt);
    if (bark && m_barkTimer <= 0.0f) {
        m_ai.makeNoise({ m_pos, 25.0f, kPlayer, 0 });
        m_barkTimer = 0.9f;
        m_log.push_back("Woof!");
    }
    m_ai.setTransform(kPlayer, m_pos, m_vel, m_yaw);

    if (m_dog) {
        const glm::mat4 t = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_pos), glm::radians(m_yaw + m_dogLook.yawOffset),
                                                   glm::vec3(0, 1, 0)),
                                       glm::vec3(m_dogLook.scale));
        m_models->setTransform(m_dog, t);
        const float speed = flatLen(m_vel);
        const char* anim = m_barkTimer > 0.4f ? "bark" : speed > 4.5f ? "run" : speed > 0.3f ? "walk" : "idle";
        showAnim(m_dog, m_dogLook, anim, m_dogPlaying);
    }
}

void FarmModule::updateCamera(float dt) {
    kke::InputMap& in = m_input->map(0);
    glm::vec2 look(0.0f);
    if (m_captured) look += in.axis2("look") * m_mouseSensitivity;
    const glm::vec2 rate = in.axis2("look.rate");
    look += glm::vec2(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
    m_rig.addLook(look.x, look.y);
    if (m_autopilot) {
        // Swing round behind the dog.
        const float behind = 180.0f - m_yaw; // rig yaw 0 looks down -Z, dog yaw 0 faces +Z
        m_rig.yaw += std::remainder(behind - m_rig.yaw, 360.0f) * (1.0f - std::exp(-1.5f * dt));
    }
    m_rig.update(dt, m_pos, [](const glm::vec3&, const glm::vec3&, float maxDistance) { return maxDistance; }, m_app->camera());
}

void FarmModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.1f);
    m_time += dt;
    if (m_lineup) {
        // One of each kind in a row ahead of the dog, all facing +X (screen
        // right): a quick check that every model's facing matches its yaw.
        std::map<std::string, int> seen;
        int slot = 0;
        for (const Animal& a : m_animals) {
            if (seen.count(a.species)) continue;
            seen[a.species] = slot;
            const glm::vec3 at = m_scene.spawn + glm::vec3(-7.5f + 3.0f * float(slot++), 0.0f, -9.0f);
            m_ai.setEnabled(a.id, false);
            m_ai.setTransform(a.id, at, glm::vec3(0.0f), 90.0f);
        }
    }
    kke::InputMap& in = m_input->map(0);
    if (in.pressed("farm.debug")) m_showDebug = !m_showDebug;
    if (in.pressed("farm.nav")) m_showNav = !m_showNav;
    if (in.pressed("farm.lesson")) m_lesson = (m_lesson + 1) % kLessons.size();
    if (in.pressed("interact")) teachNearest();
    if (in.pressed("farm.learn")) learnAll();

    updatePlayer(dt);
    if (!m_lineup) m_ai.update(dt);
    for (const kke::ai::AiEvent& e : m_ai.takeEvents()) {
        const kke::ai::Species* s = m_ai.speciesOf(e.who);
        const std::string who = s ? s->label : "?";
        if (e.kind == kke::ai::AiEvent::Kind::Scared && e.other == kPlayer) m_log.push_back(who + " runs from you");
        else if (e.kind == kke::ai::AiEvent::Kind::Calmed) m_log.push_back(who + " calms down");
        else if (e.kind == kke::ai::AiEvent::Kind::ActionChanged && e.action == "investigate" && e.other == kPlayer)
            m_log.push_back(who + " comes to see you");
        else if (e.kind == kke::ai::AiEvent::Kind::Attack) m_log.push_back(who + " attacks!");
    }
    if (m_log.size() > 6) m_log.erase(m_log.begin(), m_log.end() - 6);

    for (Animal& a : m_animals) {
        const kke::ai::Agent* ag = m_ai.agent(a.id);
        if (!ag || !a.instance) continue;
        const Look& look = m_looks[a.species];
        const glm::mat4 t = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), ag->position), glm::radians(ag->yaw + look.yawOffset),
                                                   glm::vec3(0, 1, 0)),
                                       glm::vec3(look.scale));
        m_models->setTransform(a.instance, t);
        showAnim(a.instance, look, ag->anim, a.playing);
    }
    updateCamera(dt);
}

void FarmModule::render(const kke::RenderContext& ctx) {
    if (m_ground) m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    if (m_showNav && m_nav.valid()) {
        if (!m_navMesh) {
            m_navMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
            std::vector<glm::vec3> tris;
            m_nav.debugTriangles(tris, 0.06f);
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            for (size_t i = 0; i < tris.size(); ++i) {
                v.push_back({ tris[i], glm::vec3(0.2f, 0.55f, 0.95f), glm::vec3(0, 1, 0), glm::vec2(0) });
                idx.push_back(uint32_t(i));
            }
            m_navMesh->upload(v, idx);
        }
        m_navMesh->draw(ctx);
    }
}

void FarmModule::renderShadow(const kke::ShadowRenderContext&) {}

// Shows the nearest animal the lesson: "in a moment like this, do that".
void FarmModule::teachNearest() {
    const kke::ai::Agent* best = nullptr;
    float bestDist = 10.0f;
    for (const Animal& a : m_animals) {
        const kke::ai::Agent* ag = m_ai.agent(a.id);
        if (!ag) continue;
        const float d = glm::length(glm::vec2(ag->position.x - m_pos.x, ag->position.z - m_pos.z));
        if (d < bestDist) {
            bestDist = d;
            best = ag;
        }
    }
    const std::string lesson = kLessons[m_lesson];
    if (!best) {
        m_log.push_back("Nobody near enough to show (" + lesson + ")");
        return;
    }
    const kke::ai::Species* s = m_ai.speciesOf(best->id);
    const std::string who = s ? s->label : "?";
    if (!s || !m_ai.teach(best->id, lesson)) {
        m_log.push_back(who + " can't " + lesson);
        return;
    }
    const kke::ai::LearnedPolicy* p = m_ai.policy(s->id);
    m_log.push_back("Showed the " + who + ": " + lesson + " (" + std::to_string(p ? p->exampleCount() : 0) + " shown)");
}

// Every kind that was shown something learns from it.
void FarmModule::learnAll() {
    bool any = false;
    for (const std::string& id : m_ai.speciesIds()) {
        const kke::ai::LearnedPolicy* p = m_ai.policy(id);
        if (!p || p->exampleCount() == 0) continue;
        any = true;
        const kke::ai::LearnedPolicy::Result r = m_ai.learn(id);
        const kke::ai::Species* s = m_ai.species(id);
        m_log.push_back((s ? s->label : id) + " learned from " + std::to_string(p->exampleCount()) + " (" +
                        std::to_string(int(std::lround(r.accuracy * 100.0f))) + "% right)");
    }
    if (!any) m_log.push_back("Show them something first (E)");
}

void FarmModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Farm", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("You're the dog. WASD / stick: move, Shift: run, Space: bark");
    ImGui::TextUnformatted("Click to look around (Esc lets go). F1: what they think, F2: where they walk");
    ImGui::Text("Teach: lesson '%s' (Tab), E: show the nearest animal, L: let them learn", kLessons[m_lesson]);
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    for (const std::string& l : m_log) ImGui::BulletText("%s", l.c_str());
    if (m_showDebug) {
        ImGui::Separator();
        ImGui::Text("navmesh: %s", m_navStatus.c_str());
        for (const Animal& a : m_animals) ImGui::TextUnformatted(m_ai.describe(a.id).c_str());
    }
    ImGui::End();
}

void FarmModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !m_app->uiCapturesMouse() && e.button.button == SDL_BUTTON_LEFT) {
        m_captured = true;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), true);
    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && m_captured) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
}

} // namespace farm
