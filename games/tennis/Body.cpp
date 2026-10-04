// The people (Body.h): the UAL mannequin with its walk, jog and sprint
// clips (UAL 2's side steps and cheers when there), a racket in the right
// hand and procedural swings: two-bone IK takes the racket hand along a
// path through the ball, so every shot meets the ball where it is.

#include "Body.h"

#include "kke/AnimRig.h"
#include "kke/KnownPacks.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/PhysicsModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

// The strings' look: 2.5 mm ribbons, a pale synthetic gut.
void uploadStrings(const StringLayout& layout, const std::vector<glm::vec3>& points, kke::DynamicMeshRenderer& mesh) {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    layout.mesh(points, 0.0025f, glm::vec3(0.93f, 0.92f, 0.82f), v, idx);
    mesh.upload(v, idx);
}

// Body frame (x right, y up, z forward) -> the mannequin's model space.
struct Frame {
    glm::vec3 right, up{ 0.0f, 1.0f, 0.0f }, fwd;
    glm::vec3 toModel(const glm::vec3& b) const { return right * b.x + up * b.y + fwd * b.z; }
};

glm::quat rotationOf(const glm::mat4& m) {
    const glm::mat3 r(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])));
    return glm::normalize(glm::quat_cast(r));
}

// Turns a bone by `q` (model space), children and all.
void turnBone(const kke::ModelData& rig, kke::Pose& pose, int bone, const glm::quat& q) {
    if (bone < 0 || static_cast<size_t>(bone) >= pose.size()) return;
    const int parent = rig.bones[static_cast<size_t>(bone)].parent;
    glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
    if (parent >= 0) parentRot = rotationOf(kke::poseToModel(rig, pose)[static_cast<size_t>(parent)]);
    pose[static_cast<size_t>(bone)].r = glm::normalize(glm::inverse(parentRot) * q * parentRot * pose[static_cast<size_t>(bone)].r);
}

// A stroke's body: the hips drop by `crouch` m (the knees bend, the feet
// stay where they are) and the shoulders turn `twist` degrees toward the
// racket side, a third of it in the hips.
void bendAndTurn(const Rig& r, kke::Pose& pose, const glm::vec3& up, const glm::vec3& fwd, const glm::vec3& right, float crouch, float twist) {
    const kke::ModelData& rig = r.data();
    const int pelvis = r.pelvis();
    if (pelvis < 0 || (std::abs(crouch) < 1e-3f && std::abs(twist) < 0.1f)) return;
    const std::vector<glm::mat4> before = kke::poseToModel(rig, pose);
    const bool legs = r.leg(0).valid() && r.leg(1).valid();
    glm::vec3 feet[2]{};
    if (legs)
        for (int side = 0; side < 2; ++side) feet[side] = glm::vec3(before[static_cast<size_t>(r.leg(side).end)][3]);
    if (std::abs(twist) > 0.1f) {
        const glm::vec3 axis = glm::normalize(glm::cross(fwd, right)); // turns forward toward the right
        const float a = glm::radians(twist);
        turnBone(rig, pose, pelvis, glm::angleAxis(a * 0.3f, axis));
        for (int s = 0; s < 3; ++s) turnBone(rig, pose, r.spine(s), glm::angleAxis(a * 0.7f / 3.0f, axis));
    }
    if (std::abs(crouch) > 1e-3f) {
        const int parent = rig.bones[static_cast<size_t>(pelvis)].parent;
        const glm::mat4 parentM = parent >= 0 ? before[static_cast<size_t>(parent)] : glm::mat4(1.0f);
        pose[static_cast<size_t>(pelvis)].t += glm::vec3(glm::inverse(parentM) * glm::vec4(-up * crouch, 0.0f));
    }
    // The feet stay where they were (the hips turned and dropped above them).
    if (legs)
        for (int side = 0; side < 2; ++side) {
            const glm::vec3 knee = feet[side] + up * 0.5f + fwd * 0.6f + right * (side == 0 ? -0.15f : 0.15f);
            kke::solveTwoBone(rig, pose, r.leg(side), feet[side], knee, 1.0f);
        }
}

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
    const glm::vec3 grip(0.12f, 0.12f, 0.14f), frame(0.9f, 0.9f, 0.92f), accent(0.85f, 0.2f, 0.15f);
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
    m_racket = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_racket->upload(v, idx);
    m_strings.centre = centre;
    m_strings.radii = radii * 0.93f;
    m_strings.build();
    m_stringsMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    uploadStrings(m_strings, m_strings.points, *m_stringsMesh);
    makeRacketItem();
}

Rig::~Rig() = default;

void Rig::makeRacketItem() {
    // Grips in our racket frame (shaft +Y, strings +Z). The right palm
    // holds the handle 8-9 cm up from the butt with the palm the way the
    // strings face (an eastern forehand grip); the left hand holds it with
    // its palm the other way, above the right (a two-handed backhand) or
    // at the throat.
    kke::Equippable& r = m_racketItem;
    r = kke::Equippable{};
    r.name = "tennis racket";
    r.slots = kke::kBothHands;
    const glm::mat4 flip = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0));
    r.grips.push_back({ "main", glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.035f, 0.0f)), 0.016f, 0.05f });
    r.grips.push_back({ "support", glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.125f, 0.0f)) * flip, 0.016f, 0.045f });
    r.grips.push_back({ "throat", glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_strings.centre.y - m_strings.radii.y - 0.06f, 0.0f)) * flip, 0.02f, 0.03f });
    m_supportGrip = r.grip("support");
    m_throatGrip = r.grip("throat");
    // Its shape: the shaft, and the head's rim as a ring of capsules (the
    // head is flat: one big ball round it would push the arm away from
    // nothing).
    const glm::vec2 c = m_strings.centre, rad = m_strings.radii / 0.93f;
    r.shape.push_back({ { 0.0f, -0.05f, 0.0f }, { 0.0f, c.y - rad.y, 0.0f }, 0.016f });
    constexpr int kRim = 8;
    for (int i = 0; i < kRim; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kRim, a1 = 6.2831853f * static_cast<float>(i + 1) / kRim;
        r.shape.push_back({ glm::vec3(c + rad * glm::vec2(std::cos(a0), std::sin(a0)), 0.0f), glm::vec3(c + rad * glm::vec2(std::cos(a1), std::sin(a1)), 0.0f), 0.012f });
    }
    // Down the middle of the strings, so nothing passes through the bed.
    r.shape.push_back({ glm::vec3(c.x, c.y - rad.y * 0.8f, 0.0f), glm::vec3(c.x, c.y + rad.y * 0.8f, 0.0f), 0.012f });
}

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
    m_base = *d;
    m_people.scan("Tennis");
    kke::ModelData grey = *d;
    for (kke::ModelMaterial& m : grey.materials) m.baseColor = m.name.find("Joint") != std::string::npos ? glm::vec3(0.12f) : glm::vec3(0.78f);
    m_model = models.add(std::move(grey), "tennis/person");
    d = models.model(m_model);
    m_rig = kke::ModelData{};
    m_rig.bones = d->bones;
    m_rig.animations = d->animations;
    // UAL 2 (side steps, cheers): next to UAL 1, or the pack in the asset folder.
    for (const std::string& ual2 : { (fs::path(dir) / "UAL2.fbx").string(),
                                     kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", base ? base : "") }) {
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
        kke::log::get("Tennis")->info("UAL2.fbx not found (assets/animations, or the 'Universal Animation Library 2' pack in the asset folder): "
                                      "players turn to run sideways instead of side-stepping");
    m_set = std::make_unique<kke::AnimationSet>(m_rig);
    m_arm[0] = kke::findChain(m_rig, "upperarm_l", "lowerarm_l", "hand_l");
    m_arm[1] = kke::findChain(m_rig, "upperarm_r", "lowerarm_r", "hand_r");
    m_human[0] = kke::makeHumanArm(m_rig, m_arm[0], m_arm[1]);
    m_human[1] = kke::makeHumanArm(m_rig, m_arm[1], m_arm[0]);
    // The body the arms keep out of, and the palms the racket sits in,
    // from the mannequin's own mesh.
    m_body = kke::BodyShape::fit(*d);
    m_equip = kke::Equipment(*d);
    m_leg[0] = kke::findChain(m_rig, "thigh_l", "calf_l", "foot_l");
    m_leg[1] = kke::findChain(m_rig, "thigh_r", "calf_r", "foot_r");
    m_pelvis = m_rig.findBone("pelvis");
    m_spine[0] = m_rig.findBone("spine_01");
    m_spine[1] = m_rig.findBone("spine_02");
    m_spine[2] = m_rig.findBone("spine_03");
    const glm::vec3 fwd = kke::modelForward(m_rig);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    return true;
}

bool Rig::loadRacket(kke::ModelModule& models) {
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR" }, base ? base : "");
    if (dir.empty()) return false;
    std::string fbx, atlas;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end && (fbx.empty() || atlas.empty());
         it.increment(ec)) {
        if (ec) break;
        const std::string file = it->path().filename().string();
        if (file == "SM_Prop_Sport_Tennis_Racket_01.fbx") fbx = it->path().string();
        else if (file == "PolygonShops_Texture_01_A.png") atlas = it->path().string();
    }
    if (fbx.empty()) {
        kke::log::get("Tennis")->info("POLYGON Shops' tennis racket not found under {}: the rackets are drawn from boxes", dir);
        return false;
    }
    kke::ModelData data;
    try {
        kke::ModelLoadOptions o;
        o.fallbackTexture = atlas; // the file names the artist's own .psd
        o.loadAnimations = false;
        data = kke::loadModel(fbx, o);
    } catch (const std::exception& e) {
        kke::log::get("Tennis")->warn("{}: {}", fbx, e.what());
        return false;
    }
    // The model: butt at y -0.16, the strings' middle at y 0.58, 0.97 long.
    // A real racket is 0.67-0.69 m: the hand 5 cm up from the butt, the
    // strings' middle 0.46 m up from the hand.
    constexpr float kScale = 0.689f;
    m_racketAlign = glm::scale(glm::mat4(1.0f), glm::vec3(kScale)) * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.087f, 0.0f));
    // Its strings are a flat see-through sheet (material Sports_Gear_Net):
    // out, the FEMFX strings go where it was, as big as it was.
    for (size_t i = 0; i < data.meshes.size(); ++i) {
        const kke::ModelMesh& m = data.meshes[i];
        if (m.material >= data.materials.size() || data.materials[m.material].name.find("Net") == std::string::npos) continue;
        glm::vec3 lo(1e9f), hi(-1e9f);
        for (const kke::ModelVertex& vx : m.vertices) {
            const glm::vec3 p = glm::vec3(m_racketAlign * glm::vec4(vx.position, 1.0f));
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        if (!m.vertices.empty()) {
            m_strings.centre = glm::vec2(0.5f * (lo + hi));
            m_strings.radii = glm::vec2(0.5f * (hi - lo));
            m_strings.build();
            uploadStrings(m_strings, m_strings.points, *m_stringsMesh);
        }
        data.meshes.erase(data.meshes.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    m_racketModel = models.add(std::move(data), fbx);
    if (!m_racketModel) return false;
    m_racketTexture = atlas;
    makeRacketItem();
    kke::log::get("Tennis")->info("racket: {} (strings {:.3f} x {:.3f} m)", fbx, 2.0f * m_strings.radii.x, 2.0f * m_strings.radii.y);
    return true;
}

std::unique_ptr<StringBed> Rig::makeStringBed() {
    // Every hitting racket in a full sport center (10 courts x 4), and some.
    constexpr int kMostBeds = 64;
    if (!m_physics || (m_freeBeds.empty() && m_stringBeds >= kMostBeds)) return nullptr;
    int slot = m_stringBeds;
    if (!m_freeBeds.empty()) {
        slot = m_freeBeds.back();
        m_freeBeds.pop_back();
    } else {
        ++m_stringBeds;
    }
    auto bed = std::make_unique<StringBed>(*m_physics, m_strings, slot);
    if (!bed->valid()) {
        m_freeBeds.push_back(slot);
        return nullptr;
    }
    return bed;
}

kke::ModelModule::ModelId Rig::outfitModel(kke::ModelModule& models, const kke::Outfit& outfit) {
    if (m_base.meshes.empty()) return 0;
    const std::string key = kke::outfitKey(outfit);
    if (const auto it = m_outfits.find(key); it != m_outfits.end()) return it->second;
    const kke::ModelModule::ModelId id = models.add(kke::dressModel(m_base, outfit), "tennis/person/" + key);
    m_outfits[key] = id;
    return id;
}

// ------------------------------------------------------------------ Body

Body::Body(Rig& rig, kke::ModelModule& models, const glm::vec3& tint, bool racket)
    : m_rig(rig), m_models(models), m_hasRacket(racket), m_tint(tint) {
    // Made now, so it has settled into its frame by the first hit.
    if (racket) m_bed = rig.makeStringBed();
    m_equip = rig.equipment();
    if (racket) m_equip.equip(kke::EquipSlot::RightHand, rig.racketItem());
    if (racket && rig.racketModel()) {
        m_racketInstance = models.spawn(rig.racketModel());
        models.setOverlayEnabled(m_racketInstance, false);
        if (!rig.racketTexture().empty()) models.setTextureOverride(m_racketInstance, rig.racketTexture());
    }
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
    if (m_personInstance) m_models.remove(m_personInstance);
    if (m_racketInstance) m_models.remove(m_racketInstance);
    if (m_bed) m_rig.freeStringBed(*m_bed);
}

void Body::setTint(const glm::vec3& tint) {
    m_tint = tint;
    if (m_instance && m_dressedAs.empty()) m_models.setTint(m_instance, tint);
}

// A new outfit swaps the model instance; the animator and the pose carry on.
void Body::setOutfit(const kke::Outfit& outfit) {
    const std::string key = kke::outfitKey(outfit);
    if (!m_instance || key == m_dressedAs) return;
    const kke::ModelModule::ModelId id = m_rig.outfitModel(m_models, outfit);
    if (!id) return;
    m_models.remove(m_instance);
    m_instance = m_models.spawn(id, m_xf);
    m_models.setOverlayEnabled(m_instance, false);
    m_models.setVisible(m_instance, m_visible && !m_personInstance);
    m_dressedAs = key;
}

// The mannequin keeps animating (hidden) and lends the person its pose.
void Body::setPerson(int person) {
    if (!m_instance || !m_rig.people().has(person)) person = 0;
    if (person == m_person) return;
    if (m_personInstance) m_models.remove(m_personInstance);
    m_personInstance = 0;
    m_person = 0;
    if (const kke::PeopleLibrary::Body* b = person ? m_rig.person(m_models, person) : nullptr) {
        m_personInstance = m_models.spawn(b->model, m_xf * b->retarget.placement());
        m_models.setOverlayEnabled(m_personInstance, false);
        m_models.setVisible(m_personInstance, m_visible);
        m_person = person;
    }
    m_models.setVisible(m_instance, m_visible && !m_personInstance);
}

void Body::setVisible(bool visible) {
    m_visible = visible;
    if (m_instance) m_models.setVisible(m_instance, visible && !m_personInstance);
    if (m_personInstance) m_models.setVisible(m_personInstance, visible);
    if (m_racketInstance) m_models.setVisible(m_racketInstance, visible);
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
        if (m_racketInstance) m_models.setTransform(m_racketInstance, m_racketWorld * m_rig.racketAlign());
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
    // The move blend settles: a creep (the character controller's
    // leftovers, a shuffle into place) is standing still, not a frozen
    // half step of the walk blended into the idle.
    const float target = speed < 0.35f ? 0.0f : speed;
    m_moveSpeed += (target - m_moveSpeed) * (1.0f - std::exp(-10.0f * dt));
    if (target == 0.0f && m_moveSpeed < 0.05f) m_moveSpeed = 0.0f;
    a.setParameter(m_moveSpeed);
    a.update(dt);
    kke::Pose pose = a.pose();

    // The stroke (Swing.h): the shoulders turn, the knees bend, the racket
    // arm follows the stroke's path; in the body's frame.
    const kke::ModelData& rig = m_rig.data();
    const glm::vec3 fwdM = kke::modelForward(rig);
    Frame fr{ glm::normalize(glm::cross(fwdM, glm::vec3(0, 1, 0))), glm::vec3(0, 1, 0), fwdM };
    const kke::TwoBoneChain& rArm = m_rig.arm(1);
    const kke::TwoBoneChain& lArm = m_rig.arm(0);
    glm::vec3 handBody(0.3f, 1.05f, 0.35f), shaftBody(0.15f, 0.9f, 0.4f);
    RacketPose rp;
    const bool playing = mood == Mood::Play;
    const bool swinging = playing && swing.stroke != Stroke::Ready && swing.t > kSwingIdle + 0.5f;
    if (playing) {
        glm::vec3 contact = swing.contact;
        if (glm::length(contact) < 0.01f) contact = shapeOf(swing.stroke).contact * glm::vec3(swing.backhand ? -1.0f : 1.0f, 1.0f, 1.0f);
        rp = racketAt(swinging ? swing.stroke : Stroke::Ready, swing.backhand, swinging ? swing.t : kSwingIdle, contact);
        if (swing.tossing && swing.t <= -1.0f) rp.head = shapeOf(Stroke::Toss).takeback; // the trophy position while the ball goes up
        bendAndTurn(m_rig, pose, fr.up, fr.fwd, fr.right, rp.crouch, rp.twist);
    }
    const std::vector<glm::mat4> bones = kke::poseToModel(rig, pose);
    const glm::mat4 bodyToWorld = glm::mat4(glm::vec4(worldRight, 0.0f), glm::vec4(0, 1, 0, 0), glm::vec4(facing, 0.0f), glm::vec4(feet, 1.0f));
    const glm::quat modelToWorld = rotationOf(m_xf);
    // The racket as the stroke wants it (world): the shaft, and the strings
    // facing where the shoulders face, opened (slice, lob) or closed
    // (topspin) by the stroke.
    auto racketFace = [&](const glm::vec3& shaft) {
        const float turned = glm::radians(swing.backhand ? rp.twist : -rp.twist) * 0.6f;
        const glm::vec3 shoulders = std::cos(turned) * facing + std::sin(turned) * worldRight;
        glm::vec3 face = shoulders - shaft * glm::dot(shoulders, shaft);
        face = glm::length(face) > 1e-3f ? glm::normalize(face) : glm::vec3(0, 1, 0);
        glm::vec3 up = glm::vec3(0, 1, 0) - shaft * glm::dot(glm::vec3(0, 1, 0), shaft) - face * glm::dot(glm::vec3(0, 1, 0), face);
        if (glm::length(up) > 1e-3f) {
            const float open = glm::radians(rp.faceOpen);
            face = glm::normalize(std::cos(open) * face + std::sin(open) * glm::normalize(up));
        }
        return face;
    };
    glm::vec3 handWorld = glm::vec3(bodyToWorld * glm::vec4(handBody, 1.0f));
    glm::vec3 shaft = glm::normalize(glm::vec3(bodyToWorld * glm::vec4(shaftBody, 0.0f)));
    glm::vec3 face = racketFace(shaft);
    // The racket is equipment (kke/Equipment.h): its handle in the right
    // palm, the fingers round it. The arms keep out of the body, and the
    // racket too (kke/BodyShape.h): where a stroke would take either
    // through the chest or the head, the elbow swings round or the hand
    // gives way.
    const glm::quat toModel = glm::inverse(modelToWorld);
    const kke::Equippable& racketItem = m_rig.racketItem();
    const kke::BodyShape& shape = m_rig.bodyShape();
    glm::mat4 racketM(1.0f); // the racket in model space, as the hand holds it
    if (m_hasRacket && rArm.valid()) {
        kke::ArmGoal goal;
        if (playing) {
            const glm::vec3 shoulderM(bones[static_cast<size_t>(rArm.upper)][3]);
            const glm::vec3 shoulder(glm::dot(shoulderM, fr.right), shoulderM.y, glm::dot(shoulderM, fr.fwd));
            // The racket head's centre is ~0.46 m up the shaft from the hand;
            // the racket stays nearer level than the arm.
            glm::vec3 dir = rp.head - shoulder;
            dir.y = dir.y * 0.4f + 0.25f;
            shaftBody = glm::normalize(dir);
            handBody = rp.head - shaftBody * 0.46f;
            shaft = glm::normalize(glm::vec3(bodyToWorld * glm::vec4(shaftBody, 0.0f)));
            face = racketFace(shaft);
            // Where the stroke wants the racket; the arm takes the hand to
            // hold it there, as far as a person's joints go.
            const glm::vec3 y = glm::normalize(toModel * shaft);
            glm::vec3 z = toModel * face;
            z = glm::normalize(z - y * glm::dot(z, y));
            const glm::mat4 want(glm::vec4(glm::cross(y, z), 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(fr.toModel(handBody), 1.0f));
            goal = m_equip.armGoal(1, racketItem, 0, want);
        } else {
            // Not playing: the racket in the hand as the clip swings it.
            const glm::mat4& h = bones[static_cast<size_t>(rArm.end)];
            goal.hand = glm::vec3(h[3]);
            goal.handRotation = rotationOf(h);
        }
        kke::BodyAvoid avoid;
        avoid.held = m_equip.heldShape(1);
        kke::solveHumanArm(rig, pose, m_rig.humanArm(1), goal, shape, avoid, &m_avoid[1]);
        racketM = m_equip.itemTransform(kke::EquipSlot::RightHand, kke::poseToModel(rig, pose));
        const glm::mat4 rw = m_xf * racketM;
        handWorld = glm::vec3(rw[3]);
        shaft = glm::normalize(glm::vec3(rw[1]));
        face = glm::normalize(glm::vec3(rw[2]));
        handBody = glm::vec3(glm::inverse(bodyToWorld) * glm::vec4(handWorld, 1.0f));
        shaftBody = glm::normalize(glm::vec3(glm::inverse(bodyToWorld) * glm::vec4(shaft, 0.0f)));
    }
    // The other hand: on the handle above the right for a two-handed
    // backhand, up at the ball for a toss or a smash, otherwise at the
    // racket's throat or out for balance.
    glm::vec3 tossBody(-0.25f, 1.15f, 0.35f);
    int leftGrip = -1;
    if (lArm.valid() && playing) {
        kke::ArmGoal goal;
        auto at = [&](const glm::vec3& body) { goal.hand = fr.toModel(body); };
        if (rp.twoHands && m_hasRacket) leftGrip = m_rig.supportGrip();
        else if (swing.tossing && swing.t <= -1.0f) at(glm::vec3(-0.05f, 2.05f, 0.4f));
        else if (swinging && swing.stroke == Stroke::Smash && swing.t <= -1.0f) at(glm::vec3(-0.3f, 2.0f, 0.45f));
        else if (!swinging && m_hasRacket) leftGrip = m_rig.throatGrip();
        else at(glm::vec3(-0.45f, 1.05f, 0.25f) + glm::vec3(0.0f, 0.0f, 0.2f) * (swing.t < 0.0f ? 1.0f : 0.0f));
        if (leftGrip >= 0) goal = m_equip.armGoal(0, racketItem, leftGrip, racketM);
        kke::BodyAvoid avoid;
        avoid.otherArm = leftGrip < 0; // both hands on the racket: the forearms may cross
        const kke::BodyAvoidResult reached = kke::solveHumanArm(rig, pose, m_rig.humanArm(0), goal, shape, avoid, &m_avoid[0]);
        if (swing.tossing && swing.t <= -1.0f) tossBody = glm::vec3(glm::inverse(bodyToWorld) * (m_xf * glm::vec4(reached.arm.hand, 1.0f)));
    }
    // Fingers closed round the handle (and the throat).
    if (m_hasRacket) {
        m_equip.closeHand(rig, pose, 1);
        if (leftGrip >= 0 && m_equip.equip(kke::EquipSlot::LeftHand, racketItem, leftGrip)) m_equip.closeHand(rig, pose, 0);
        else m_equip.unequip(kke::EquipSlot::LeftHand);
    }
    if (std::vector<glm::mat4>* locals = m_models.boneLocals(m_instance)) {
        kke::poseToLocals(pose, *locals);
        if (m_personInstance)
            if (const kke::PeopleLibrary::Body* b = m_rig.person(m_models, m_person)) kke::PeopleLibrary::follow(m_models, m_personInstance, *b, *locals, m_xf);
    }

    // The racket in the world, in the hand.
    const glm::vec3 x = glm::cross(shaft, face);
    m_racketWorld = glm::mat4(glm::vec4(x, 0.0f), glm::vec4(shaft, 0.0f), glm::vec4(face, 0.0f), glm::vec4(handWorld, 1.0f));
    m_tossHand = glm::vec3(bodyToWorld * glm::vec4(tossBody + glm::vec3(0.0f, 0.08f, 0.0f), 1.0f));
    if (m_racketInstance) m_models.setTransform(m_racketInstance, m_racketWorld * m_rig.racketAlign());
    const glm::vec3 head = glm::vec3(m_racketWorld * glm::vec4(m_rig.strings().centre, 0.0f, 1.0f));
    if (dt > 0.0f) m_racketVel = (head - m_racketHead) / dt;
    m_racketHead = head;
    updateStrings(dt);
}

void Body::hitStrings(const glm::vec3& ball, const glm::vec3& velocity) {
    if (!m_bed) return;
    // Into the racket's frame; the ball's speed as the strings feel it.
    const glm::mat4 toRacket = glm::inverse(m_racketWorld);
    const glm::vec3 at = glm::vec3(toRacket * glm::vec4(ball, 1.0f)), rel = glm::vec3(toRacket * glm::vec4(velocity - m_racketVel, 0.0f));
    m_bed->strike(at, rel);
}

void Body::updateStrings(float dt) {
    if (!m_bed) return;
    const bool bent = m_bed->update(dt, m_bentPoints);
    if (bent) {
        if (!m_bentStrings) m_bentStrings = std::make_unique<kke::DynamicMeshRenderer>(m_rig.app());
        uploadStrings(m_rig.strings(), m_bentPoints, *m_bentStrings);
    }
    m_bent = bent;
}

void Body::render(const kke::RenderContext& ctx) {
    if (!m_visible) return;
    if (!m_instance) m_rig.block().draw(ctx, m_xf, 0.0f, 0.6f);
    if (m_hasRacket && !m_racketInstance) m_rig.racketMesh().draw(ctx, m_racketWorld, 0.1f, 0.45f);
    if (m_hasRacket) (m_bent ? *m_bentStrings : m_rig.stringsMesh()).draw(ctx, m_racketWorld, 0.0f, 0.5f);
}

void Body::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (!m_visible) return;
    if (!m_instance) m_rig.block().drawShadow(ctx, m_xf);
    if (m_hasRacket && !m_racketInstance) m_rig.racketMesh().drawShadow(ctx, m_racketWorld);
}

} // namespace tennis
