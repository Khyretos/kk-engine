// The people (Body.h): the UAL mannequin with its walk, jog and sprint
// clips (UAL 2's side steps and cheers when there), a racket in the right
// hand and procedural swings: two-bone IK takes the racket hand along a
// path through the ball, so every shot meets the ball where it is.

#include "Body.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace tennis {

namespace fs = std::filesystem;

namespace {

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

// A box turned about Z (the racket's face normal): for the frame's ring.
void appendTiltedBox(const glm::vec3& center, const glm::vec3& half, float angle, const glm::vec3& color, std::vector<kke::Vertex>& v,
                     std::vector<uint32_t>& idx) {
    const size_t first = v.size();
    appendBox(glm::vec3(0.0f), half, color, v, idx);
    const float c = std::cos(angle), s = std::sin(angle);
    for (size_t i = first; i < v.size(); ++i) {
        glm::vec3& p = v[i].position;
        glm::vec3& n = v[i].normal;
        p = glm::vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z) + center;
        n = glm::vec3(c * n.x - s * n.y, s * n.x + c * n.y, n.z);
    }
}

// Body frame (x right, y up, z forward) -> the mannequin's model space.
struct Frame {
    glm::vec3 right, up{ 0.0f, 1.0f, 0.0f }, fwd;
    glm::vec3 toModel(const glm::vec3& b) const { return right * b.x + up * b.y + fwd * b.z; }
};

float ease(float u) { return u * u * (3.0f - 2.0f * u); }

} // namespace

// ------------------------------------------------------------------ Rig

Rig::Rig(kke::Application& app) : m_app(app) {
    // The fallback body: a torso and a dark visor on the front (-Z).
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.25f, 0.9f, 0.18f }, glm::vec3(0.8f), v, idx);
    appendBox({ 0.0f, 1.55f, -0.18f }, { 0.18f, 0.07f, 0.03f }, glm::vec3(0.1f), v, idx);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_block->upload(v, idx);

    // The racket, grip at the origin, shaft along +Y, strings facing +Z:
    // a 69 cm frame with an oval head.
    v.clear();
    idx.clear();
    const glm::vec3 grip(0.12f, 0.12f, 0.14f), frame(0.9f, 0.9f, 0.92f), accent(0.85f, 0.2f, 0.15f), strings(0.93f, 0.93f, 0.86f);
    appendBox({ 0.0f, 0.1f, 0.0f }, { 0.016f, 0.1f, 0.013f }, grip, v, idx);
    appendTiltedBox({ -0.045f, 0.245f, 0.0f }, { 0.008f, 0.06f, 0.009f }, 0.45f, accent, v, idx);
    appendTiltedBox({ 0.045f, 0.245f, 0.0f }, { 0.008f, 0.06f, 0.009f }, -0.45f, accent, v, idx);
    const glm::vec2 centre(0.0f, 0.46f), radii(0.125f, 0.165f);
    constexpr int kSegments = 20;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSegments, a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
        const glm::vec2 p0 = centre + radii * glm::vec2(std::cos(a0), std::sin(a0));
        const glm::vec2 p1 = centre + radii * glm::vec2(std::cos(a1), std::sin(a1));
        const glm::vec2 mid = (p0 + p1) * 0.5f, d = p1 - p0;
        appendTiltedBox({ mid.x, mid.y, 0.0f }, { 0.5f * glm::length(d) + 0.004f, 0.009f, 0.011f }, std::atan2(d.y, d.x), frame, v, idx);
    }
    appendBox({ centre.x, centre.y, 0.0f }, { radii.x * 0.93f, radii.y * 0.93f, 0.002f }, strings, v, idx);
    m_racket = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_racket->upload(v, idx);
}

Rig::~Rig() = default;

bool Rig::load(kke::ModelModule& models) {
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string ual1 = dir.empty() ? std::string() : (fs::path(dir) / "UAL1_Standard.fbx").string();
    std::error_code ec;
    if (ual1.empty() || !fs::exists(ual1, ec)) {
        kke::log::get("Tennis")->info("animation library not found (assets/animations/UAL1_Standard.fbx): the people are blocks");
        return false;
    }
    const kke::ModelModule::ModelId loaded = models.load(ual1);
    const kke::ModelData* d = loaded ? models.model(loaded) : nullptr;
    if (!d || d->bones.empty() || d->animations.empty()) return false;
    // A light grey copy takes each person's colour (the tint multiplies);
    // the joints stay dark.
    kke::ModelData grey = *d;
    for (kke::ModelMaterial& m : grey.materials) m.baseColor = m.name.find("Joint") != std::string::npos ? glm::vec3(0.12f) : glm::vec3(0.78f);
    m_model = models.add(std::move(grey), "tennis/person");
    d = models.model(m_model);
    m_rig = kke::ModelData{};
    m_rig.bones = d->bones;
    m_rig.animations = d->animations;
    // UAL 2 (side steps, cheers): next to UAL 1, or the pack under KKE_ASSETS_DIR.
    const char* assets = std::getenv("KKE_ASSETS_DIR");
    for (const std::string& ual2 : { (fs::path(dir) / "UAL2.fbx").string(),
                                     assets ? (fs::path(assets) / "Universal Animation Library 2" / "Unity" / "UAL2.fbx").string() : std::string() }) {
        if (ual2.empty() || !fs::exists(ual2, ec)) continue;
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::appendClipsByBoneName(m_rig, kke::loadModel(ual2, o));
            m_sideSteps = true;
        } catch (const std::exception& e) {
            kke::log::get("Tennis")->warn("UAL2.fbx: {}", e.what());
        }
        break;
    }
    if (!m_sideSteps)
        kke::log::get("Tennis")->info("UAL2.fbx not found (assets/animations, or 'Universal Animation Library 2' under KKE_ASSETS_DIR): "
                                      "players turn to run sideways instead of side-stepping");
    m_set = std::make_unique<kke::AnimationSet>(m_rig);
    m_arm[0] = kke::findChain(m_rig, "upperarm_l", "lowerarm_l", "hand_l");
    m_arm[1] = kke::findChain(m_rig, "upperarm_r", "lowerarm_r", "hand_r");
    const glm::vec3 fwd = kke::modelForward(m_rig);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    return true;
}

// ------------------------------------------------------------------ Body

Body::Body(Rig& rig, kke::ModelModule& models, const glm::vec3& tint, bool racket)
    : m_rig(rig), m_models(models), m_hasRacket(racket), m_tint(tint) {
    if (!rig.loaded()) return;
    m_instance = models.spawn(rig.model());
    models.setOverlayEnabled(m_instance, false);
    models.setTint(m_instance, tint);
    m_anim = std::make_unique<kke::Animator>(rig.set());
    const kke::AnimationSet& s = rig.set();
    auto pick = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (int c = s.find(n); c >= 0) return c;
        return -1;
    };
    m_st.idle = m_anim->addBlendState("move", { { { pick({ "|Idle_Loop" }), 0.0f },
                                                  { pick({ "|Walk_Loop" }), 1.6f },
                                                  { pick({ "|Jog_Fwd_Loop" }), 3.6f },
                                                  { pick({ "|Sprint_Loop" }), 6.2f } } });
    if (rig.sideSteps()) {
        m_st.left = m_anim->addClipState("left", pick({ "|Walk_L_Loop" }), true, 1.8f);
        m_st.right = m_anim->addClipState("right", pick({ "|Walk_R_Loop" }), true, 1.8f);
        m_st.back = m_anim->addClipState("back", pick({ "|Walk_Bwd_Loop" }), true, 1.6f);
    }
    m_st.sit = m_anim->addClipState("sit", pick({ "|Sitting_Idle_Loop", "|Sitting_Talking_Loop" }), true);
    m_st.cheer = m_anim->addClipState("cheer", pick({ "|Yes", "|Dance_Loop" }), true);
    m_st.clap = m_anim->addClipState("clap", pick({ "|Idle_Rail_Call", "|Dance_Loop", "|Yes" }), true);
    m_st.groan = m_anim->addClipState("groan", pick({ "|Idle_No_Loop", "|Surprise", "|Idle_Talking_Loop" }), true);
    m_anim->play(m_st.idle, 0.0f);
}

Body::~Body() {
    if (m_instance) m_models.remove(m_instance);
}

void Body::setTint(const glm::vec3& tint) {
    m_tint = tint;
    if (m_instance) m_models.setTint(m_instance, tint);
}

void Body::setVisible(bool visible) {
    m_visible = visible;
    if (m_instance) m_models.setVisible(m_instance, visible);
}

void Body::update(const glm::vec3& feet, float yawDegrees, const glm::vec3& velocity, const SwingPose& swing, Mood mood, float dt) {
    const float yawR = glm::radians(yawDegrees);
    const glm::vec3 facing(std::sin(yawR), 0.0f, std::cos(yawR));
    const float turn = glm::degrees(std::atan2(facing.x, -facing.z)); // 0 = facing -Z
    const glm::vec3 worldRight = glm::normalize(glm::cross(facing, glm::vec3(0, 1, 0)));
    if (!m_instance || !m_anim) {
        m_xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-turn), glm::vec3(0, 1, 0));
        // Blocks still hold a racket: out to the right, head up.
        const glm::vec3 hand = feet + glm::vec3(0.0f, 1.0f, 0.0f) + worldRight * 0.35f + facing * 0.25f;
        m_racketWorld = glm::mat4(glm::vec4(worldRight * -1.0f, 0.0f), glm::vec4(0, 1, 0, 0), glm::vec4(facing, 0.0f), glm::vec4(hand, 1.0f));
        m_tossHand = feet + glm::vec3(0.0f, 1.2f, 0.0f) - worldRight * 0.3f + facing * 0.3f;
        return;
    }
    m_xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_rig.modelYaw() - turn), glm::vec3(0, 1, 0));
    m_models.setTransform(m_instance, m_xf);

    // Legs: the move blend by speed; while playing, sideways and backwards
    // steps keep the body facing the net (UAL 2), otherwise it turns.
    kke::Animator& a = *m_anim;
    const glm::vec3 flatV(velocity.x, 0.0f, velocity.z);
    const float speed = glm::length(flatV);
    int want = m_st.idle;
    switch (mood) {
    case Mood::Sit: want = m_st.sit; break;
    case Mood::Cheer: want = m_st.cheer; break;
    case Mood::Groan: want = m_st.groan; break;
    case Mood::Stand: break;
    case Mood::Play:
        if (speed > 0.6f && m_st.left >= 0 && speed < 4.5f) {
            const float side = glm::dot(flatV, worldRight), fwd = glm::dot(flatV, facing);
            if (std::abs(side) > std::abs(fwd) * 1.2f) want = side > 0.0f ? m_st.right : m_st.left;
            else if (fwd < -0.6f) want = m_st.back;
        }
        break;
    }
    if (want < 0) want = m_st.idle;
    if (a.current() != want) a.play(want, 0.2f);
    a.setParameter(speed);
    a.update(dt);
    kke::Pose pose = a.pose();

    // Arms: the swing (right arm) and the toss (left arm), in the body's frame.
    const kke::ModelData& rig = m_rig.data();
    const glm::vec3 fwdM = kke::modelForward(rig);
    Frame fr{ glm::normalize(glm::cross(fwdM, glm::vec3(0, 1, 0))), glm::vec3(0, 1, 0), fwdM };
    const std::vector<glm::mat4> bones = kke::poseToModel(rig, pose);
    const kke::TwoBoneChain& rArm = m_rig.arm(1);
    const kke::TwoBoneChain& lArm = m_rig.arm(0);
    glm::vec3 handBody(0.3f, 1.05f, 0.35f), shaftBody(0.15f, 0.9f, 0.4f);
    if (m_hasRacket && rArm.valid() && mood == Mood::Play) {
        const glm::vec3 shoulderM(bones[static_cast<size_t>(rArm.upper)][3]);
        const glm::vec3 shoulder(glm::dot(shoulderM, fr.right), shoulderM.y, glm::dot(shoulderM, fr.fwd));
        // The racket head's centre is ~0.46 m up the shaft from the hand.
        auto handFor = [&](const glm::vec3& head, glm::vec3& shaft) {
            glm::vec3 dir = head - shoulder;
            dir.y = dir.y * 0.4f + 0.25f; // the racket stays nearer level than the arm
            shaft = glm::normalize(dir);
            return head - shaft * 0.46f;
        };
        using K = SwingPose::Kind;
        const float t = std::clamp(swing.t, -1.0f, 1.0f);
        glm::vec3 back(0.0f), follow(0.0f), bump(0.0f);
        glm::vec3 contactHead = swing.contact;
        switch (swing.kind) {
        case K::Forehand:
            back = { 0.6f, 1.2f, -0.5f };
            follow = { -0.45f, 1.5f, 0.35f };
            bump = { 0.1f, 0.0f, 0.3f };
            break;
        case K::Backhand:
            back = { -0.55f, 1.15f, -0.3f };
            follow = { 0.55f, 1.55f, 0.25f };
            bump = { -0.05f, 0.0f, 0.3f };
            break;
        case K::Serve:
        case K::Toss:
            back = { 0.3f, 1.55f, -0.45f };
            follow = { -0.4f, 0.8f, 0.4f };
            bump = { 0.1f, 0.3f, 0.1f };
            if (glm::length(contactHead) < 0.01f) contactHead = { 0.25f, 2.55f, 0.4f };
            break;
        case K::Ready:
            break;
        }
        glm::vec3 head;
        if (swing.kind == K::Ready) {
            head = glm::vec3(0.12f, 1.4f, 0.5f);
        } else if (swing.kind == K::Toss) {
            head = back + glm::vec3(0.0f, 0.4f, 0.0f);
        } else if (t <= 0.0f) {
            const float u = ease(t + 1.0f);
            head = glm::mix(back, contactHead, u) + bump * (4.0f * u * (1.0f - u));
        } else {
            const float u = ease(t);
            head = glm::mix(contactHead, follow, u) + bump * (2.0f * u * (1.0f - u));
        }
        handBody = handFor(head, shaftBody);
        const glm::vec3 pole = fr.toModel(glm::vec3(0.6f, 0.6f, -0.4f));
        kke::solveTwoBone(rig, pose, rArm, fr.toModel(handBody), pole, 1.0f);
    }
    // The toss: the left hand up in front of the head.
    glm::vec3 tossBody(-0.25f, 1.15f, 0.35f);
    if (swing.kind == SwingPose::Kind::Toss && lArm.valid() && mood == Mood::Play) {
        tossBody = glm::vec3(-0.05f, 2.05f, 0.4f);
        kke::solveTwoBone(rig, pose, lArm, fr.toModel(tossBody), fr.toModel(glm::vec3(-0.6f, 1.0f, -0.3f)), 1.0f);
    }
    if (std::vector<glm::mat4>* locals = m_models.boneLocals(m_instance)) kke::poseToLocals(pose, *locals);

    // The racket in the world: from the hand the IK reached, along the shaft.
    const glm::mat4 bodyToWorld = glm::mat4(glm::vec4(worldRight, 0.0f), glm::vec4(0, 1, 0, 0), glm::vec4(facing, 0.0f), glm::vec4(feet, 1.0f));
    glm::vec3 handWorld = glm::vec3(bodyToWorld * glm::vec4(handBody, 1.0f));
    if (rArm.valid()) {
        const std::vector<glm::mat4> posed = kke::poseToModel(rig, pose);
        handWorld = glm::vec3(m_xf * posed[static_cast<size_t>(rArm.end)][3]);
    }
    const glm::vec3 shaft = glm::normalize(glm::vec3(bodyToWorld * glm::vec4(shaftBody, 0.0f)));
    glm::vec3 face = facing - shaft * glm::dot(facing, shaft);
    face = glm::length(face) > 1e-3f ? glm::normalize(face) : glm::vec3(0, 1, 0);
    const glm::vec3 x = glm::cross(shaft, face);
    m_racketWorld = glm::mat4(glm::vec4(x, 0.0f), glm::vec4(shaft, 0.0f), glm::vec4(face, 0.0f), glm::vec4(handWorld, 1.0f));
    m_tossHand = glm::vec3(bodyToWorld * glm::vec4(tossBody + glm::vec3(0.0f, 0.08f, 0.0f), 1.0f));
}

void Body::render(const kke::RenderContext& ctx) {
    if (!m_visible) return;
    if (!m_instance) m_rig.block().draw(ctx, m_xf, 0.0f, 0.6f);
    if (m_hasRacket) m_rig.racketMesh().draw(ctx, m_racketWorld, 0.1f, 0.45f);
}

void Body::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (!m_visible) return;
    if (!m_instance) m_rig.block().drawShadow(ctx, m_xf);
    if (m_hasRacket) m_rig.racketMesh().drawShadow(ctx, m_racketWorld);
}

} // namespace tennis
