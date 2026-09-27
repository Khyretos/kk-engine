#include "SyntySceneModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"

#include <glm/gtc/matrix_transform.hpp>
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif


#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <typeindex>
#include <algorithm>

namespace kke_demo {

namespace fs = std::filesystem;


std::vector<kke::ModuleDependency> SyntySceneModule::dependencies() const {
    return { { std::type_index(typeid(kke::ModelModule)), true, "loads and draws the Synty FBX models" } };
}

// `relative` is kept for readability ("StaticMeshes/SM_Prop_Crate_01.fbx")
// but only the file name matters: the catalog finds it wherever the pack
// keeps it (StaticMeshes/, FBX/, _SourceFiles/...).
kke::ModelModule::ModelId SyntySceneModule::load(const std::string& relative) {
    std::string name = fs::path(relative).stem().string();
    const kke::CatalogAsset* asset = m_catalog.find(name);
    if (!asset) {
        kke::log::get(this->name())->warn("asset '{}' not found in any pack under '{}'", name, m_packDir);
        return 0;
    }
    const kke::CatalogPack* pack = m_catalog.pack(asset->pack);
    kke::ModelLoadOptions opts;
    opts.textureSearchPaths = pack->textureDirs;
    // Some meshes reference textures from other Synty packs (the trees
    // point at POLYGON Military's atlas); fall back to this pack's own.
    opts.fallbackTexture = pack->defaultTexture;
    return m_models->load(asset->path, opts);
}

void SyntySceneModule::place(const std::string& relative, glm::vec3 position, float yawDegrees, glm::vec3 scale) {
    kke::ModelModule::ModelId id = load(relative);
    if (!id) return;
    // Synty pivots vary: building pieces sit at a corner, props at their
    // center (so a crate would be half in the floor). Place every piece by
    // its bounds instead: centered on `position` in X/Z, bottom at its Y.
    const kke::ModelData* d = m_models->model(id);
    glm::vec3 center = (d->boundsMin + d->boundsMax) * 0.5f;
    glm::vec3 offset(-center.x, -d->boundsMin.y, -center.z);
    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    t = glm::rotate(t, glm::radians(yawDegrees), glm::vec3(0, 1, 0));
    t = glm::scale(t, scale);
    m_models->spawn(id, glm::translate(t, offset));
    ++m_propCount;
}

void SyntySceneModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    // Optional and loosely coupled: any module implementing
    // IRagdollPhysics enables ragdolls; Jolt's (RigidBodyModule) is
    // preferred over FEMFX's when both are there.
    m_physics = kke::bestRagdollPhysics(app.findCapability<kke::IRagdollPhysics>());
    const char* base = SDL_GetBasePath();
    m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &m_searched);
    if (!m_packDir.empty()) m_catalog = kke::AssetCatalog::scan(m_packDir);
    if (m_packDir.empty() || !m_catalog.find("SM_Buildings_Floor_5x5_01")) {
        kke::log::get(name())->info("Synty POLYGON Prototype pack not found. Put the extracted pack folder(s) in assets/synty/ "
                                    "(e.g. assets/synty/POLYGON_Prototype/Characters/...) or set KKE_ASSETS_DIR.");
        m_packDir.clear();
        defineInput();
        buildPanel(); // says what's missing and where it looked
        return;
    }
    kke::log::get(name())->info("asset folder '{}': {} pack(s), {} assets", m_packDir, m_catalog.packs.size(), m_catalog.assets.size());
    // The Prototype look: its world-space measuring grid over the atlas
    // colours (what Synty's own Prototype shader does).
    if (const kke::CatalogPack* proto = m_catalog.pack(m_catalog.find("SM_Buildings_Floor_5x5_01")->pack); proto && !proto->overlayTextures.empty())
        m_models->setWorldOverlay(proto->overlayTextures.front(), 2.0f, 1.0f);

    // --- the level: a 20x20 m floor of 5x5 tiles, walls, stairs, props
    for (int x = -2; x < 2; ++x) {
        for (int z = -2; z < 2; ++z) place("StaticMeshes/SM_Buildings_Floor_5x5_01.fbx", glm::vec3(x * 5.0f + 2.5f, -0.1f, z * 5.0f + 2.5f));
    }
    place("StaticMeshes/SM_Buildings_Wall_5x3_01.fbx", glm::vec3(-7.5f, 0, -10), 0);
    place("StaticMeshes/SM_Buildings_WallDoor_5x3_01.fbx", glm::vec3(-2.5f, 0, -10), 0);
    place("StaticMeshes/SM_Buildings_Wall_5x3_01.fbx", glm::vec3(2.5f, 0, -10), 0);
    place("StaticMeshes/SM_Buildings_Wall_5x3_01.fbx", glm::vec3(7.5f, 0, -10), 0);
    place("StaticMeshes/SM_Buildings_Wall_5x3_01.fbx", glm::vec3(-10, 0, -7.5f), 90);
    place("StaticMeshes/SM_Buildings_Wall_5x3_01.fbx", glm::vec3(-10, 0, -2.5f), 90);
    place("StaticMeshes/SM_Buildings_Stairs_1x3_01.fbx", glm::vec3(6.0f, 0, -7.0f), 0);
    place("StaticMeshes/SM_Buildings_Column_1x3_01.fbx", glm::vec3(-9.0f, 0, 9.0f));
    place("StaticMeshes/SM_Buildings_Column_1x3_01.fbx", glm::vec3(9.0f, 0, 9.0f));
    place("StaticMeshes/SM_Prop_Crate_01.fbx", glm::vec3(-6.0f, 0, -6.5f), 15);
    place("StaticMeshes/SM_Prop_Crate_02.fbx", glm::vec3(-4.8f, 0, -7.2f), -10);
    place("StaticMeshes/SM_Prop_Crate_Question_01.fbx", glm::vec3(-5.4f, 1.0f, -6.9f), 30);
    place("StaticMeshes/SM_Prop_Barrel_01.fbx", glm::vec3(-7.5f, 0, -4.5f));
    place("StaticMeshes/SM_Prop_Barrel_01.fbx", glm::vec3(-7.0f, 0, -3.6f));
    place("StaticMeshes/SM_Prop_Chest_Wood_01.fbx", glm::vec3(3.0f, 0, -8.5f), 180);
    place("StaticMeshes/SM_Prop_Cone_01.fbx", glm::vec3(1.0f, 0, 5.0f));
    place("StaticMeshes/SM_Prop_Cone_01.fbx", glm::vec3(2.0f, 0, 5.0f));
    place("StaticMeshes/SM_Prop_Cone_01.fbx", glm::vec3(3.0f, 0, 5.0f));
    place("StaticMeshes/SM_Prop_Barrier_01.fbx", glm::vec3(-3.0f, 0, 6.0f), 10);
    place("StaticMeshes/SM_Generic_Tree_01.fbx", glm::vec3(8.0f, 0, 3.0f));
    place("StaticMeshes/SM_Generic_Tree_02.fbx", glm::vec3(7.0f, 0, 7.5f), 40);
    place("StaticMeshes/SM_Generic_Tree_03.fbx", glm::vec3(-8.0f, 0, 6.0f), 75);
    place("StaticMeshes/SM_Generic_Small_Rocks_01.fbx", glm::vec3(6.0f, 0, 5.5f));
    place("StaticMeshes/SM_Prop_FlagPole_01.fbx", glm::vec3(0.0f, 0, -8.0f));

    addJoltLevel();

    // --- characters in a row, each doing something different
    struct Spec { const char* file; const char* label; const char* behavior; };
    const Spec specs[] = {
        { "Characters/SK_Character_Dummy_Male_01.fbx", "Dummy (male) - FBX clip", "clip" },
        { "Characters/SK_Character_Dummy_Female_01.fbx", "Dummy (female) - procedural wave", "wave" },
        { "Characters/SK_Character_Male_Face_01.fbx", "Male - idle breathing", "breathe" },
        { "Characters/SK_Character_Female_Face_01.fbx", "Female - hand-posed", "pose" },
    };
    for (int i = 0; i < 4; ++i) {
        Character c;
        c.label = specs[i].label;
        c.behavior = specs[i].behavior;
        c.model = load(specs[i].file);
        if (!c.model) continue;
        c.position = glm::vec3((i - 1.5f) * 1.6f, 0.0f, 0.0f);
        c.instance = m_models->spawn(c.model, glm::translate(glm::mat4(1.0f), c.position));
        if (c.behavior == "clip") m_models->playAnimation(c.instance, 0, true);
        m_characters.push_back(c);
    }
    // Animals, if a Quaternius Farm Animals pack is there too (any
    // Quaternius-named rig works): they ragdoll with animal joint limits.
    struct AnimalSpec { const char* file; const char* label; glm::vec3 at; float yaw, scale, mass; };
    const AnimalSpec animals[] = {
        { "Horse.fbx", "Horse (Quaternius)", { -3.5f, 0.0f, -4.5f }, 70.0f, 0.3f, 500.0f },
        { "Pug.fbx", "Pug (Quaternius)", { 3.2f, 0.0f, -3.0f }, -60.0f, 0.2f, 8.0f },
    };
    for (const AnimalSpec& a : animals) {
        if (!m_catalog.find(fs::path(a.file).stem().string())) continue;
        Character c;
        c.label = a.label;
        c.behavior = "clip";
        c.animal = true;
        c.mass = a.mass;
        c.model = load(a.file);
        if (!c.model) continue;
        c.position = a.at;
        glm::mat4 t = glm::translate(glm::mat4(1.0f), c.position);
        t = glm::rotate(t, glm::radians(a.yaw), glm::vec3(0, 1, 0));
        c.instance = m_models->spawn(c.model, glm::scale(t, glm::vec3(a.scale)));
        // An idle clip if it has one.
        if (const kke::ModelData* d = m_models->model(c.model)) {
            for (size_t i = 0; i < d->animations.size(); ++i)
                if (d->animations[i].name.find("Idle") != std::string::npos) {
                    m_models->playAnimation(c.instance, static_cast<int>(i), true);
                    break;
                }
        }
        m_characters.push_back(c);
    }
    if (m_selected >= static_cast<int>(m_characters.size())) m_selected = 0;
    if (!m_characters.empty()) {
        const kke::ModelData* d = m_models->model(m_characters[m_selected].model);
        m_poseEuler.assign(d ? d->bones.size() : 0, glm::vec3(0.0f));
    }
    kke::log::get(name())->info("scene: {} props, {} characters", m_propCount, m_characters.size());
    defineInput();
    buildPanel();
}

// Rotates a bone relative to its rest pose, in the bone's own local axes.
void SyntySceneModule::rotateBone(Character& c, const char* boneName, glm::vec3 euler) {
    const kke::ModelData* d = m_models->model(c.model);
    std::vector<glm::mat4>* locals = m_models->boneLocals(c.instance);
    if (!d || !locals) return;
    int b = d->findBone(boneName);
    if (b < 0) return;
    glm::mat4 r(1.0f);
    r = glm::rotate(r, glm::radians(euler.x), glm::vec3(1, 0, 0));
    r = glm::rotate(r, glm::radians(euler.y), glm::vec3(0, 1, 0));
    r = glm::rotate(r, glm::radians(euler.z), glm::vec3(0, 0, 1));
    (*locals)[b] = d->bones[b].localRest * r;
}

void SyntySceneModule::update(const kke::UpdateContext& ctx) {
    readInput();
    m_time += ctx.dt;
    m_models->setShowBones(m_showBones);
    std::vector<glm::mat4> bodies;
    for (Character& c : m_characters) {
        if (c.ragdoll && m_physics && m_physics->ragdollBodyTransforms(c.ragdoll, bodies)) {
            const kke::ModelData* d = m_models->model(c.model);
            glm::mat4 worldToModel = glm::inverse(m_models->transform(c.instance));
            m_models->setBoneWorldOverride(c.instance, kke::poseFromRagdoll(*d, c.binding, bodies, worldToModel));
            continue;
        }
        float t = m_time;
        if (c.behavior == "wave") {
            // Arm up and out, forearm swinging. Axes were found by trying
            // them on this rig (Synty bones' local frames aren't aligned
            // with the world), which is exactly what the Pose panel is for.
            // Left/right arm bones are mirrored in this rig: +Z raises the
            // right arm but lowers the left.
            rotateBone(c, "UpperArm_R", glm::vec3(0, 0, 70));
            rotateBone(c, "lowerarm_r", glm::vec3(0, 0, 40 + 30 * std::sin(t * 6.0f)));
            rotateBone(c, "head", glm::vec3(0, 12 * std::sin(t * 1.3f), 0));
        } else if (c.behavior == "breathe") {
            float s = std::sin(t * 1.8f);
            rotateBone(c, "spine_02", glm::vec3(0, 0, 2.5f * s));
            rotateBone(c, "spine_03", glm::vec3(0, 0, 2.0f * s));
            rotateBone(c, "head", glm::vec3(0, 20 * std::sin(t * 0.5f), 4 * s));
            rotateBone(c, "UpperArm_L", glm::vec3(0, 0, -65));
            rotateBone(c, "UpperArm_R", glm::vec3(0, 0, -65));
        }
        blendToAnimation(c, ctx.dt);
    }
}

namespace {
constexpr float kStandUpSeconds = 0.6f;
}

// The pose the character's locals (clip, procedural, hand-posed) give
// right now, blended in from the ragdoll pose it stood up from.
void SyntySceneModule::blendToAnimation(Character& c, float dt) {
    if (c.blendAge < 0.0f) return;
    const kke::ModelData* d = m_models->model(c.model);
    const std::vector<glm::mat4>* locals = m_models->boneLocals(c.instance);
    if (!d || !locals || locals->size() != d->bones.size()) {
        c.blendAge = -1.0f;
        m_models->setBoneWorldOverride(c.instance, {});
        return;
    }
    c.blendAge += dt;
    const float w = kke::blendWeight(c.blendAge, kStandUpSeconds);
    if (w >= 1.0f) {
        c.blendAge = -1.0f;
        c.blendFrom.clear();
        m_models->setBoneWorldOverride(c.instance, {}); // the animation drives it again
        return;
    }
    std::vector<glm::mat4> animated(d->bones.size());
    for (size_t b = 0; b < animated.size(); ++b) {
        const int p = d->bones[b].parent;
        animated[b] = p >= 0 ? animated[p] * (*locals)[b] : (*locals)[b];
    }
    m_models->setBoneWorldOverride(c.instance, kke::blendPoses(c.blendFrom, animated, w));
}

// Jolt only sees what it's given: the floor (top at y = 0, like FEMFX's
// ground) and the level's two walls, as boxes. Without them a Jolt
// ragdoll would fall forever.
void SyntySceneModule::addJoltLevel() {
#if KKE_ENABLE_JOLT
    auto* rigid = m_app->getModule<kke::RigidBodyModule>();
    if (!rigid) return;
    kke::RigidWorld& w = rigid->world();
    auto box = [&](glm::vec3 center, glm::vec3 half) {
        kke::RigidWorld::BodyDesc b;
        b.motion = kke::RigidWorld::Motion::Static;
        b.position = center;
        b.halfExtents = half;
        if (w.add(b) == kke::RigidWorld::kNoBody) kke::log::get(name())->warn("Jolt refused a level box at ({}, {}, {})", center.x, center.y, center.z);
    };
    box(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(30.0f, 0.5f, 30.0f));
    box(glm::vec3(0.0f, 1.5f, -10.1f), glm::vec3(10.0f, 1.5f, 0.1f));
    box(glm::vec3(-10.1f, 1.5f, -5.0f), glm::vec3(0.1f, 1.5f, 5.0f));
#endif
}

void SyntySceneModule::ragdoll(Character& c, const glm::vec3& push) {
    if (!m_physics || c.ragdoll) return;
    const kke::ModelData* d = m_models->model(c.model);
    if (!d) return;
    // Start from whatever pose the character is in right now.
    glm::mat4 instance = m_models->transform(c.instance);
    std::vector<glm::mat4> world = m_models->boneWorld(c.instance);
    for (glm::mat4& w : world) w = instance * w;
    std::string missing;
    // Realistic joint limits come with the builders; a game can change
    // any of them here (c.ragdollDesc.findJoint("knee_l")->... or
    // scaleLimits) before the ragdoll is created.
    c.ragdollDesc = c.animal ? kke::buildQuadrupedRagdoll(*d, world, c.mass, &missing) : kke::buildHumanoidRagdoll(*d, world, c.mass, &missing);
    if (c.ragdollDesc.bodies.empty()) {
        kke::log::get(name())->warn("'{}' can't ragdoll: skeleton has no '{}' bone", c.label, missing);
        return;
    }
    c.binding = kke::bindSkeletonToRagdoll(*d, world, c.ragdollDesc);
    c.ragdoll = m_physics->createRagdoll(c.ragdollDesc, glm::vec3(0.0f));
    if (!c.ragdoll) return;
    // Shove the upper body harder than the legs so it topples, not slides.
    for (const char* body : { c.animal ? "chest" : "torso", "head" }) m_physics->pushRagdollBody(c.ragdoll, c.ragdollDesc.findBody(body), push);
    m_physics->pushRagdollBody(c.ragdoll, c.ragdollDesc.findBody("pelvis"), push * 0.4f);
    c.behaviorBeforeRagdoll = c.behavior;
    c.behavior = "ragdoll";
    c.blendAge = -1.0f; // knocked down again while getting up
    c.blendFrom.clear();
}

// Stands a glass pane 1.8 m in front of the character (away from the
// camera) and throws the character through it: a ragdoll (Jolt limbs
// through PhysicsBridgeModule, or FEMFX rigid bodies) hitting a
// fracturable FEMFX deformable body. This one needs the
// concrete PhysicsModule for the pane — the ragdoll itself only goes
// through IRagdollPhysics.
void SyntySceneModule::throughGlass(Character& c) {
#if KKE_ENABLE_FEMFX
    auto* physics = m_app->getModule<kke::PhysicsModule>();
    if (!physics || c.ragdoll) return;
    glm::vec3 away = m_app->camera().target - m_app->camera().position;
    away.y = 0.0f;
    away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(0, 0, -1);
    kke::Material glass;
    glass.density = 2500.0f;
    glass.stiffness = 7.0e7f;
    glass.poissonsRatio = 0.22f;
    glass.fractureStressThreshold = 2000.0f; // same measured glass value as PhysicsModule's Glass Sheet scene
    glass.metallic = 0.0f;
    glass.roughness = 0.05f;
    glass.textureId = 4;
    glm::vec3 pane = glm::vec3(m_models->transform(c.instance)[3]) + away * 1.8f + glm::vec3(0, 1.21f, 0);
    float yaw = glm::degrees(std::atan2(away.x, away.z));
    physics->spawnFracturableBox(glm::ivec3(6, 6, 1), glm::vec3(2.2f, 2.4f, 0.1f), pane, glass, yaw);
    ragdoll(c, away * 11.0f + glm::vec3(0, 1.5f, 0));
#else
    (void)c;
#endif
}

void SyntySceneModule::standUp(Character& c) {
    if (!c.ragdoll) return;
    // Get up where the body landed, facing the way it was: move the
    // character there, then blend from the lying pose (now relative to
    // the new spot) back to its animation.
    std::vector<glm::mat4> bodies;
    const kke::ModelData* d = m_models->model(c.model);
    const int pelvis = c.ragdollDesc.findBody("pelvis");
    if (d && pelvis >= 0 && m_physics->ragdollBodyTransforms(c.ragdoll, bodies)) {
        glm::mat4 t = m_models->transform(c.instance);
        const glm::vec3 landed(bodies[pelvis][3]);
        t[3] = glm::vec4(landed.x, t[3].y, landed.z, 1.0f);
        m_models->setTransform(c.instance, t);
        c.blendFrom = kke::poseFromRagdoll(*d, c.binding, bodies, glm::inverse(t));
        c.blendAge = 0.0f;
        m_models->setBoneWorldOverride(c.instance, c.blendFrom);
    } else {
        m_models->setBoneWorldOverride(c.instance, {});
    }
    m_physics->destroyRagdoll(c.ragdoll);
    c.ragdoll = 0;
    c.behavior = c.behaviorBeforeRagdoll;
}

void SyntySceneModule::onEvent(const SDL_Event& event) {
    // Shift+R ragdolls everyone (the pad does it from the panel); the
    // rest are actions (defineInput) so a controller does them too.
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    if (event.key.key == SDLK_F1) {
        m_showEnginePanels = !m_showEnginePanels;
        for (kke::Module* m : m_enginePanels) m->setUiVisible(m_showEnginePanels);
    }
    if (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_SHIFT)) ragdollAll(true);
}

void SyntySceneModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Characters" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("synty.ragdoll", "Ragdoll the selected character", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_WEST);
    action("synty.stand", "Everyone stands up", SDL_SCANCODE_T, SDL_GAMEPAD_BUTTON_NORTH);
    action("synty.glass", "Through a glass pane", SDL_SCANCODE_G, SDL_GAMEPAD_BUTTON_SOUTH);
    action("synty.bones", "Show bones", SDL_SCANCODE_B, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    action("synty.prev", "Previous character", SDL_SCANCODE_LEFTBRACKET, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    action("synty.next", "Next character", SDL_SCANCODE_RIGHTBRACKET, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    in->commitDefaults();
}

void SyntySceneModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    if (m.pressed("synty.bones")) m_showBones = !m_showBones;
    if (m_characters.empty()) return;
    // Shift+R is "everyone" (onEvent); plain R is the selected one.
    if (m.pressed("synty.ragdoll") && !(SDL_GetModState() & SDL_KMOD_SHIFT)) ragdollAll(false);
    if (m.pressed("synty.stand"))
        for (Character& c : m_characters) standUp(c);
    if (m.pressed("synty.glass")) throughGlass(m_characters[size_t(m_selected)]);
    const int n = static_cast<int>(m_characters.size());
    if (m.pressed("synty.prev")) select((m_selected + n - 1) % n);
    if (m.pressed("synty.next")) select((m_selected + 1) % n);
}

void SyntySceneModule::select(int index) {
    m_selected = index;
    const kke::ModelData* d = m_models->model(m_characters[size_t(index)].model);
    m_poseEuler.assign(d ? d->bones.size() : 0, glm::vec3(0.0f));
}

// Pushed away from the camera, so you see them fall.
void SyntySceneModule::ragdollAll(bool everyone) {
    glm::vec3 away = m_app->camera().target - m_app->camera().position;
    away.y = 0.0f;
    away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(0, 0, -1);
    for (int i = 0; i < static_cast<int>(m_characters.size()); ++i)
        if (everyone || i == m_selected) ragdoll(m_characters[size_t(i)], away * 4.0f + glm::vec3(0, 1.0f, 0));
}

// The panel (RmlUi, kke::DemoPanelModule): View on a controller or F3
// gives it the controller, the mouse just clicks. F1 still shows the
// engine's ImGui developer panels.
void SyntySceneModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Characters");
    if (m_packDir.empty()) {
        s.text("Synty POLYGON Prototype pack not found.");
        s.note("Put your extracted pack folder(s) inside assets/synty/ (any layout: assets/synty/POLYGON_Prototype/Characters/... works), "
               "or set KKE_ASSETS_DIR to the folder that contains them. Never committed to git: Synty assets are licensed per user.");
        std::string looked = "Looked in:";
        for (const std::string& p : m_searched) looked += " " + p;
        s.note(looked);
        return;
    }
    s.hint("{synty.ragdoll} ragdoll  Shift+{synty.ragdoll} everyone  {synty.stand} stand up  {synty.glass} through glass  "
           "{synty.prev}{synty.next} who  {synty.bones} bones  F1 developer panels",
           "{synty.ragdoll} ragdoll  {synty.stand} stand up  {synty.glass} through glass  {synty.prev}{synty.next} who  "
           "{synty.bones} bones  {camera.orbit} look  {camera.zoom} zoom");
    s.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu props, %zu characters, %zu draw calls, %zu culled", m_propCount, m_characters.size(),
                      m_models->drawCallsLastFrame(), m_models->culledLastFrame());
        return std::string(buf);
    });
    s.toggle("Show bones", &m_showBones);
    auto physics = [this] { return m_physics != nullptr && !m_characters.empty(); };
    s.button("Ragdoll the selected one", [this] { ragdollAll(false); }).showIf(physics);
    s.button("Ragdoll everyone", [this] { ragdollAll(true); }).showIf(physics);
    s.button("Everyone stands up", [this] {
        for (Character& c : m_characters) standUp(c);
    }).showIf(physics);
    s.button("Through a glass pane", [this] { throughGlass(m_characters[size_t(m_selected)]); }).showIf(physics);
    s.note("Ragdolls need a physics module (Jolt or FEMFX).").showIf([this] { return m_physics == nullptr; });
    if (m_characters.empty()) return;

    std::vector<std::string> labels;
    for (const Character& c : m_characters) labels.push_back(c.label);
    s.choice("Character", kke::DemoPanelModule::Ref<int>([this] {
                 m_selectedIndex = m_selected;
                 return &m_selectedIndex;
             }),
             labels, [this] { select(m_selectedIndex); });
    auto model = [this]() -> const kke::ModelData* { return m_models->model(m_characters[size_t(m_selected)].model); };
    s.text([this, model] {
        const kke::ModelData* d = model();
        if (!d) return std::string();
        char buf[120];
        std::snprintf(buf, sizeof(buf), "%zu bones, %zu animation clip(s), %zu triangles", d->bones.size(), d->animations.size(), d->triangleCount());
        return std::string(buf);
    });
    s.button("Play the FBX clip", [this, model] {
        const kke::ModelData* d = model();
        if (!d || d->animations.empty()) return;
        Character& c = m_characters[size_t(m_selected)];
        c.behavior = "clip";
        m_models->playAnimation(c.instance, 0, true);
    }).showIf([model] {
        const kke::ModelData* d = model();
        return d && !d->animations.empty();
    });
    s.button("Rest pose (pose bones)", [this, model] {
        const kke::ModelData* d = model();
        if (!d) return;
        Character& c = m_characters[size_t(m_selected)];
        c.behavior = "pose";
        m_models->playAnimation(c.instance, -1);
        m_poseEuler.assign(d->bones.size(), glm::vec3(0.0f));
    });
    // Posing: a bone, then its rotation relative to rest in its own axes.
    auto posing = [this, model] {
        const kke::ModelData* d = model();
        return d && !d->bones.empty() && m_characters[size_t(m_selected)].behavior == "pose";
    };
    s.choice("Bone", kke::DemoPanelModule::Ref<int>([this, posing, model]() -> int* {
                 if (!posing()) return nullptr;
                 m_selectedBone = std::min(m_selectedBone, static_cast<int>(model()->bones.size()) - 1);
                 return &m_selectedBone;
             }),
             std::function<std::vector<std::string>()>([model] {
                 std::vector<std::string> names;
                 if (const kke::ModelData* d = model())
                     for (const auto& b : d->bones) names.push_back(b.name);
                 return names;
             }));
    auto axis = [this, posing, model](int component) {
        return kke::DemoPanelModule::Ref<float>([this, posing, model, component]() -> float* {
            if (!posing()) return nullptr;
            if (m_poseEuler.size() != model()->bones.size()) m_poseEuler.assign(model()->bones.size(), glm::vec3(0.0f));
            return &m_poseEuler[size_t(m_selectedBone)][component];
        });
    };
    auto pose = [this, model] {
        Character& c = m_characters[size_t(m_selected)];
        rotateBone(c, model()->bones[size_t(m_selectedBone)].name.c_str(), m_poseEuler[size_t(m_selectedBone)]);
    };
    s.slider("X", axis(0), -180.0f, 180.0f, "%.0f deg", pose, 5.0f);
    s.slider("Y", axis(1), -180.0f, 180.0f, "%.0f deg", pose, 5.0f);
    s.slider("Z", axis(2), -180.0f, 180.0f, "%.0f deg", pose, 5.0f);
}

} // namespace kke_demo
