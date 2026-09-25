#include "SyntySceneModule.h"

#include "kke/Application.h"
#include "kke/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <typeindex>

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
    // IRagdollPhysics (FEMFX's PhysicsModule today) enables ragdolls.
    auto ragdollProviders = app.findCapability<kke::IRagdollPhysics>();
    m_physics = ragdollProviders.empty() ? nullptr : ragdollProviders.front();
    const char* base = SDL_GetBasePath();
    m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &m_searched);
    if (!m_packDir.empty()) m_catalog = kke::AssetCatalog::scan(m_packDir);
    if (m_packDir.empty() || !m_catalog.find("SM_Buildings_Floor_5x5_01")) {
        kke::log::get(name())->warn("Synty POLYGON Prototype pack not found. Put the extracted pack folder(s) in assets/synty/ "
                                    "(e.g. assets/synty/POLYGON_Prototype/Characters/...) or set KKE_ASSETS_DIR.");
        m_packDir.clear();
        return;
    }
    kke::log::get(name())->info("asset folder '{}': {} pack(s), {} assets", m_packDir, m_catalog.packs.size(), m_catalog.assets.size());

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

    // --- characters in a row, each doing something different
    struct Spec { const char* file; const char* label; const char* behavior; glm::vec3 tint; };
    const Spec specs[] = {
        { "Characters/SK_Character_Dummy_Male_01.fbx", "Dummy (male) - FBX clip", "clip", { 1.0f, 1.0f, 1.0f } },
        { "Characters/SK_Character_Dummy_Female_01.fbx", "Dummy (female) - procedural wave", "wave", { 1.0f, 0.75f, 0.6f } },
        { "Characters/SK_Character_Male_Face_01.fbx", "Male - idle breathing", "breathe", { 0.65f, 0.8f, 1.0f } },
        { "Characters/SK_Character_Female_Face_01.fbx", "Female - hand-posed", "pose", { 0.75f, 1.0f, 0.7f } },
    };
    for (int i = 0; i < 4; ++i) {
        Character c;
        c.label = specs[i].label;
        c.behavior = specs[i].behavior;
        c.model = load(specs[i].file);
        if (!c.model) continue;
        c.position = glm::vec3((i - 1.5f) * 1.6f, 0.0f, 0.0f);
        c.instance = m_models->spawn(c.model, glm::translate(glm::mat4(1.0f), c.position));
        m_models->setTint(c.instance, specs[i].tint);
        if (c.behavior == "clip") m_models->playAnimation(c.instance, 0, true);
        m_characters.push_back(c);
    }
    if (m_selected >= static_cast<int>(m_characters.size())) m_selected = 0;
    if (!m_characters.empty()) {
        const kke::ModelData* d = m_models->model(m_characters[m_selected].model);
        m_poseEuler.assign(d ? d->bones.size() : 0, glm::vec3(0.0f));
    }
    kke::log::get(name())->info("scene: {} props, {} characters", m_propCount, m_characters.size());
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
    }
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
    c.ragdollDesc = kke::buildHumanoidRagdoll(*d, world, 70.0f, &missing);
    if (c.ragdollDesc.bodies.empty()) {
        kke::log::get(name())->warn("'{}' can't ragdoll: skeleton has no '{}' bone", c.label, missing);
        return;
    }
    c.binding = kke::bindSkeletonToRagdoll(*d, world, c.ragdollDesc);
    c.ragdoll = m_physics->createRagdoll(c.ragdollDesc, glm::vec3(0.0f));
    if (!c.ragdoll) return;
    // Shove the upper body harder than the legs so it topples, not slides.
    for (const char* body : { "torso", "head" }) m_physics->pushRagdollBody(c.ragdoll, c.ragdollDesc.findBody(body), push);
    m_physics->pushRagdollBody(c.ragdoll, c.ragdollDesc.findBody("pelvis"), push * 0.4f);
    c.behaviorBeforeRagdoll = c.behavior;
    c.behavior = "ragdoll";
}

// Stands a glass pane 1.8 m in front of the character (away from the
// camera) and throws the character through it: a ragdoll (FEMFX rigid
// bodies) hitting a fracturable FEMFX deformable body. This one needs the
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
    m_physics->destroyRagdoll(c.ragdoll);
    c.ragdoll = 0;
    m_models->setBoneWorldOverride(c.instance, {});
    c.behavior = c.behaviorBeforeRagdoll;
}

void SyntySceneModule::onEvent(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    if (event.key.key == SDLK_B) m_showBones = !m_showBones;
    if (event.key.key == SDLK_R && !m_characters.empty()) {
        // Shift+R: everyone; R: the selected character. Pushed away from
        // the camera so you see them fall.
        glm::vec3 away = m_app->camera().target - m_app->camera().position;
        away.y = 0.0f;
        away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(0, 0, -1);
        bool all = (event.key.mod & SDL_KMOD_SHIFT) != 0;
        for (int i = 0; i < static_cast<int>(m_characters.size()); ++i) {
            if (all || i == m_selected) ragdoll(m_characters[i], away * 4.0f + glm::vec3(0, 1.0f, 0));
        }
    }
    if (event.key.key == SDLK_T) for (Character& c : m_characters) standUp(c);
    if (event.key.key == SDLK_G && !m_characters.empty()) throughGlass(m_characters[m_selected]);
}

void SyntySceneModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 170), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Characters");
    if (m_packDir.empty()) {
        ImGui::TextWrapped("Synty POLYGON Prototype pack not found.\n\nPut your extracted pack folder(s) inside assets/synty/ "
                           "(any layout: assets/synty/POLYGON_Prototype/Characters/... works), or set KKE_ASSETS_DIR to the "
                           "folder that contains them. Never committed to git: Synty assets are licensed per user.\n\nLooked in:");
        for (const std::string& p : m_searched) ImGui::BulletText("%s", p.c_str());
        ImGui::End();
        return;
    }
    ImGui::Text("%zu props, %zu characters, %zu draw calls", m_propCount, m_characters.size(), m_models->drawCallsLastFrame());
    ImGui::Checkbox("Show bones (B)", &m_showBones);
    if (m_physics) {
        ImGui::TextWrapped("R: ragdoll selected   Shift+R: everyone   T: stand up   G: through a glass pane\n"
                           "(physics body boxes: Physics panel)");
    } else {
        ImGui::TextDisabled("Ragdolls need a physics module (build with -DKKE_ENABLE_FEMFX=ON)");
    }
    for (int i = 0; i < static_cast<int>(m_characters.size()); ++i) {
        if (ImGui::RadioButton(m_characters[i].label.c_str(), m_selected == i)) {
            m_selected = i;
            const kke::ModelData* d = m_models->model(m_characters[i].model);
            m_poseEuler.assign(d ? d->bones.size() : 0, glm::vec3(0.0f));
        }
    }
    if (m_characters.empty()) {
        ImGui::End();
        return;
    }
    Character& c = m_characters[m_selected];
    const kke::ModelData* d = m_models->model(c.model);
    if (!d) {
        ImGui::End();
        return;
    }
    ImGui::Separator();
    ImGui::Text("%zu bones, %zu animation clip(s), %zu triangles", d->bones.size(), d->animations.size(), d->triangleCount());
    if (!d->animations.empty() && ImGui::Button(c.behavior == "clip" ? "Restart clip" : "Play FBX clip")) {
        c.behavior = "clip";
        m_models->playAnimation(c.instance, 0, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rest pose")) {
        c.behavior = "pose";
        m_models->playAnimation(c.instance, -1);
        m_poseEuler.assign(d->bones.size(), glm::vec3(0.0f));
    }
    if (c.behavior == "pose" && !d->bones.empty()) {
        ImGui::TextWrapped("Pose a bone (rotation relative to rest, in the bone's own axes):");
        m_selectedBone = std::min(m_selectedBone, static_cast<int>(d->bones.size()) - 1);
        if (ImGui::BeginCombo("Bone", d->bones[m_selectedBone].name.c_str())) {
            for (int b = 0; b < static_cast<int>(d->bones.size()); ++b) {
                if (ImGui::Selectable(d->bones[b].name.c_str(), b == m_selectedBone)) m_selectedBone = b;
            }
            ImGui::EndCombo();
        }
        if (m_poseEuler.size() != d->bones.size()) m_poseEuler.assign(d->bones.size(), glm::vec3(0.0f));
        glm::vec3& e = m_poseEuler[m_selectedBone];
        bool changed = ImGui::SliderFloat("X", &e.x, -180, 180) | ImGui::SliderFloat("Y", &e.y, -180, 180) | ImGui::SliderFloat("Z", &e.z, -180, 180);
        if (changed) rotateBone(c, d->bones[m_selectedBone].name.c_str(), e);
    }
    ImGui::End();
}

} // namespace kke_demo
