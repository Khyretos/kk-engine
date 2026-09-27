#include "Mannequin.h"

#include "Procedural.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <typeindex>

namespace cookbook {

namespace {

// The rotation part of a bone's model-space matrix (its columns can carry
// scale, which a quaternion must not).
glm::quat rotationOf(const glm::mat4& m) {
    return glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2]))));
}

int findBone(const kke::ModelData& d, const char* canonical) {
    for (size_t b = 0; b < d.bones.size(); ++b)
        if (kke::canonicalBoneName(d.bones[b].name) == canonical) return static_cast<int>(b);
    return -1;
}

} // namespace

Mannequin::Mannequin() = default;
Mannequin::~Mannequin() = default;

std::vector<kke::ModuleDependency> Mannequin::dependencies() const {
    return { { std::type_index(typeid(kke::ModelModule)), true, "draws the mannequins" },
             { std::type_index(typeid(kke::RigidBodyModule)), true, "the ground under the feet" } };
}

void Mannequin::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_rigid = app.getModule<kke::RigidBodyModule>();
    // KKE_COOKBOOK_MANNEQUINS=0: none (screenshots of the Lua recipes).
    if (const char* on = kke::dev::env("KKE_COOKBOOK_MANNEQUINS"); on && std::string(on) == "0") return;

    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string file = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        kke::log::get(name())->info("assets/animations/UAL1_Standard.fbx not found: no mannequins");
        return;
    }
    m_model = m_models->load(file);
    m_data = m_model ? m_models->model(m_model) : nullptr;
    if (!m_data || m_data->bones.empty() || m_data->animations.empty()) {
        kke::log::get(name())->info("{} has no skeleton or clips: no mannequins", file);
        return;
    }
    const glm::vec3 fwd = kke::modelForward(*m_data);
    m_turnToPlusZ = -glm::degrees(std::atan2(fwd.x, fwd.z));

    // --8<-- [start:blend]
    // Clips once as poses (AnimationSet), then an Animator per character.
    // A 1D blend space: idle at 0 m/s, walk at 1.4, jog at 3.2. Setting
    // the parameter to the speed blends the two nearest clips, in step.
    m_set = std::make_unique<kke::AnimationSet>(*m_data);
    m_walkAnim = std::make_unique<kke::Animator>(*m_set);
    const int move = m_walkAnim->addBlendState(
        "move", { { { m_set->find("|Idle_Loop"), 0.0f }, { m_set->find("|Walk_Loop"), 1.4f }, { m_set->find("Jog_Fwd_Loop"), 3.2f } } });
    m_walkAnim->play(move, 0.0f);
    // --8<-- [end:blend]

    m_standAnim = std::make_unique<kke::Animator>(*m_set);
    m_standAnim->play(m_standAnim->addClipState("idle", m_set->find("|Idle_Loop")), 0.0f);

    // --8<-- [start:rig]
    // The chains IK bends (found by bone name) and the head's forward axis
    // in its own space, worked out once from the rest pose.
    m_armR = kke::findChain(*m_data, "upperarm_r", "lowerarm_r", "hand_r");
    m_feet = kke::FootPlacer(*m_data, kke::findChain(*m_data, "thigh_l", "calf_l", "foot_l"), kke::findChain(*m_data, "thigh_r", "calf_r", "foot_r"),
                             findBone(*m_data, "pelvis"));
    m_head = findBone(*m_data, "head");
    if (m_head >= 0) {
        const std::vector<glm::mat4> rest = kke::poseToModel(*m_data, m_set->restPose());
        m_headForward = glm::normalize(glm::inverse(rotationOf(rest[static_cast<size_t>(m_head)])) * fwd);
    }
    // --8<-- [end:rig]

    m_walker = m_models->spawn(m_model);
    m_stander = m_models->spawn(m_model);
    // The stander faces +Z (toward where the player starts).
    m_models->setTransform(m_stander,
                           glm::rotate(glm::translate(glm::mat4(1.0f), standAt), glm::radians(m_turnToPlusZ), glm::vec3(0, 1, 0)));

    // The orb the hand reaches for: a small glowing cube.
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 c(1.0f, 0.85f, 0.3f);
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t b = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)})
            v.push_back({ (n + u * k.x + w * k.y) * 0.06f, c, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
        else idx.insert(idx.end(), { b, b + 2, b + 1, b, b + 3, b + 2 });
    }
    m_orb = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_orb->upload(v, idx);
    m_lookAt = standAt + glm::vec3(0.0f, 1.6f, 2.0f);
}

void Mannequin::update(const kke::UpdateContext& ctx) {
    if (!m_set) return;
    m_time += ctx.dt;
    updateWalker(ctx.dt);
    updateStander(ctx.dt);
}

// --8<-- [start:walker]
void Mannequin::updateWalker(float dt) {
    // Speed goes up and down between standing and jogging; a spring keeps
    // the changes smooth, and the blend space follows the speed.
    const float wanted = 1.6f + 1.6f * std::sin(m_time * 0.35f);
    springTowards(m_speed, m_speedVelocity, wanted, 0.4f, dt);
    m_angle += m_speed / circleRadius * dt; // radians: arc length / radius
    m_walkAnim->setParameter(m_speed);
    m_walkAnim->update(dt);

    // Round the circle, facing along it (counter-clockwise seen from above).
    const glm::vec3 at = circleAt + glm::vec3(std::cos(m_angle), 0.0f, -std::sin(m_angle)) * circleRadius;
    const float headingDegrees = glm::degrees(m_angle) + 180.0f; // tangent of the circle
    m_models->setTransform(m_walker, glm::rotate(glm::translate(glm::mat4(1.0f), at), glm::radians(headingDegrees + m_turnToPlusZ),
                                                 glm::vec3(0, 1, 0)));
    if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_walker)) kke::poseToLocals(m_walkAnim->pose(), *locals);
}
// --8<-- [end:walker]

glm::vec3 Mannequin::orbPosition() const {
    // In front of the stander, to its right, drifting in a slow figure eight.
    return standAt + glm::vec3(-0.35f + 0.25f * std::sin(m_time * 0.9f), 1.25f + 0.25f * std::sin(m_time * 1.8f), 0.45f);
}

void Mannequin::updateStander(float dt) {
    m_standAnim->update(dt);
    kke::Pose pose = m_standAnim->pose(); // the animated pose; we pose on top of it
    const glm::mat4 toWorld = m_models->transform(m_stander);
    const glm::mat4 toModel = glm::inverse(toWorld);
    auto model = [&](const glm::vec3& world) { return glm::vec3(toModel * glm::vec4(world, 1.0f)); };

    // --8<-- [start:feet]
    // 1. Feet on the ground: a ray down from each foot, in model space.
    kke::RigidWorld& w = m_rigid->world();
    auto ground = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        const kke::RigidWorld::RayHit h = w.raycast(glm::vec3(toWorld * glm::vec4(from, 1.0f)), glm::vec3(0, -1, 0), 1.2f);
        if (!h.hit || h.normal.y < 0.5f) return false;
        hit = model(h.point);
        normal = glm::normalize(glm::mat3(toModel) * h.normal);
        return true;
    };
    if (m_feet.valid()) m_feet.apply(*m_data, pose, kke::FootPlacer::SurfaceQuery(ground), dt);
    // --8<-- [end:feet]

    // --8<-- [start:ik]
    // 2. The right hand on the orb: two-bone IK (shoulder, elbow, wrist).
    // The pole is where the elbow should point: out and down.
    if (m_armR.valid()) {
        const glm::vec3 orb = orbPosition();
        const glm::vec3 pole = standAt + glm::vec3(-0.8f, 0.6f, -0.3f);
        kke::solveTwoBone(*m_data, pose, m_armR, model(orb), model(pole), 1.0f);
    }
    // --8<-- [end:ik]

    // --8<-- [start:look]
    // 3. The head looks at the camera: a spring smooths where it looks,
    // turnTowards() limits how far the neck turns (Procedural.h), and the
    // turn goes into the head bone's local rotation.
    if (m_head >= 0) {
        springTowards(m_lookAt, m_lookVelocity, m_app->camera().position, 0.25f, dt);
        const std::vector<glm::mat4> bones = kke::poseToModel(*m_data, pose);
        const size_t head = static_cast<size_t>(m_head);
        const glm::quat headRot = rotationOf(bones[head]);
        const glm::vec3 facing = headRot * m_headForward;
        const glm::vec3 toTarget = model(m_lookAt) - glm::vec3(bones[head][3]);
        const glm::quat turn = turnTowards(facing, toTarget, 60.0f);
        const int parent = m_data->bones[head].parent;
        const glm::quat parentRot = parent >= 0 ? rotationOf(bones[static_cast<size_t>(parent)]) : glm::quat(1, 0, 0, 0);
        // model = parent * local, so a model-space turn T becomes
        // local' = parent^-1 * T * parent * local.
        pose[head].r = glm::normalize(glm::inverse(parentRot) * turn * parentRot * pose[head].r);
    }
    // --8<-- [end:look]

    if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_stander)) kke::poseToLocals(pose, *locals);
}

void Mannequin::render(const kke::RenderContext& ctx) {
    if (m_orb && m_set) m_orb->draw(ctx, glm::translate(glm::mat4(1.0f), orbPosition()), 0.0f, 0.3f);
}

void Mannequin::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_orb && m_set) m_orb->drawShadow(ctx, glm::translate(glm::mat4(1.0f), orbPosition()));
}

} // namespace cookbook
