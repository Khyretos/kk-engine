#include "DogBody.h"

#include "Scenery.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/Ragdoll.h"
#include "kke/SphereImpostors.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace pet_companion {

namespace fs = std::filesystem;

namespace {

constexpr const char* kDogFile = "Unity_SK_Animals_Dog_01";
constexpr float kPugHeight = 0.55f;

glm::vec3 pos(const glm::mat4& m) { return glm::vec3(m[3]); }

// Model-space transform of bone `b` in one frame of a clip (locals chained up the parents).
glm::mat4 boneInFrame(const kke::ModelData& d, const std::vector<glm::mat4>& frame, int b) {
    glm::mat4 m(1.0f);
    for (int i = b; i >= 0; i = d.bones[size_t(i)].parent) m = (size_t(i) < frame.size() ? frame[size_t(i)] : d.bones[size_t(i)].localRest) * m;
    return m;
}

// A POLYGON Dogs animation file onto the dog's skeleton, by bone name
// (bones the file doesn't move keep their rest pose). `speed` gets the
// clip's own pace: how far its hips travel per second before the clip is
// made to play in place (the game moves the dog).
bool appendClip(kke::ModelData& target, const fs::path& file, const std::string& name, float* speed = nullptr) {
    kke::ModelLoadOptions o;
    o.allowNoMeshes = true;
    o.clipsInPlace = false;
    kke::ModelData src;
    try {
        src = kke::loadModel(file.string(), o);
    } catch (const std::exception&) {
        return false;
    }
    if (src.animations.empty()) return false;
    if (speed) {
        const kke::ModelAnimation& a = src.animations.front();
        const int hips = src.findBone("spine_C0_hip_joint");
        if (hips >= 0 && a.frames.size() > 1 && a.duration > 0.0f) {
            glm::vec3 d = pos(boneInFrame(src, a.frames.back(), hips)) - pos(boneInFrame(src, a.frames.front(), hips));
            d.y = 0.0f;
            *speed = glm::length(d) / a.duration;
        }
    }
    kke::makeClipsInPlace(src);
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

struct ClipFile {
    const char* name;
    const char* file;
};
// Named after what they're for; DogBody::setUpRig finds them by these names.
const ClipFile kClips[] = {
    { "idle", "Locomotion/_POLYGON_Dog_Locomotion_Standing.fbx" },
    { "walk", "Locomotion/_POLYGON_Dog_Locomotion_Walking.fbx" },
    { "run", "Locomotion/_POLYGON_Dog_Locomotion_Running.fbx" },
    { "sit_to", "Transitions/_POLYGON_Dog_Transition_Stand_ToSit.fbx" },
    { "sit", "Sit/_POLYGON_Dog_Sitting.fbx" },
    { "sit_from", "Transitions/_POLYGON_Dog_Transition_Sit_ToStand.fbx" },
    { "sleep_to", "Transitions/_POLYGON_Dog_Transition_Stand_ToSleep.fbx" },
    { "sleep", "Sleep/_POLYGON_Dog_Sleep_Idle.fbx" },
    { "sleep_from", "Transitions/_POLYGON_Dog_Transition_Sleep_ToStand.fbx" },
    { "eat", "Actions_Standing/_POLYGON_Dog_Action_Standing_Eat.fbx" },
    { "drink", "Actions_Standing/_POLYGON_Dog_Action_Standing_Drink.fbx" },
    { "sniff", "Actions_Standing/_POLYGON_Dog_Action_Standing_Sniff.fbx" },
    { "bark", "Actions_Standing/_POLYGON_Dog_Action_Standing_Bark.fbx" },
    { "wag", "Actions_Standing/_POLYGON_Dog_Action_Standing_TailWag.fbx" },
    { "beg", "Actions_Standing/_POLYGON_Dog_Action_Standing_Beg.fbx" },
    { "shake", "Actions_Standing/_POLYGON_Dog_Action_Standing_ShakeToy.fbx" },
    { "dig", "Actions_Standing/_POLYGON_Dog_Action_Standing_Dig.fbx" },
    { "poop", "Actions_Standing/_POLYGON_Dog_Action_Standing_Pooping.fbx" },
    { "pee", "Actions_Standing/_POLYGON_Dog_Action_Standing_Peeing.fbx" },
    { "yawn", "Actions_Standing/_POLYGON_Dog_Action_Standing_Yawn.fbx" },
    { "cower", "Actions_Standing/_POLYGON_Dog_Action_Standing_Cower.fbx" },
    { "joy", "Locomotion/_POLYGON_Dog_Locomotion_Jump_Standing.fbx" },
    { "leap", "Locomotion/_POLYGON_Dog_Locomotion_Jump_Running.fbx" },
    { "fall", "Locomotion/_POLYGON_Dog_Locomotion_Falling.fbx" },
    { "sit_bark", "Actions_Sitting/_POLYGON_Dog_Action_Sitting_Bark.fbx" },
    { "sit_wag", "Actions_Sitting/_POLYGON_Dog_Action_Sitting_TailWag.fbx" },
    { "sit_beg", "Actions_Sitting/_POLYGON_Dog_Action_Sitting_Beg.fbx" },
    { "sit_yawn", "Actions_Sitting/_POLYGON_Dog_Action_Sitting_Yawn.fbx" },
};

bool loops(DogAct a) {
    switch (a) {
    case DogAct::Bark:
    case DogAct::Shake:
    case DogAct::Poop:
    case DogAct::Pee:
    case DogAct::Yawn:
    case DogAct::Joy:
    case DogAct::Leap: return false;
    default: return true;
    }
}

} // namespace

const std::vector<Breed>& breeds() {
    // Scales make the breeds the size they are next to each other (the
    // pack draws them all on one skeleton, about a labrador's size).
    static const std::vector<Breed> list = {
        { "labrador", "Labrador", "Labrador", "Labrador", 1.0f },
        { "golden", "Golden Retriever", "GoldenRetriever", "GoldenRetriever", 1.0f },
        { "shepherd", "German Shepherd", "GermanShepherd", "GermanShepherd", 1.05f },
        { "husky", "Husky", "Husky", "Husky", 1.0f },
        { "shiba", "Shiba Inu", "Shiba", "Shiba", 0.8f },
        { "dalmatian", "Dalmatian", "Dalmatian", "Dalmatian", 1.0f },
        { "doberman", "Dobermann", "Doberman", "Doberman", 1.05f },
        { "greyhound", "Greyhound", "Greyhound", "Greyhound", 1.0f },
        { "pointer", "Pointer", "Pointer", "Pointer", 1.0f },
        { "ridgeback", "Ridgeback", "Ridgeback", "Ridgeback", 1.05f },
        { "wolf", "Wolf", "Wolf", "Wolf", 1.1f },
        { "fox", "Fox", "Fox", "Fox", 0.8f },
        { "coyote", "Coyote", "Coyote", "Coyote", 0.9f },
        { "robot", "Robot dog", "Robot", "Robot", 1.0f },
        { "scifi", "Sci-fi dog", "Scifi", "Sci-fi", 1.0f },
        { "hellhound", "Hellhound", "HellHound", "HellHound", 1.15f },
        { "zombie", "Zombie dog", "Zombie_Doberman", "Zombie_Doberman", 1.0f },
        { "pug", "Pug", "", "", 1.0f },
    };
    return list;
}

int breedIndex(const std::string& id) {
    const std::vector<Breed>& b = breeds();
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].id == id) return int(i);
    return -1;
}

const char* dogActName(DogAct act) {
    static const char* names[] = { "move", "sit", "sleep", "eat", "drink", "sniff", "bark", "wag", "beg",
                                   "shake", "dig", "poop", "pee", "yawn", "cower", "joy", "leap", "fall" };
    return names[size_t(act)];
}

DogBody::DogBody(kke::Application& app, kke::ModelModule& models, command_kit::Scenery& scenery)
    : m_app(app), m_models(models), m_scenery(scenery) {
    m_looks.resize(breeds().size());
    // No art: a brown block with a nose (facing +Z).
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    command_kit::appendBox({ 0, 0.3f, 0 }, { 0.15f, 0.15f, 0.3f }, { 0.75f, 0.55f, 0.35f }, v, idx);
    command_kit::appendBox({ 0, 0.45f, 0.33f }, { 0.1f, 0.1f, 0.1f }, { 0.35f, 0.25f, 0.15f }, v, idx);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_block->upload(v, idx);
}

DogBody::~DogBody() {
    if (m_instance) m_models.remove(m_instance);
}

bool DogBody::available(int breed) const {
    if (breed < 0 || size_t(breed) >= breeds().size()) return false;
    const kke::AssetCatalog& c = m_scenery.catalog();
    if (breeds()[size_t(breed)].material.empty()) return c.find("Pug", { "Farm Animals Animated  by Quaternius" }) != nullptr;
    return c.find(kDogFile, { "POLYGON_Dogs" }) != nullptr;
}

std::string DogBody::label() const { return m_breed >= 0 ? breeds()[size_t(m_breed)].label : std::string("Dog"); }

bool DogBody::load(int breed, int coat) {
    if (breed < 0 || size_t(breed) >= breeds().size()) breed = 0;
    if (m_instance) m_models.remove(m_instance);
    m_instance = 0;
    m_breed = breed;
    m_coat = std::clamp(coat, 0, 2);
    const Breed& b = breeds()[size_t(breed)];
    const bool ok = b.material.empty() ? loadPug() : loadSynty(b);
    if (!ok) {
        // A block the size of a small dog.
        m_synty = false;
        m_height = 0.55f;
        m_length = 0.6f;
        m_walk = 1.2f;
        m_run = 4.5f;
        m_anim.reset();
        return false;
    }
    m_posture = Posture::Stand;
    m_act = DogAct::Move;
    m_actTime = 0.0f;
    m_gaitReady = false;
    return true;
}

bool DogBody::loadSynty(const Breed& b) {
    Look& look = m_looks[size_t(&b - breeds().data())];
    const kke::AssetCatalog& c = m_scenery.catalog();
    const kke::CatalogAsset* asset = c.find(kDogFile, { "POLYGON_Dogs" });
    if (!asset) return false;
    if (!look.model) {
        kke::ModelLoadOptions opts = kke::packLoadOptions(c, *asset);
        opts.loadAnimations = true;
        kke::ModelData d;
        try {
            d = kke::loadModel(asset->path, opts);
        } catch (const std::exception& e) {
            kke::log::get("Pet")->warn("dog: {}", e.what());
            return false;
        }
        // The file holds every breed on one skeleton: keep this one's parts (and its collar).
        std::vector<kke::ModelMesh> keep;
        for (kke::ModelMesh& m : d.meshes) {
            const std::string& mat = m.material < d.materials.size() ? d.materials[m.material].name : std::string();
            if (mat.rfind(b.material + "_", 0) == 0) keep.push_back(std::move(m));
        }
        if (keep.empty()) {
            kke::log::get("Pet")->warn("dog: no meshes for '{}' in {}", b.material, asset->path);
            return false;
        }
        d.meshes = std::move(keep);
        d.animations.clear();
        const fs::path anims = fs::path(asset->path).parent_path() / "Animations";
        for (const ClipFile& clip : kClips) {
            float* speed = std::string(clip.name) == "walk" ? &look.walk : std::string(clip.name) == "run" ? &look.run : nullptr;
            if (!appendClip(d, anims / clip.file, clip.name, speed))
                kke::log::get("Pet")->warn("dog clip '{}' missing ({})", clip.name, clip.file);
        }
        // A pace that isn't a dog's (a clip made in place) falls back to one.
        if (look.walk < 0.4f || look.walk > 4.0f) look.walk = 1.3f;
        if (look.run < 2.0f || look.run > 15.0f) look.run = 5.5f;
        look.model = m_models.add(std::move(d), "pet-dog:" + b.id);
        kke::log::get("Pet")->info("{}: POLYGON Dogs, walks at {:.2f} m/s and runs at {:.2f} m/s in its clips", b.label, look.walk, look.run);
    }
    const kke::ModelData* d = m_models.model(look.model);
    if (!d) return false;
    m_synty = true;
    m_scale = b.scale;
    m_instance = m_models.spawn(look.model);
    m_models.setOverlayEnabled(m_instance, false);
    if (m_coat > 0 && !b.texture.empty()) {
        // The pack's other coats: the same UVs, another texture.
        const fs::path tex = fs::path(asset->path).parent_path().parent_path() / "Textures" / "AltTextures" /
                             ("PolygonDog_" + b.texture + "_0" + std::to_string(m_coat + 1) + ".png");
        std::error_code ec;
        if (fs::exists(tex, ec)) m_models.setTextureOverride(m_instance, tex.string());
    }
    m_walk = look.walk * m_scale;
    m_run = look.run * m_scale;
    setUpRig(*d);
    return true;
}

bool DogBody::loadPug() {
    Look& look = m_looks.back();
    if (!look.model) look.model = m_scenery.model("Pug", { "Farm Animals Animated  by Quaternius" }, true);
    const kke::ModelData* d = look.model ? m_models.model(look.model) : nullptr;
    if (!d || d->bones.empty()) return false;
    m_synty = false;
    m_instance = m_models.spawn(look.model);
    m_models.setOverlayEnabled(m_instance, false);
    m_scale = kPugHeight * 0.95f / std::max(0.1f, d->boundsMax.y - d->boundsMin.y);
    m_walk = 1.2f;
    m_run = 4.5f;
    setUpRig(*d);
    return true;
}

void DogBody::setUpRig(const kke::ModelData& d) {
    m_rig = kke::ModelData{};
    m_rig.bones = d.bones;
    m_rig.animations = d.animations;
    m_set = std::make_unique<kke::AnimationSet>(m_rig);
    m_anim = std::make_unique<kke::Animator>(*m_set);
    m_state.assign(size_t(DogAct::Fall) + 1, -1);
    m_legs = kke::LegPlacer();
    m_gait = kke::ProceduralGait();
    m_legChains.clear();
    m_upLegs.clear();
    m_lowLegs.clear();
    const std::vector<glm::mat4> rest = kke::computeRestPose(m_rig);

    // How big it is: the skinned mesh's bounds (model space at rest), and
    // which way it faces: from its hind legs to its front legs.
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (const kke::ModelMesh& m : d.meshes)
        for (const kke::ModelVertex& v : m.vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
    if (mn.x > mx.x) {
        mn = d.boundsMin;
        mx = d.boundsMax;
    }
    kke::QuadrupedBones qb;
    if (m_synty) {
        qb.hips = "spine_C0_hip_joint";
        qb.neck = "neck_C0_1_joint";
        qb.head = "neck_C0_head_joint";
        const char* side[2] = { "L", "R" };
        for (int i = 0; i < 2; ++i) {
            qb.upperLeg[i] = std::string("frontLeg_") + side[i] + "0_0_joint";
            qb.lowerLeg[i] = std::string("frontLeg_") + side[i] + "0_1_joint";
            qb.foot[i] = std::string("frontLeg_") + side[i] + "0_2_joint";
            qb.upperLeg[2 + i] = std::string("backLeg_") + side[i] + "0_0_joint";
            qb.lowerLeg[2 + i] = std::string("backLeg_") + side[i] + "0_1_joint";
            qb.foot[2 + i] = std::string("backLeg_") + side[i] + "0_2_joint";
        }
    }
    glm::vec3 front(0.0f), back(0.0f);
    for (int i = 0; i < 4; ++i) {
        const int bone = m_rig.findBone(qb.upperLeg[i]);
        if (bone >= 0) (i < 2 ? front : back) += pos(rest[size_t(bone)]);
    }
    glm::vec3 fwd = front - back;
    fwd.y = 0.0f;
    fwd = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : glm::vec3(0, 0, 1);
    m_yawOffset = glm::degrees(std::atan2(fwd.x, fwd.z));
    m_height = (mx.y - mn.y) * m_scale;
    m_length = glm::length(front - back) * 1.6f * m_scale;
    m_head = m_rig.findBone(qb.head);
    m_hips = m_rig.findBone(qb.hips);
    m_jaw = m_rig.findBone("jaw_C0_0_joint");
    m_snout = m_rig.findBone("snout_C0_0_joint");

    if (m_synty) {
        auto clip = [&](const char* name) { return m_set->find(name); };
        auto state = [&](const char* name, bool loop) {
            const int c = clip(name);
            return c >= 0 ? m_anim->addClipState(name, c, loop) : -1;
        };
        kke::BlendSpace1D move;
        move.points.push_back({ clip("idle"), 0.0f });
        if (clip("walk") >= 0) move.points.push_back({ clip("walk"), m_walk });
        if (clip("run") >= 0) move.points.push_back({ clip("run"), m_run });
        m_move = m_anim->addBlendState("move", move);
        const char* actClip[] = { nullptr, "sit", "sleep", "eat", "drink", "sniff", "bark", "wag", "beg",
                                  "shake", "dig", "poop", "pee", "yawn", "cower", "joy", "leap", "fall" };
        for (size_t a = 1; a < m_state.size(); ++a) m_state[a] = state(actClip[a], loops(DogAct(a)));
        m_state[0] = m_move;
        m_toSit = state("sit_to", false);
        m_fromSit = state("sit_from", false);
        m_toSleep = state("sleep_to", false);
        m_fromSleep = state("sleep_from", false);
        m_sitIdle = m_state[size_t(DogAct::Sit)];
        m_sleepIdle = m_state[size_t(DogAct::Sleep)];
        m_sitBark = state("sit_bark", false);
        m_sitWag = state("sit_wag", true);
        m_sitBeg = state("sit_beg", true);
        m_sitYawn = state("sit_yawn", false);
        // Feet keep their clip height over the ground under them (a ramp, a seesaw).
        std::vector<kke::TwoBoneChain> legs = kke::quadrupedLegChains(m_rig, qb);
        if (legs.size() == 4 && m_hips >= 0) {
            kke::LegPlacer::Settings ls;
            ls.maxDrop = 0.25f;
            ls.maxRaise = 0.25f;
            m_legs = kke::LegPlacer(std::move(legs), m_hips, ls);
        }
        std::vector<kke::LookAt::Link> chain;
        for (const char* n : { "neck_C0_0_joint", "neck_C0_1_joint", "neck_C0_2_joint", "neck_C0_head_joint" }) {
            const int bone = m_rig.findBone(n);
            if (bone >= 0) chain.push_back({ bone, 1.0f });
        }
        m_look = chain.empty() ? kke::LookAt() : kke::LookAt(chain, fwd);
    } else {
        // The pug: Idle and Jump; procedural legs, sit and head.
        m_move = m_anim->addClipState("idle", m_set->find("Idle"), true);
        m_state.assign(m_state.size(), m_move);
        m_state[size_t(DogAct::Joy)] = m_state[size_t(DogAct::Leap)] = m_anim->addClipState("jump", m_set->find("Jump"), false, 1.2f);
        m_legChains = kke::quadrupedLegChains(m_rig);
        std::vector<kke::LegDesc> legs = kke::legsFromSkeleton(m_rig, m_legChains, glm::scale(glm::mat4(1.0f), glm::vec3(m_scale)));
        if (legs.size() == 4 && m_hips >= 0) {
            kke::GaitSettings gs;
            gs.stepHeight = 0.45f; // a short-legged dog lifts its paws high for its size
            gs.cycleFast = 0.25f;
            m_gait = kke::ProceduralGait(std::move(legs), gs);
        } else {
            kke::log::get("Pet")->warn("the pug has {} of 4 leg chains: its legs stay still", legs.size());
        }
        for (const char* n : { "FrontUpLeg.L", "FrontUpLeg.R", "BackUpLeg.L", "BackUpLeg.R" }) m_upLegs.push_back(m_rig.findBone(n));
        for (const char* n : { "FrontLowLeg.L", "FrontLowLeg.R", "BackLowLeg.L", "BackLowLeg.R" }) m_lowLegs.push_back(m_rig.findBone(n));
        m_look = kke::LookAt::quadruped(m_rig);
        m_yawOffset = 0.0f; // the pug faces +Z
    }
    m_anim->play(m_move, 0.0f);
}

void DogBody::play(DogAct act, float fade) {
    const int s = m_state.empty() ? -1 : m_state[size_t(act)];
    if (s >= 0) m_anim->play(s, fade, !loops(act));
}

glm::mat4 DogBody::modelMatrix(const Frame& f) const {
    glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), f.feet), glm::radians(f.yaw), glm::vec3(0, 1, 0));
    if (f.pitch != 0.0f) m = glm::rotate(m, glm::radians(-f.pitch), glm::vec3(1, 0, 0));
    return glm::scale(glm::rotate(m, glm::radians(-m_yawOffset), glm::vec3(0, 1, 0)), glm::vec3(m_scale));
}

void DogBody::update(const Frame& f, const Ground& ground, float dt) {
    m_blockXf = glm::rotate(glm::translate(glm::mat4(1.0f), f.feet), glm::radians(f.yaw), glm::vec3(0, 1, 0));
    if (f.act != m_act) {
        m_act = f.act;
        m_actTime = 0.0f;
    } else {
        m_actTime += dt;
    }
    if (!m_instance || !m_anim) {
        m_world = m_blockXf;
        return;
    }
    m_world = modelMatrix(f);
    m_models.setTransform(m_instance, m_world);
    kke::Pose pose;
    if (m_synty) animateSynty(f, ground, dt, pose);
    else animatePug(f, dt, pose);
    if (m_look.valid()) {
        const glm::vec3 target = glm::vec3(glm::inverse(m_world) * glm::vec4(f.look, 1.0f));
        const bool busy = f.act == DogAct::Eat || f.act == DogAct::Drink || f.act == DogAct::Sleep || f.act == DogAct::Leap ||
                          f.act == DogAct::Joy || f.act == DogAct::Poop || f.act == DogAct::Pee || f.act == DogAct::Dig ||
                          f.act == DogAct::Sniff || f.act == DogAct::Shake;
        m_look.apply(m_rig, pose, f.hasLook ? &target : nullptr, dt, busy ? 0.0f : 1.0f);
    }
    m_bones = kke::poseToModel(m_rig, pose);
    if (std::vector<glm::mat4>* locals = m_models.boneLocals(m_instance)) kke::poseToLocals(pose, *locals);
}

void DogBody::animateSynty(const Frame& f, const Ground& ground, float dt, kke::Pose& pose) {
    // Sitting and lying down go through the pack's transitions both ways.
    // Wagging, barking or yawning while it sits stays sitting (the pack's
    // sitting versions); begging is always done sitting.
    const bool seated = m_posture == Posture::Sit || m_posture == Posture::Sitting;
    const bool sitAct = f.act == DogAct::Wag || f.act == DogAct::Bark || f.act == DogAct::Yawn;
    const Posture want = f.act == DogAct::Sit || f.act == DogAct::Beg || (seated && sitAct) ? Posture::Sit
                         : f.act == DogAct::Sleep                                            ? Posture::Lie
                                                                                             : Posture::Stand;
    const bool done = m_anim->finished();
    switch (m_posture) {
    case Posture::Stand:
        if (want == Posture::Sit && m_toSit >= 0) {
            m_anim->play(m_toSit, 0.15f, true);
            m_posture = Posture::Sitting;
        } else if (want == Posture::Lie && m_toSleep >= 0) {
            m_anim->play(m_toSleep, 0.15f, true);
            m_posture = Posture::Lying;
        } else if (f.act == DogAct::Move || m_state[size_t(f.act)] < 0) {
            m_anim->play(m_move, 0.25f);
        } else if (m_anim->current() != m_state[size_t(f.act)] || (m_actTime == 0.0f && !loops(f.act))) {
            if (m_anim->current() != m_state[size_t(f.act)] || m_actTime == 0.0f) play(f.act, f.act == DogAct::Leap ? 0.08f : 0.2f);
        }
        break;
    case Posture::Sitting:
        if (done) {
            m_posture = Posture::Sit;
            m_anim->play(m_sitIdle, 0.1f);
        }
        break;
    case Posture::Sit: {
        if (want != Posture::Sit) {
            m_anim->play(m_fromSit >= 0 ? m_fromSit : m_move, 0.15f, true);
            m_posture = m_fromSit >= 0 ? Posture::Rising : Posture::Stand;
            break;
        }
        int clip = m_sitIdle;
        if (f.act == DogAct::Wag && m_sitWag >= 0) clip = m_sitWag;
        if (f.act == DogAct::Beg && m_sitBeg >= 0) clip = m_sitBeg;
        if (f.act == DogAct::Bark && m_sitBark >= 0) clip = m_sitBark;
        if (f.act == DogAct::Yawn && m_sitYawn >= 0) clip = m_sitYawn;
        // A one-off (a bark, a yawn) plays once, then it sits on.
        if (clip != m_sitIdle && m_anim->current() == clip && done) clip = m_sitIdle;
        if (m_anim->current() != clip && !(m_anim->current() != m_sitIdle && !done && clip == m_sitIdle && m_sitClipOnce)) {
            m_anim->play(clip, 0.2f, clip == m_sitBark || clip == m_sitYawn);
            m_sitClipOnce = clip == m_sitBark || clip == m_sitYawn;
        }
        break;
    }
    case Posture::Rising:
        if (done) m_posture = Posture::Stand;
        break;
    case Posture::Lying:
        if (done) {
            m_posture = Posture::Lie;
            m_anim->play(m_sleepIdle, 0.1f);
        }
        break;
    case Posture::Lie:
        if (want != Posture::Lie) {
            m_anim->play(m_fromSleep >= 0 ? m_fromSleep : m_move, 0.15f, true);
            m_posture = m_fromSleep >= 0 ? Posture::Waking : Posture::Stand;
        }
        break;
    case Posture::Waking:
        if (done) m_posture = Posture::Stand;
        break;
    }
    m_anim->setParameter(glm::length(glm::vec2(f.velocity.x, f.velocity.z)));
    if (f.act == DogAct::Leap && m_anim->current() == m_state[size_t(DogAct::Leap)]) m_anim->setProgress(std::clamp(f.phase, 0.0f, 1.0f));
    m_anim->update(dt);
    pose = m_anim->pose();
    // Feet on the ground under them (in the air over a jump, they stay as the clip has them).
    if (m_legs.valid() && ground && f.act != DogAct::Leap && f.act != DogAct::Fall) {
        const glm::mat4 toWorld = m_world, toModel = glm::inverse(m_world);
        const kke::LegPlacer::SurfaceQuery q = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            glm::vec3 h, n;
            if (!ground(glm::vec3(toWorld * glm::vec4(from, 1.0f)), h, n)) return false;
            hit = glm::vec3(toModel * glm::vec4(h, 1.0f));
            normal = glm::normalize(glm::mat3(toModel) * n);
            return true;
        };
        m_legs.apply(m_rig, pose, q, dt, m_posture == Posture::Stand ? 1.0f : 0.0f);
    }
}

void DogBody::animatePug(const Frame& f, float dt, kke::Pose& pose) {
    const bool joy = f.act == DogAct::Joy || f.act == DogAct::Leap;
    const int jump = m_state[size_t(DogAct::Joy)];
    if (joy && m_anim->current() != jump) m_anim->play(jump, 0.1f, true);
    if (!joy && m_anim->current() == jump) m_anim->play(m_move, 0.2f);
    if (f.act == DogAct::Leap) m_anim->setProgress(std::clamp(f.phase, 0.0f, 1.0f));
    m_anim->update(dt);
    pose = m_anim->pose();
    const bool sitting = f.act == DogAct::Sit || f.act == DogAct::Sleep || f.act == DogAct::Beg;
    m_sit += ((sitting ? 1.0f : 0.0f) - m_sit) * (1.0f - std::exp(-6.0f * dt));
    // Sitting: the back end down, nose up (on the model's transform).
    const glm::mat4 body = m_blockXf;
    const glm::mat4 xf = glm::rotate(glm::translate(body, glm::vec3(0.0f, -0.06f * m_sit, 0.0f)), glm::radians(-24.0f * m_sit), glm::vec3(1, 0, 0));
    m_world = glm::scale(xf, glm::vec3(m_scale));
    m_models.setTransform(m_instance, m_world);
    if (m_gait.valid()) {
        const kke::ProceduralGait::SurfaceQuery flat = [](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            hit = glm::vec3(from.x, 0.0f, from.z);
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return true;
        };
        if (!m_gaitReady) {
            m_gait.reset(body, flat);
            m_gaitReady = true;
        }
        m_gait.update(body, f.velocity, f.turnRate, flat, dt);
        kke::applyGait(m_rig, pose, m_legChains, m_hips, m_gait, m_world, (1.0f - m_sit) * (joy ? 0.0f : 1.0f));
    }
    // Sitting folds the back legs under and straightens the front ones.
    for (size_t i = 0; i < m_upLegs.size() && m_sit > 0.01f; ++i) {
        if (m_upLegs[i] < 0) continue;
        auto& up = pose[size_t(m_upLegs[i])];
        if (i >= 2) {
            up.r = up.r * glm::angleAxis(glm::radians(-55.0f) * m_sit, glm::vec3(1, 0, 0));
            if (m_lowLegs[i] >= 0) pose[size_t(m_lowLegs[i])].r *= glm::angleAxis(glm::radians(95.0f) * m_sit, glm::vec3(1, 0, 0));
        } else {
            up.r = up.r * glm::angleAxis(glm::radians(24.0f) * m_sit, glm::vec3(1, 0, 0));
        }
    }
}

bool DogBody::actDone() const {
    if (!m_anim) return m_actTime > 1.2f;
    if (loops(m_act)) return false;
    // The pug has one clip for everything one-shot.
    return m_anim->finished() || m_actTime > 4.0f;
}

glm::mat4 DogBody::mouthFrame() const {
    const glm::vec3 fwd = glm::normalize(glm::vec3(m_blockXf[2]));
    glm::vec3 p = glm::vec3(m_blockXf * glm::vec4(0.0f, 0.42f, 0.38f, 1.0f));
    glm::vec3 z = fwd;
    if (m_instance && m_synty && m_jaw >= 0 && m_snout >= 0 && !m_bones.empty()) {
        // Between the jaw's hinge and the nose, a little toward the nose.
        const glm::vec3 jaw = glm::vec3(m_world * glm::vec4(pos(m_bones[size_t(m_jaw)]), 1.0f));
        const glm::vec3 nose = glm::vec3(m_world * glm::vec4(pos(m_bones[size_t(m_snout)]), 1.0f));
        p = jaw + (nose - jaw) * 0.6f;
        if (glm::length(nose - jaw) > 1e-4f) z = glm::normalize(nose - jaw);
    } else if (m_instance && m_head >= 0 && !m_bones.empty()) {
        const glm::vec3 head = glm::vec3(m_world * glm::vec4(pos(m_bones[size_t(m_head)]), 1.0f));
        p = head + fwd * (m_length * 0.22f) - glm::vec3(0.0f, m_height * 0.12f, 0.0f);
    }
    glm::vec3 x = glm::cross(glm::vec3(0, 1, 0), z);
    x = glm::length(x) > 1e-4f ? glm::normalize(x) : glm::vec3(1, 0, 0);
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(p, 1.0f);
    return m;
}

glm::vec3 DogBody::mouth() const { return glm::vec3(mouthFrame()[3]); }

glm::vec3 DogBody::headTop() const {
    if (m_instance && m_head >= 0 && !m_bones.empty()) {
        const glm::vec3 head = glm::vec3(m_world * glm::vec4(pos(m_bones[size_t(m_head)]), 1.0f));
        return head + glm::vec3(0.0f, m_height * 0.14f, 0.0f);
    }
    return glm::vec3(m_blockXf * glm::vec4(0.0f, 0.56f, 0.3f, 1.0f));
}

void DogBody::render(const kke::RenderContext& ctx) {
    if (!m_instance) m_block->draw(ctx, m_blockXf, 0.0f, 0.7f);
}

void DogBody::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (!m_instance) m_block->drawShadow(ctx, m_blockXf);
}

} // namespace pet_companion
