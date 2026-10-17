#include "AnimDemoModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/KnownPacks.h"
#include "kke/Log.h"
#include "kke/Mesh.h"
#include "kke/ProceduralAnim.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace kke_anim {

namespace {

constexpr int kJoints = 15;            // a tentacle's joints, base and tip included
constexpr float kSegment = 0.18f;      // m between joints
constexpr float kArena = 6.0f;         // m: the mannequin stays this close to the middle
const glm::vec3 kCopyAt[2] = { { 0.4f, 0.0f, 2.8f }, { -1.2f, 0.0f, 2.8f } }; // without, with rest matching (left, right as the camera starts)

// A box as 24 vertices (flat normals) into v / idx.
void box(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color) {
    const glm::vec3 c[8] = { { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
                             { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
    const int faces[6][4] = { { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 }, { 0, 4, 7, 3 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 } };
    const glm::vec3 normals[6] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (int k = 0; k < 4; ++k) v.push_back(kke::Vertex{ c[faces[f][k]], color, normals[f], { 0, 0 } });
        for (uint32_t k : { 0u, 1u, 2u, 0u, 2u, 3u }) idx.push_back(base + k);
    }
}

// A four-sided limb from a to b, `ra` thick at a and `rb` at b.
void limb(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx, const glm::vec3& a, const glm::vec3& b, float ra, float rb,
          const glm::vec3& color) {
    const glm::vec3 d = b - a;
    if (glm::length(d) < 1e-5f) return;
    const glm::vec3 axis = glm::normalize(d);
    const glm::vec3 side = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 up = glm::cross(side, axis);
    const glm::vec3 around[4] = { side, up, -side, -up };
    for (int f = 0; f < 4; ++f) {
        const glm::vec3 p = around[f], q = around[(f + 1) % 4];
        const glm::vec3 n = glm::normalize(p + q);
        const uint32_t base = static_cast<uint32_t>(v.size());
        v.push_back(kke::Vertex{ a + p * ra, color, n, { 0, 0 } });
        v.push_back(kke::Vertex{ a + q * ra, color, n, { 0, 0 } });
        v.push_back(kke::Vertex{ b + q * rb, color, n, { 0, 0 } });
        v.push_back(kke::Vertex{ b + p * rb, color, n, { 0, 0 } });
        for (uint32_t k : { 0u, 2u, 1u, 0u, 3u, 2u }) idx.push_back(base + k);
    }
}

// The clip named exactly `name` (FBX names may carry an "Armature|" prefix), or -1.
int clipNamed(const kke::AnimationSet& set, const std::string& name) {
    for (size_t i = 0; i < set.clipCount(); ++i) {
        const std::string& n = set.clipName(static_cast<int>(i));
        if (n == name || (n.size() > name.size() && n.compare(n.size() - name.size() - 1, std::string::npos, "|" + name) == 0))
            return static_cast<int>(i);
    }
    return -1;
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// The mannequin remodelled in an A-pose: each upper arm turned 45 degrees
// down at rest. The skin keeps its T-pose binding, so drawn at rest the
// arms hang down, the way an A-pose character's do.
kke::ModelData aPoseOf(const kke::ModelData& model) {
    kke::ModelData out = model;
    out.animations.clear();
    std::vector<glm::mat4> world(out.bones.size());
    for (size_t b = 0; b < out.bones.size(); ++b)
        world[b] = out.bones[b].parent >= 0 ? world[static_cast<size_t>(out.bones[b].parent)] * out.bones[b].localRest : out.bones[b].localRest;
    for (size_t b = 0; b < out.bones.size(); ++b) {
        const std::string name = kke::canonicalBoneName(out.bones[b].name);
        if (name != "upperarm_l" && name != "upperarm_r") continue;
        int child = -1;
        for (size_t c = 0; c < out.bones.size(); ++c)
            if (out.bones[c].parent == static_cast<int>(b)) child = static_cast<int>(c);
        if (child < 0 || out.bones[b].parent < 0) continue;
        const glm::vec3 along = positionOf(world[static_cast<size_t>(child)]) - positionOf(world[b]);
        const glm::vec3 axis = glm::cross(along, glm::vec3(0, -1, 0));
        if (glm::length(axis) < 1e-5f) continue;
        glm::mat4 turned = glm::mat4_cast(glm::angleAxis(glm::radians(45.0f), glm::normalize(axis))) * glm::mat4(glm::mat3(world[b]));
        turned[3] = world[b][3];
        out.bones[b].localRest = glm::inverse(world[static_cast<size_t>(out.bones[b].parent)]) * turned;
    }
    return out;
}

} // namespace

void AnimDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_arms = std::make_unique<kke::DynamicMeshRenderer>(app);
    if (const char* move = std::getenv("KKE_ANIM_MOVE")) {
        m_forced = std::sscanf(move, "%f,%f", &m_forcedMove.x, &m_forcedMove.y) == 2;
    }
    if (const char* circle = std::getenv("KKE_ANIM_CIRCLE")) m_circle = std::max(0.0f, static_cast<float>(std::atof(circle)));
    if (const char* rest = std::getenv("KKE_ANIM_REST")) m_showRest = rest[0] == '1';
    if (const char* mirror = std::getenv("KKE_ANIM_MIRROR")) m_mirrorFloor = mirror[0] == '1';
    if (auto* in = app.getModule<kke::InputModule>()) {
        kke::InputModule::defineCharacterActions(in->map(0));
        in->commitDefaults();
    }
    buildScenery();
    buildTentacles();
    loadCharacters();
    buildPanel();
}

void AnimDemoModule::loadCharacters() {
    if (!m_models) return;
    auto log = kke::log::get(name());
    // Quaternius' Universal Animation Library (CC0): the mannequin and its
    // clips; UAL2 beside it (or as its pack) adds the eight-way walk and run.
    const char* base = SDL_GetBasePath();
    const std::string exe = base ? base : "";
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, exe);
    const std::string ual = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    std::error_code ec;
    if (ual.empty() || !std::filesystem::exists(ual, ec)) {
        m_status = "assets/animations/UAL1_Standard.fbx not found: only the tentacles play.";
        log->warn("{}", m_status);
        return;
    }
    const kke::ModelModule::ModelId model = m_models->load(ual);
    const kke::ModelData* d = model ? m_models->model(model) : nullptr;
    if (!d || d->animations.empty()) {
        m_status = "UAL1_Standard.fbx has no clips: only the tentacles play.";
        log->warn("{}", m_status);
        return;
    }
    m_rig = kke::ModelData{};
    m_rig.bones = d->bones;
    m_rig.animations = d->animations;
    std::string ual2 = (std::filesystem::path(dir) / "UAL2.fbx").string();
    if (!std::filesystem::exists(ual2, ec)) ual2 = kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", exe);
    if (!ual2.empty()) {
        if (const kke::ModelData* d2 = m_models->model(m_models->load(ual2))) kke::appendClipsByBoneName(m_rig, *d2);
    } else {
        log->info("UAL2.fbx not found: the mannequin turns to walk forward (no strafing)");
    }
    m_set = std::make_unique<kke::AnimationSet>(m_rig);
    m_anim = std::make_unique<kke::Animator>(*m_set);
    buildBlendSpace();
    m_hero = m_models->spawn(model);

    // The A-pose copies, drawn with the same mesh.
    m_aPose = aPoseOf(*d);
    const kke::ModelModule::ModelId aPose = m_models->add(m_aPose, "anim_demo:ual_a_pose");
    const kke::BoneMatch match = kke::matchBones(m_rig, m_aPose);
    for (int i = 0; i < 2; ++i) {
        m_retarget[i] = kke::PoseRetarget(m_rig, m_aPose, match, i == 1);
        m_copy[i] = m_models->spawn(aPose, glm::translate(glm::mat4(1.0f), kCopyAt[i]));
    }
    log->info("rest matching turns the A-pose copy's limbs by up to {:.1f} degrees", m_retarget[1].restDifferenceDegrees());
}

void AnimDemoModule::buildBlendSpace() {
    const kke::AnimationSet& s = *m_set;
    const int idle = clipNamed(s, "Idle_Loop"), jog = clipNamed(s, "Jog_Fwd_Loop");
    // UAL2's walk in eight directions round idle, and UAL1's jog straight
    // ahead: strafing and backing off walk, going forward can run.
    const char* dirs[8] = { "Fwd", "Fwd_R", "R", "Bwd_R", "Bwd", "Bwd_L", "L", "Fwd_L" };
    std::vector<kke::BlendSpace2D::Point> points;
    bool all = idle >= 0 && jog >= 0;
    if (all) points.push_back({ idle, { 0.0f, 0.0f } });
    for (int k = 0; k < 8 && all; ++k) {
        const std::string clip = std::string("Walk_") + dirs[k] + "_Loop";
        const int c = clipNamed(s, clip);
        if (c < 0) {
            kke::log::get(name())->info("no clip '{}': the mannequin turns to walk forward (no strafing)", clip);
            all = false;
            break;
        }
        const float a = glm::radians(45.0f * static_cast<float>(k));
        points.push_back({ c, { std::sin(a) * m_walkSpeed, std::cos(a) * m_walkSpeed } });
    }
    if (all) points.push_back({ jog, { 0.0f, m_runSpeed } });
    m_directions = all;
    if (!all) {
        // UAL1 alone: forward only (idle, walk, jog), and the mannequin turns to face where it goes.
        points.clear();
        for (auto [clip, speed] : { std::pair{ "Idle_Loop", 0.0f }, std::pair{ "Walk_Loop", m_walkSpeed }, std::pair{ "Jog_Fwd_Loop", m_runSpeed } }) {
            const int c = clipNamed(s, clip);
            if (c >= 0) points.push_back({ c, { 0.0f, speed } });
        }
    }
    m_space.points = points;
    if (points.empty()) return;
    m_state = m_anim->addBlendState2D("move", m_space);
    m_anim->play(m_state, 0.0f);
}

void AnimDemoModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    m_clock += dt;
    steer(dt);
    if (m_anim && m_hero) {
        m_anim->setParameter2(m_param);
        m_anim->update(dt);
        const glm::mat4 at = glm::rotate(glm::translate(glm::mat4(1.0f), m_position), glm::radians(m_yaw), glm::vec3(0, 1, 0));
        m_models->setTransform(m_hero, at);
        if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_hero)) kke::poseToLocals(shownPose(), *locals);
        copyPose();
    }
    if (auto* orbit = m_app->getModule<kke::OrbitCameraModule>()) orbit->setTarget(m_position + glm::vec3(0.0f, 1.0f, 0.0f));
    // Each tentacle reaches for the mannequin's head when it is near
    // enough, else it sways about over its root.
    const glm::vec3 head = m_position + glm::vec3(0.0f, 1.65f, 0.0f);
    for (Tentacle& t : m_tentacles) {
        const float phase = t.ccd ? 1.7f : 0.0f;
        const glm::vec3 sway = t.base + glm::vec3(0.9f * std::sin(0.8f * m_clock + phase), 1.7f + 0.4f * std::sin(1.3f * m_clock + phase),
                                                  0.9f * std::cos(0.6f * m_clock + phase));
        reach(t, glm::length(head - t.base) < kSegment * (kJoints - 1) * 0.95f ? head : sway);
    }
    buildTentacles();
}

void AnimDemoModule::steer(float dt) {
    glm::vec2 stick = m_forcedMove;
    if (m_circle > 0.0f) {
        // A check's stick: held at the given length, turning round once every m_circle seconds.
        const float a = 6.2831853f * m_clock / m_circle;
        stick = glm::length(m_forcedMove) * glm::vec2(std::sin(a), std::cos(a));
    }
    bool slow = false;
    if (!m_forced) {
        if (auto* in = m_app->getModule<kke::InputModule>()) {
            stick = in->map(0).axis2("move");
            slow = in->map(0).held("walk");
        }
    }
    if (glm::length(stick) > 1.0f) stick = glm::normalize(stick);
    // Camera-relative: the mannequin faces where the camera looks.
    const kke::Camera& cam = m_app->camera();
    glm::vec3 look = cam.target - cam.position;
    look.y = 0.0f;
    look = glm::length(look) > 1e-4f ? glm::normalize(look) : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right(-look.z, 0.0f, look.x);
    // Ahead it runs; sideways and back it walks (the clips UAL has), in between it eases from one to the other.
    const float ahead = glm::length(stick) > 1e-3f ? std::max(0.0f, stick.y / glm::length(stick)) : 0.0f;
    const float top = slow ? m_walkSpeed : (m_directions ? m_walkSpeed + (m_runSpeed - m_walkSpeed) * ahead * ahead : m_runSpeed);
    const glm::vec3 want = (right * stick.x + look * stick.y) * top;
    // Speeds up and slows down over a quarter of a second or so.
    m_velocity += (want - m_velocity) * (1.0f - std::exp(-6.0f * dt));
    m_position += m_velocity * dt;
    const glm::vec2 flat(m_position.x, m_position.z);
    if (glm::length(flat) > kArena) {
        const glm::vec2 in = glm::normalize(flat) * kArena;
        m_position = glm::vec3(in.x, 0.0f, in.y);
    }
    // The mannequin faces -Z at yaw 0.
    glm::vec3 facing = look;
    if (!m_directions) {
        // Forward clips only: turn to the way it goes.
        const glm::vec3 v(m_velocity.x, 0.0f, m_velocity.z);
        facing = glm::length(v) > 0.2f ? glm::normalize(v) : glm::vec3(-std::sin(glm::radians(m_yaw)), 0.0f, -std::cos(glm::radians(m_yaw)));
    }
    m_yaw = glm::degrees(std::atan2(-facing.x, -facing.z));
    const glm::vec3 side(-facing.z, 0.0f, facing.x);
    m_param = glm::vec2(glm::dot(m_velocity, side), glm::dot(m_velocity, facing));
}

const kke::Pose& AnimDemoModule::shownPose() const { return m_showRest ? m_set->restPose() : m_anim->pose(); }

void AnimDemoModule::copyPose() {
    std::vector<glm::mat4> hero;
    kke::poseToLocals(shownPose(), hero);
    for (int i = 0; i < 2; ++i) {
        if (!m_copy[i] || !m_retarget[i].valid()) continue;
        if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_copy[i])) m_retarget[i].apply(hero, *locals);
        const glm::mat4 at = glm::rotate(glm::translate(glm::mat4(1.0f), kCopyAt[i]), glm::radians(m_yaw), glm::vec3(0, 1, 0));
        m_models->setTransform(m_copy[i], at);
    }
}

void AnimDemoModule::reach(Tentacle& t, const glm::vec3& target) {
    if (t.ccd) {
        kke::CcdSettings s;
        s.maxStepDegrees = m_damping;
        s.maxBendDegrees.assign(static_cast<size_t>(kJoints), m_bendLimit);
        s.maxBendDegrees[0] = 90.0f; // the base may lean over a lot
        kke::ccdPoints(t.points, target, s);
    } else {
        kke::fabrikPoints(t.points, target);
    }
    // The sharpest bend, measured the same way for both.
    t.worstBend = 0.0f;
    for (size_t i = 1; i + 1 < t.points.size(); ++i) {
        const glm::vec3 a = glm::normalize(t.points[i] - t.points[i - 1]), b = glm::normalize(t.points[i + 1] - t.points[i]);
        t.worstBend = std::max(t.worstBend, glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f))));
    }
}

void AnimDemoModule::buildScenery() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    box(v, idx, glm::vec3(-9.0f, -0.2f, -9.0f), glm::vec3(9.0f, 0.0f, 9.0f), glm::vec3(0.32f, 0.34f, 0.36f));
    // Plinths under the A-pose copies and the tentacles' roots.
    for (const glm::vec3& c : kCopyAt) box(v, idx, c - glm::vec3(0.4f, 0.0f, 0.4f), c + glm::vec3(0.4f, 0.05f, 0.4f), glm::vec3(0.45f, 0.4f, 0.35f));
    m_tentacles[0].base = glm::vec3(2.4f, 0.0f, -1.6f);
    m_tentacles[1].base = glm::vec3(2.4f, 0.0f, 1.6f);
    m_tentacles[1].ccd = true;
    for (Tentacle& t : m_tentacles) {
        box(v, idx, t.base - glm::vec3(0.3f, 0.0f, 0.3f), t.base + glm::vec3(0.3f, 0.12f, 0.3f), glm::vec3(0.3f, 0.25f, 0.3f));
        t.base.y = 0.12f;
        t.points.clear();
        for (int i = 0; i < kJoints; ++i) t.points.push_back(t.base + glm::vec3(0.0f, kSegment * static_cast<float>(i), 0.0f));
    }
    m_ground->upload(v, idx);
}

void AnimDemoModule::buildTentacles() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    for (const Tentacle& t : m_tentacles) {
        for (size_t i = 0; i + 1 < t.points.size(); ++i) {
            // Joints past the bend limit show red; the CCD one never gets there.
            float bend = 0.0f;
            if (i > 0) {
                const glm::vec3 a = glm::normalize(t.points[i] - t.points[i - 1]), b = glm::normalize(t.points[i + 1] - t.points[i]);
                bend = glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f)));
            }
            const float over = std::clamp((bend - m_bendLimit) / 20.0f, 0.0f, 1.0f);
            const glm::vec3 skin = t.ccd ? glm::vec3(0.45f, 0.25f, 0.6f) : glm::vec3(0.25f, 0.5f, 0.45f);
            const float along = static_cast<float>(i) / static_cast<float>(t.points.size() - 1);
            limb(v, idx, t.points[i], t.points[i + 1], 0.09f * (1.0f - 0.7f * along), 0.09f * (1.0f - 0.7f * (along + 1.0f / kJoints)),
                 glm::mix(skin, glm::vec3(0.9f, 0.15f, 0.1f), over));
        }
    }
    m_arms->upload(v, idx);
}

void AnimDemoModule::render(const kke::RenderContext& ctx) {
    m_ground->draw(ctx, glm::mat4(1.0f), m_mirrorFloor ? 0.6f : 0.0f, m_mirrorFloor ? 0.04f : 0.9f); // polished: brushed metal
    m_arms->draw(ctx, glm::mat4(1.0f), 0.0f, 0.5f);
}

void AnimDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_ground->drawShadow(ctx);
    m_arms->drawShadow(ctx);
}

std::string AnimDemoModule::weightRow(int row) const {
    if (m_space.points.empty() || !m_set) return row < 0 ? m_status : std::string();
    char buf[96];
    if (row < 0) {
        std::snprintf(buf, sizeof(buf), "Sideways %+.1f m/s, forward %+.1f m/s", static_cast<double>(m_param.x), static_cast<double>(m_param.y));
        return buf;
    }
    // The clips weighted now, heaviest first.
    std::vector<float> w;
    kke::blendSpace2DWeights(m_space, m_param, w);
    std::vector<std::pair<float, int>> on;
    for (size_t i = 0; i < w.size(); ++i)
        if (w[i] > 0.01f) on.push_back({ w[i], m_space.points[i].clip });
    std::sort(on.begin(), on.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (static_cast<size_t>(row) >= on.size()) return std::string();
    const std::string& clip = m_set->clipName(on[static_cast<size_t>(row)].second);
    const size_t bar = clip.rfind('|');
    std::snprintf(buf, sizeof(buf), "%3.0f%%  %-20s %s", static_cast<double>(on[static_cast<size_t>(row)].first * 100.0f),
                  std::string(static_cast<size_t>(on[static_cast<size_t>(row)].first * 20.0f + 0.5f), '|').c_str(),
                  clip.substr(bar == std::string::npos ? 0 : bar + 1).c_str());
    return buf;
}

// The settings (RmlUi, kke::DemoPanelModule): a controller opens them with
// View, the keyboard with F3, the mouse just clicks.
void AnimDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& blend = panel->section("2D blend space");
    blend.text("Move with WASD or the left stick; the mannequin faces where the camera looks. Hold walk to walk.");
    blend.text([this] { return weightRow(-1); });
    for (int row = 0; row < 4; ++row) blend.text([this, row] { return weightRow(row); }).showIf([this, row] { return !weightRow(row).empty(); });
    blend.note("One kke::BlendSpace2D over idle, UAL2's walk in eight directions and a forward jog: gradient band weights, "
               "smooth as the stick turns, every clip at one shared phase so the feet never slide.");
    blend.text("No eight-way walk (UAL2.fbx): the mannequin turns to walk forward instead.").showIf([this] { return m_anim && !m_directions; });

    auto& rest = panel->section("Rest matching");
    rest.text("The two figures behind copy the mannequin's pose (kke::PoseRetarget). They were modelled in an A-pose.");
    rest.text([this] {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Left: rest matching off, the arms stay 45 degrees low.\nRight: on, limbs turned by up to %.0f degrees at rest.",
                      static_cast<double>(m_retarget[1].restDifferenceDegrees()));
        return std::string(buf);
    }).showIf([this] { return m_copy[0] != 0; });
    rest.toggle("Show the rest pose (the clips' T-pose)", &m_showRest);

    auto& reachSection = panel->section("Tentacles");
    reachSection.text("Both reach for the mannequin's head when it comes near: FABRIK (green) and CCD with bend limits (purple).");
    reachSection.slider("Bend limit", &m_bendLimit, 8.0f, 60.0f, "%.0f deg a joint", {}, 1.0f);
    reachSection.slider("Damping", &m_damping, 1.0f, 30.0f, "%.0f deg a pass", {}, 1.0f);
    reachSection.text([this] {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Sharpest bend: FABRIK %.0f deg, CCD %.0f deg", static_cast<double>(m_tentacles[0].worstBend),
                      static_cast<double>(m_tentacles[1].worstBend));
        return std::string(buf);
    });
    reachSection.note("Joints bent past the limit turn red. FABRIK has no limits; CCD keeps every joint inside its own, and "
                      "damping spreads the bend along the tentacle instead of folding it at the tip.");

    auto& look = panel->section("Floor");
    look.toggle("Polished floor (shows reflections: settings, Graphics, Reflections)", &m_mirrorFloor);
}

} // namespace kke_anim
