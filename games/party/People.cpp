#include "People.h"

#include "Minigame.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace party {

namespace {

// POLYGON City Characters: everyday people, a good crowd for a game show.
struct PersonAsset {
    const char* name;
    const char* asset;
};
const PersonAsset kPeople[] = {
    { "Jock", "SK_Character_Jock" },           { "Tourist", "SK_Character_Tourist" },
    { "Firefighter", "SK_Character_FireFighter" }, { "Paramedic", "SK_Character_Paramedic" },
    { "Grandpa", "SK_Character_Grandpa" },     { "Grandma", "SK_Character_Grandma" },
    { "Hipster", "SK_Character_HipsterGirl" }, { "Punk", "SK_Character_PunkGuy" },
    { "Beachgoer", "SK_Character_SummerGirl" }, { "Roadworker", "SK_Character_Roadworker" },
    { "Hot dog", "SK_Character_Hotdog" },
};
constexpr int kPeopleCount = static_cast<int>(sizeof(kPeople) / sizeof(kPeople[0]));

// The animator's states, added in this order on every one.
enum State { Move = 0, JumpUp, Fall, Land, Dive, Hit, Cheer, Push, Pull };

} // namespace

const std::vector<std::string>& People::all() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const PersonAsset& p : kPeople) n.push_back(p.name);
        return n;
    }();
    return names;
}

const char* People::assetOf(int body) { return body >= 1 && body <= kPeopleCount ? kPeople[body - 1].asset : ""; }

void People::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_available = { 0 };
    m_rigs.clear();
    m_rigs.resize(static_cast<size_t>(kPeopleCount)); // never resized again: AnimationSets point into them
    m_paths.assign(static_cast<size_t>(kPeopleCount), std::string());
    m_options.assign(static_cast<size_t>(kPeopleCount), kke::ModelLoadOptions{});
    auto log = kke::log::get("Party");
    if (!m_models) return;
    const char* base = SDL_GetBasePath();
    const std::string animDir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    m_ualFile = animDir.empty() ? std::string() : (std::filesystem::path(animDir) / "UAL1_Standard.fbx").string();
    if (m_ualFile.empty() || !std::filesystem::exists(m_ualFile)) {
        log->info("people: no animation library (assets/animations/UAL1_Standard.fbx), so everyone is a bean");
        return;
    }
    m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    if (m_packDir.empty()) {
        log->info("people: no Synty packs (assets/synty or KKE_ASSETS_DIR), so everyone is a bean");
        return;
    }
    kke::CatalogScanOptions only;
    only.onlyPacks = { "POLYGON_City_Characters" };
    const kke::AssetCatalog catalog = kke::AssetCatalog::scan(m_packDir, only);
    for (int i = 0; i < kPeopleCount; ++i) {
        const kke::CatalogAsset* a = catalog.find(kPeople[i].asset, { "POLYGON_City_Characters" });
        if (!a) continue;
        m_paths[static_cast<size_t>(i)] = a->path;
        m_options[static_cast<size_t>(i)] = kke::packLoadOptions(catalog, *a);
        m_available.push_back(i + 1);
    }
    log->info("people: {} of {} City characters found", m_available.size() - 1, kPeopleCount);
}

bool People::has(int body) const { return std::find(m_available.begin(), m_available.end(), body) != m_available.end() && body > 0; }

People::Rig* People::rig(int body) {
    if (!has(body)) return nullptr;
    Rig& r = m_rigs[static_cast<size_t>(body - 1)];
    if (r.tried) return r.model ? &r : nullptr;
    r.tried = true;
    auto log = kke::log::get("Party");
    // Loaded the first time someone picks them (the menu cycles through
    // people one press at a time; loading all eleven up front is slow).
    try {
        if (!m_ualLoaded) {
            m_ual = kke::loadModel(m_ualFile);
            m_ualLoaded = true;
        }
        kke::ModelData model = kke::loadModel(m_paths[static_cast<size_t>(body - 1)], m_options[static_cast<size_t>(body - 1)]);
        const kke::BoneMatch match = kke::matchBones(m_ual, model);
        r.data.bones = model.bones;
        r.data.boundsMin = model.boundsMin;
        r.data.boundsMax = model.boundsMax;
        r.data.animations = kke::retargetAnimations(m_ual, model, match);
        const glm::vec3 fwd = kke::modelForward(r.data);
        r.yaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
        const float height = std::max(0.1f, model.boundsMax.y - model.boundsMin.y);
        r.scale = kBeanHeight * 1.08f / height; // the capsule's height, the head just over it
        r.model = m_models->add(std::move(model), std::string("party/") + assetOf(body));
        log->info("people: {} ready ({} of {} bones take the clips)", assetOf(body), match.matched, r.data.bones.size());
    } catch (const std::exception& e) {
        log->warn("people: {}: {}", assetOf(body), e.what());
        r.model = 0;
        return nullptr;
    }
    if (!r.model) return nullptr;
    r.set = std::make_unique<kke::AnimationSet>(r.data);
    return &r;
}

bool People::sync(Bean& b) {
    const int want = has(b.look.body) ? b.look.body : 0;
    if (b.person && b.person->body == want) return want != 0 && b.person->instance != 0;
    remove(b);
    if (!want) return false;
    Rig* r = rig(want);
    if (!r) return false;
    auto p = std::make_shared<PersonBody>();
    p->body = want;
    p->instance = m_models->spawn(r->model, glm::mat4(1.0f));
    m_models->setOverlayEnabled(p->instance, false);
    const kke::AnimationSet& s = *r->set;
    auto clip = [&s](const char* name, const char* fallback) {
        const int c = s.find(name);
        return c >= 0 ? c : std::max(0, s.find(fallback)); // the first clip when neither is there
    };
    p->anim = std::make_unique<kke::Animator>(s);
    kke::Animator& a = *p->anim;
    // The same order as State.
    a.addBlendState("move", { { { clip("|Idle_Loop", "Idle"), 0.0f }, { clip("|Walk_Loop", "Walk"), 1.6f },
                                { clip("Jog_Fwd_Loop", "Jog"), 3.6f }, { clip("Sprint_Loop", "Jog"), 6.2f } } });
    a.addClipState("jump", clip("Jump_Start", "Jump"), false, 1.8f);
    a.addClipState("fall", clip("Jump_Loop", "Jump"), true);
    a.addClipState("land", clip("Jump_Land", "Jump"), false, 1.8f);
    a.addClipState("dive", clip("|Roll", "Jump_Loop"), false, 1.3f);
    a.addClipState("hit", clip("Hit_Chest", "Hit"), false, 1.0f);
    a.addClipState("cheer", clip("|Dance_Loop", "Idle"), true);
    a.addClipState("push", clip("Push_Loop", "Hit_Chest"), true, 1.6f);
    a.addClipState("pull", clip("Crouch_Idle_Loop", "Idle"), true); // low, leaning back on the rope (bodyMatrix)
    a.play(Move, 0.0f);
    p->state = Move;
    b.person = std::move(p);
    return true;
}

void People::remove(Bean& b) {
    if (!b.person) return;
    if (b.person->instance && m_models) m_models->remove(b.person->instance);
    b.person.reset();
}

void People::animate(Bean& b, const glm::mat4& m, float speed, bool cheer, bool visible, float dt) {
    if (!b.person || !b.person->instance) return;
    PersonBody& p = *b.person;
    const Rig* r = rig(p.body);
    if (!r) return;
    m_models->setVisible(p.instance, visible);
    if (!visible) return;
    m_models->setTransform(p.instance, glm::scale(glm::rotate(m, glm::radians(r->yaw), glm::vec3(0, 1, 0)), glm::vec3(r->scale)));
    kke::Animator& a = *p.anim;
    auto go = [&](int state, float fade, bool restart = false) {
        if (p.state == state && !restart) return;
        a.play(state, fade, restart);
        p.state = state;
    };
    // Tumbling and diving come first; then the air; then the ground.
    if (b.dive > 0.0f) {
        go(Dive, 0.08f);
    } else if (b.stun > 0.0f) {
        go(Hit, 0.08f);
    } else if (!b.grounded) {
        p.airTime += dt;
        if (p.wasGrounded) go(JumpUp, 0.08f, true);
        else if (p.state == JumpUp && a.finished()) go(Fall, 0.15f);
        else if (p.state != JumpUp && p.state != Fall) go(Fall, 0.2f);
    } else if (!p.wasGrounded && p.airTime > 0.35f) {
        go(Land, 0.05f, true);
        p.airTime = 0.0f;
    } else if (p.state == Land && !a.finished()) {
        // landing
    } else if (b.pushPose > 0.0f) {
        go(Push, 0.06f);
    } else if (b.pulling) {
        go(Pull, 0.2f);
    } else if (cheer) {
        go(Cheer, 0.3f);
    } else {
        go(Move, 0.2f);
    }
    if (b.grounded) p.airTime = 0.0f;
    p.wasGrounded = b.grounded;
    a.setParameter(speed);
    a.update(dt);
    if (std::vector<glm::mat4>* locals = m_models->boneLocals(p.instance)) kke::poseToLocals(a.pose(), *locals);
}

} // namespace party
