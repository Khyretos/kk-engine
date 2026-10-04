#include "kke/PeopleLibrary.h"

#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"

#include <SDL3/SDL.h>

#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace kke {

namespace {

struct PersonAsset {
    const char* name;
    const char* asset;
};
// POLYGON City Characters: everyday people.
const PersonAsset kPeople[] = {
    { "Jock", "SK_Character_Jock" },           { "Tourist", "SK_Character_Tourist" },
    { "Firefighter", "SK_Character_FireFighter" }, { "Paramedic", "SK_Character_Paramedic" },
    { "Grandpa", "SK_Character_Grandpa" },     { "Grandma", "SK_Character_Grandma" },
    { "Hipster", "SK_Character_HipsterGirl" }, { "Punk", "SK_Character_PunkGuy" },
    { "Beachgoer", "SK_Character_SummerGirl" }, { "Roadworker", "SK_Character_Roadworker" },
    { "Hot dog", "SK_Character_Hotdog" },
};
constexpr int kCount = static_cast<int>(sizeof(kPeople) / sizeof(kPeople[0]));
const char* const kPack = "POLYGON_City_Characters";

} // namespace

const std::vector<std::string>& PeopleLibrary::names() {
    static const std::vector<std::string> n = [] {
        std::vector<std::string> out;
        for (const PersonAsset& p : kPeople) out.push_back(p.name);
        return out;
    }();
    return n;
}

const char* PeopleLibrary::assetOf(int person) { return person >= 1 && person <= kCount ? kPeople[person - 1].asset : ""; }

void PeopleLibrary::scan(const std::string& logName) {
    m_log = logName;
    m_paths.assign(static_cast<size_t>(kCount), std::string());
    m_options.assign(static_cast<size_t>(kCount), ModelLoadOptions{});
    m_bodies.clear();
    m_bodies.resize(static_cast<size_t>(kCount));
    m_tried.assign(static_cast<size_t>(kCount), false);
    m_found = 0;
    const char* base = SDL_GetBasePath();
    const std::string dir = findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    if (dir.empty()) {
        log::get(m_log)->info("people: no Synty packs (assets/synty or KKE_ASSETS_DIR), so everyone is the mannequin");
        return;
    }
    CatalogScanOptions only;
    only.onlyPacks = { kPack };
    const AssetCatalog catalog = AssetCatalog::scan(dir, only);
    for (int i = 0; i < kCount; ++i) {
        const CatalogAsset* a = catalog.find(kPeople[i].asset, { kPack });
        if (!a) continue;
        m_paths[static_cast<size_t>(i)] = a->path;
        m_options[static_cast<size_t>(i)] = packLoadOptions(catalog, *a);
        ++m_found;
    }
    log::get(m_log)->info("people: {} of {} City characters found", m_found, kCount);
}

bool PeopleLibrary::has(int person) const {
    return person >= 1 && person <= kCount && static_cast<size_t>(person - 1) < m_paths.size() && !m_paths[static_cast<size_t>(person - 1)].empty();
}

const PeopleLibrary::Body* PeopleLibrary::body(int person, ModelModule& models, const ModelData& rig) {
    if (!has(person)) return nullptr;
    const size_t i = static_cast<size_t>(person - 1);
    if (m_tried[i]) return m_bodies[i].get();
    m_tried[i] = true;
    // Loaded the first time someone picks them (loading all of them up
    // front is slow).
    try {
        ModelData model = loadModel(m_paths[i], m_options[i]);
        auto b = std::make_unique<Body>();
        b->retarget = PoseRetarget(rig, model);
        if (!b->retarget.valid()) {
            log::get(m_log)->warn("people: {}: no bone matches the rig's", assetOf(person));
            return nullptr;
        }
        const BoneMatch& match = b->retarget.match();
        log::get(m_log)->info("people: {} ready ({} of {} bones follow the rig)", assetOf(person), match.matched, model.bones.size());
        b->model = models.add(std::move(model), std::string("people/") + assetOf(person));
        if (!b->model) return nullptr;
        m_bodies[i] = std::move(b);
    } catch (const std::exception& e) {
        log::get(m_log)->warn("people: {}: {}", assetOf(person), e.what());
    }
    return m_bodies[i].get();
}

void PeopleLibrary::follow(ModelModule& models, ModelModule::InstanceId instance, const Body& body, const std::vector<glm::mat4>& rigLocals,
                           const glm::mat4& rigTransform) {
    if (std::vector<glm::mat4>* locals = models.boneLocals(instance)) body.retarget.apply(rigLocals, *locals);
    models.setTransform(instance, rigTransform * body.retarget.placement());
}

} // namespace kke
