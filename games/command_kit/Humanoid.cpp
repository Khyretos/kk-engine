#include "Humanoid.h"

#include "Scenery.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

namespace command_kit {

HumanoidKit::HumanoidKit(kke::Application& app) : m_app(app) {
    // The fallback body: a torso and a dark visor on the front (-Z).
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.25f, 0.9f, 0.18f }, glm::vec3(0.8f), v, idx);
    appendBox({ 0.0f, 1.55f, -0.18f }, { 0.18f, 0.07f, 0.03f }, glm::vec3(0.1f), v, idx);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_block->upload(v, idx);
}

HumanoidKit::~HumanoidKit() = default;

bool HumanoidKit::load(kke::ModelModule& models) {
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string file = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    if (file.empty() || !std::filesystem::exists(file)) {
        kke::log::get("Humanoid")->warn("animation library not found (assets/animations/UAL1_Standard.fbx): people are blocks");
        return false;
    }
    m_model = models.load(file);
    const kke::ModelData* d = m_model ? models.model(m_model) : nullptr;
    if (!d || d->bones.empty() || d->animations.empty()) {
        m_model = 0;
        return false;
    }
    m_rig = kke::ModelData{};
    m_rig.bones = d->bones;
    m_rig.animations = d->animations;
    m_set = std::make_unique<kke::AnimationSet>(m_rig);
    const glm::vec3 fwd = kke::modelForward(m_rig);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    m_models = &models;
    m_hands[0] = kke::makeHandRig(*d, true);
    m_hands[1] = kke::makeHandRig(*d, false);
    return true;
}

const kke::ModelData* HumanoidKit::skinned() const { return m_models && m_model ? m_models->model(m_model) : nullptr; }

Humanoid::Humanoid(HumanoidKit& kit, kke::ModelModule& models, const glm::vec3& tint) : m_kit(kit), m_models(models), m_tint(tint) {
    if (!kit.loaded()) return;
    m_instance = models.spawn(kit.model());
    models.setOverlayEnabled(m_instance, false);
    models.setTint(m_instance, tint);
    m_anim = std::make_unique<kke::Animator>(kit.set());
    const kke::AnimationSet& s = kit.set();
    m_move = m_anim->addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                               { s.find("|Walk_Loop"), 1.6f },
                                               { s.find("Jog_Fwd_Loop"), 3.6f },
                                               { s.find("Sprint_Loop"), 6.2f } } });
    m_anim->play(m_move, 0.0f);
}

Humanoid::~Humanoid() {
    if (m_instance) m_models.remove(m_instance);
}

void Humanoid::act(const std::string& clip, bool loop, float speed, float fade, bool restart) {
    if (!m_anim) {
        m_action = clip;
        return;
    }
    if (clip.empty()) {
        if (!m_action.empty()) m_anim->play(m_move, fade);
        m_action.clear();
        return;
    }
    if (clip == m_action && !restart) return;
    const std::string key = clip + (loop ? "@loop" : "@once") + std::to_string(speed);
    auto it = m_states.find(key);
    if (it == m_states.end()) {
        const int c = m_kit.set().find(clip);
        if (c < 0) {
            kke::log::get("Humanoid")->warn("no clip named like '{}'", clip);
            m_states[key] = -1;
            return;
        }
        it = m_states.emplace(key, m_anim->addClipState(key, c, loop, speed)).first;
    }
    if (it->second < 0) return;
    m_anim->play(it->second, fade, true);
    m_action = clip;
}

bool Humanoid::actionFinished() const { return m_action.empty() || !m_anim || m_anim->finished(); }

void Humanoid::update(const glm::vec3& feet, float yawDegrees, float groundSpeed, float dt) {
    m_transform = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_kit.modelYaw() - yawDegrees), glm::vec3(0, 1, 0));
    if (!m_instance || !m_anim) {
        m_transform = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-yawDegrees), glm::vec3(0, 1, 0));
        return;
    }
    m_models.setTransform(m_instance, m_transform);
    m_anim->setParameter(groundSpeed);
    m_anim->update(dt);
    if (!m_ik) {
        if (std::vector<glm::mat4>* locals = m_models.boneLocals(m_instance)) kke::poseToLocals(m_anim->pose(), *locals);
        return;
    }
    // Clip, then hands and feet on the world, then fingers round what the palms hold.
    const glm::vec3 velocity = m_haveFeet && dt > 0.0f ? (feet - m_lastFeet) / dt : glm::vec3(0.0f);
    m_lastFeet = feet;
    m_haveFeet = true;
    const kke::ModelData& rig = m_kit.rig();
    kke::Pose pose = m_anim->pose();
    m_ik->apply(rig, pose, m_transform, m_ground, velocity, dt);
    m_bones = kke::poseToModel(rig, pose);
    for (int side = 0; side < 2; ++side) {
        const kke::HandRig& h = m_kit.hand(side);
        if (m_grip[side] <= 0.0f || !h.valid()) continue;
        const glm::mat4 p = m_bones[size_t(h.hand)] * h.palm;
        const glm::vec3 centre = glm::vec3(p[3]) + glm::normalize(glm::vec3(p[2])) * m_grip[side];
        kke::GripSurface ball;
        ball.capsules.push_back({ centre, centre, m_grip[side] });
        kke::wrapFingers(rig, pose, h, ball, 1.0f, 0.8f);
        m_grip[side] = 0.0f;
    }
    m_bones = kke::poseToModel(rig, pose);
    if (std::vector<glm::mat4>* locals = m_models.boneLocals(m_instance)) kke::poseToLocals(pose, *locals);
}

void Humanoid::enableIk(kke::CharacterIk::GroundQuery ground) {
    m_ground = std::move(ground);
    if (!m_ik && m_instance) m_ik = std::make_unique<kke::CharacterIk>(m_kit.rig(), m_kit.skinned());
}

void Humanoid::reach(int side, const glm::vec3& point, const std::optional<glm::vec3>& elbowToward) {
    if (m_ik) m_ik->hand(side ? kke::CharacterIk::Right : kke::CharacterIk::Left, point, elbowToward);
}

void Humanoid::grip(int side, float radius) { m_grip[side & 1] = radius; }

bool Humanoid::palm(int side, glm::mat4& world) const {
    const kke::HandRig& h = m_kit.hand(side);
    if (m_bones.empty() || !h.valid()) return false;
    world = m_transform * m_bones[size_t(h.hand)] * h.palm;
    return true;
}

glm::vec3 Humanoid::inPalm(int side, float radius) const {
    glm::mat4 p(1.0f);
    if (!palm(side, p)) {
        // No mannequin: in front of the block body, at the hand's height.
        const float s = side ? 1.0f : -1.0f;
        return glm::vec3(m_transform * glm::vec4(0.3f * s, 1.0f, -0.3f, 1.0f));
    }
    return glm::vec3(p[3]) + glm::normalize(glm::vec3(p[2])) * radius;
}

float Humanoid::reachWeight(int side) const {
    return m_ik ? m_ik->handWeight(side ? kke::CharacterIk::Right : kke::CharacterIk::Left) : 0.0f;
}

void Humanoid::setTint(const glm::vec3& tint) {
    m_tint = tint;
    if (m_instance) m_models.setTint(m_instance, tint);
}

void Humanoid::setVisible(bool visible) {
    m_visible = visible;
    if (m_instance) m_models.setVisible(m_instance, visible);
}

void Humanoid::render(const kke::RenderContext& ctx) {
    if (m_instance || !m_visible) return;
    // Without the mannequin everyone is the same grey block with a visor.
    m_kit.block().draw(ctx, m_transform, 0.0f, 0.6f);
}

void Humanoid::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_instance || !m_visible) return;
    m_kit.block().drawShadow(ctx, m_transform);
}

} // namespace command_kit
