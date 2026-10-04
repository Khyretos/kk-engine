#include "Art.h"

#include "kke/KnownPacks.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace horde {

namespace fs = std::filesystem;

namespace {

int findCanonical(const kke::ModelData& m, const char* name) {
    const std::string want = kke::canonicalBoneName(name);
    for (size_t b = 0; b < m.bones.size(); ++b)
        if (kke::canonicalBoneName(m.bones[b].name) == want) return static_cast<int>(b);
    return -1;
}

std::string key(const ArtRef& r) { return r.pack + "/" + r.file + "/" + r.part; }

bool endsWith(const std::string& s, const std::string& end) { return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0; }

// What every character plays whatever the roster says.
const char* const kAlways[] = { "Idle_Loop", "Sword_Idle", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop", "Walk_Bwd_Loop", "Jog_Bwd_Loop",
                                "Jog_Left_Loop", "Jog_Right_Loop", "Roll", "Hit_Chest", "Hit_Head", "Hit_Shoulder_L", "Hit_Shoulder_R",
                                "Hit_Stomach", "Hit_Knockback", "LayToIdle", "KipUp", "Death01", "Death02", "Sword_Block", "Idle_Shield_Loop",
                                "Celebration", "Bow_Aim_Neutral", "Bow_Aim_Up", "Bow_Aim_Down", "Bow_Notch", "Bow_Shoot", "Pistol_Reload",
                                "Pistol_Aim_Neutral", "Interact", "Kick", "Melee_Knee", "Idle_Tired_Loop" };

} // namespace

int Rig::clip(const std::string& name) const {
    if (!set) return -1;
    const std::string tail = "|" + name;
    for (size_t c = 0; c < set->clipCount(); ++c) {
        const std::string& n = set->clipName(static_cast<int>(c));
        if (n == name || endsWith(n, tail)) return static_cast<int>(c);
    }
    return -1;
}

int Rig::clip(const std::vector<std::string>& names) const {
    for (const std::string& n : names)
        if (int c = clip(n); c >= 0) return c;
    return -1;
}

Art::Art(kke::ModelModule& models) : m_models(models) {}
Art::~Art() = default;

bool Art::init(const std::set<std::string>& clips, const std::vector<std::string>& packs) {
    auto log = kke::log::get("Horde");
    const char* base = SDL_GetBasePath();
    const std::string exe = base ? base : "";
    m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, exe);
    m_animDir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, exe);
    if (!m_packDir.empty()) {
        kke::CatalogScanOptions only;
        only.onlyPacks = packs; // not the whole shared cache
        m_catalog = kke::AssetCatalog::scan(m_packDir, only);
        for (const std::string& p : packs)
            if (!m_catalog.pack(p)) m_missing.push_back(p);
        if (!m_missing.empty()) {
            std::string list;
            for (const std::string& p : m_missing) list += (list.empty() ? "" : ", ") + p;
            log->info("packs not found under {}: {} (what needs them is drawn as the mannequin or left out)", m_packDir, list);
        }
    } else {
        log->info("no asset folder (assets/synty or KKE_ASSETS_DIR): everyone is the UAL mannequin");
    }

    // The animation library: UAL 1 (with the mannequin) and UAL 2's clips.
    const std::string ual1 = m_animDir.empty() ? std::string() : (fs::path(m_animDir) / "UAL1_Standard.fbx").string();
    std::error_code ec;
    if (ual1.empty() || !fs::exists(ual1, ec)) {
        log->info("animation library not found (assets/animations/UAL1_Standard.fbx): nobody can move, they are blocks");
        return false;
    }
    kke::ModelData ual;
    try {
        ual = kke::loadModel(ual1);
    } catch (const std::exception& e) {
        log->error("{}: {}", ual1, e.what());
        return false;
    }
    // The whole UAL 1 (Synty's pack folder) has the kick, the spells with
    // both hands, the strafes and the cheer the free Standard one lacks.
    if (const std::string full = kke::findPackFile("Universal Animation Library", "UAL1.fbx", exe); !full.empty()) {
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::appendClipsByBoneName(ual, kke::loadModel(full, o));
        } catch (const std::exception& e) {
            log->warn("UAL1.fbx: {}", e.what());
        }
    } else {
        log->info("the whole Universal Animation Library (UAL1.fbx) not found: some moves fall back to simpler clips");
    }
    std::string ual2 = (fs::path(m_animDir) / "UAL2.fbx").string();
    if (!fs::exists(ual2, ec)) ual2 = kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", exe);
    if (!ual2.empty()) {
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::appendClipsByBoneName(ual, kke::loadModel(ual2, o));
        } catch (const std::exception& e) {
            log->warn("UAL2.fbx: {}", e.what());
        }
    } else {
        log->info("UAL2.fbx not found: one sword swing for everything, no bows or spells (assets/animations/UAL2.fbx)");
    }
    // Only the clips the fight plays: retargeting all 240 onto every
    // skeleton would cost seconds and memory for nothing.
    std::set<std::string> keep(clips);
    for (const char* c : kAlways) keep.insert(c);
    std::vector<kke::ModelAnimation> kept;
    for (kke::ModelAnimation& a : ual.animations) {
        const size_t bar = a.name.rfind('|');
        const std::string shortName = bar == std::string::npos ? a.name : a.name.substr(bar + 1);
        if (keep.count(shortName)) kept.push_back(std::move(a));
    }
    for (const kke::ModelAnimation& a : kept) keep.erase(a.name.substr(a.name.rfind('|') + 1));
    if (!keep.empty()) {
        std::string list;
        for (const std::string& c : keep) list += (list.empty() ? "" : ", ") + c;
        log->info("clips not in the animation library (a fallback plays): {}", list);
    }
    ual.animations = std::move(kept);
    m_ual = std::make_unique<kke::ModelData>(std::move(ual));
    log->info("animation library: {} clips kept for the fight", m_ual->animations.size());
    return true;
}

const kke::CatalogAsset* Art::find(const std::string& pack, const std::string& file) const {
    // Prefer the plain FBX over the engine-specific copies some packs carry.
    const kke::CatalogAsset* best = nullptr;
    for (const kke::CatalogAsset& a : m_catalog.assets) {
        if (a.name != file) continue;
        if (!pack.empty() && a.pack != pack && kke::packBaseName(a.pack) != pack) continue;
        const bool engineCopy = a.path.find("Unreal") != std::string::npos || a.path.find("Godot") != std::string::npos;
        if (!best || !engineCopy) best = &a;
        if (!engineCopy) break;
    }
    return best;
}

Rig* Art::rigFor(const std::string& rigKey, const kke::ModelData& body) {
    auto it = m_rigs.find(rigKey);
    if (it != m_rigs.end()) return it->second.get();
    auto rig = std::make_unique<Rig>();
    rig->data.bones = body.bones;
    rig->data.boundsMin = body.boundsMin;
    rig->data.boundsMax = body.boundsMax;
    if (m_ual) {
        if (&body == m_ual.get()) {
            rig->data.animations = m_ual->animations;
        } else {
            const kke::BoneMatch match = kke::matchBones(*m_ual, body);
            rig->data.animations = kke::retargetAnimations(*m_ual, body, match);
            if (match.matched < 20) kke::log::get("Horde")->warn("{}: only {} bones take the animation library's clips", rigKey, match.matched);
        }
    }
    rig->set = std::make_unique<kke::AnimationSet>(rig->data);
    rig->yaw = -glm::degrees(std::atan2(kke::modelForward(rig->data).x, kke::modelForward(rig->data).z));
    rig->handR = findCanonical(rig->data, "hand_r");
    rig->handL = findCanonical(rig->data, "hand_l");
    rig->forearmR = findCanonical(rig->data, "lowerarm_r");
    rig->forearmL = findCanonical(rig->data, "lowerarm_l");
    rig->spine = findCanonical(rig->data, "spine_01");
    rig->chest = findCanonical(rig->data, "spine_03");
    if (rig->chest < 0) rig->chest = findCanonical(rig->data, "spine_02");
    rig->head = findCanonical(rig->data, "head");
    rig->rest = kke::poseToModel(rig->data, rig->set->restPose());
    rig->height = std::max(0.5f, body.boundsMax.y - body.boundsMin.y);
    Rig* raw = rig.get();
    m_rigs[rigKey] = std::move(rig);
    return raw;
}

std::vector<std::string> Art::skinTextures(const kke::ModelData& body) const {
    // Synty's colour variants: Pack_Texture_01_A.png, _B, _C... next to each other.
    std::vector<std::string> out;
    for (const kke::ModelMaterial& m : body.materials) {
        if (m.albedoTexture.empty()) continue;
        const fs::path p(m.albedoTexture);
        const std::string stem = p.stem().string();
        if (stem.size() < 2 || stem[stem.size() - 2] != '_') continue;
        const std::string prefix = stem.substr(0, stem.size() - 1);
        out.clear();
        for (char c = 'A'; c <= 'H'; ++c) {
            const fs::path variant = p.parent_path() / (prefix + c + p.extension().string());
            std::error_code ec;
            if (fs::exists(variant, ec)) out.push_back(variant.string());
        }
        if (!out.empty()) break;
    }
    return out;
}

int Art::skins(const ArtRef& ref) {
    if (ref.empty()) return 6; // the mannequin's colours
    const kke::CatalogAsset* a = find(ref.pack, ref.file);
    if (!a) return 1;
    auto it = m_files.find(a->path);
    if (it == m_files.end()) {
        if (!character(ref, 0)) return 1;
        it = m_files.find(a->path);
        if (it == m_files.end()) return 1;
    }
    return std::max<int>(1, static_cast<int>(skinTextures(*it->second).size()));
}

const Look* Art::character(const ArtRef& ref, int skin) {
    if (!m_ual || ref.empty()) return nullptr;
    const std::string k = key(ref) + "#" + std::to_string(skin);
    if (auto it = m_looks.find(k); it != m_looks.end()) return it->second.get();
    auto log = kke::log::get("Horde");
    const kke::CatalogAsset* a = find(ref.pack, ref.file);
    if (!a) {
        if (m_catalog.pack(ref.pack) && m_warned.insert(key(ref)).second) log->warn("{}: no {} in it", ref.pack, ref.file);
        m_looks[k] = nullptr;
        return nullptr;
    }
    auto file = m_files.find(a->path);
    if (file == m_files.end()) {
        try {
            file = m_files.emplace(a->path, std::make_unique<kke::ModelData>(kke::loadModel(a->path, kke::packLoadOptions(m_catalog, *a)))).first;
        } catch (const std::exception& e) {
            if (m_warned.insert(key(ref)).second) log->warn("{}: {}", a->path, e.what());
            m_looks[k] = nullptr;
            return nullptr;
        }
    }
    const kke::ModelData& whole = *file->second;
    kke::ModelData body;
    body.sourcePath = whole.sourcePath;
    body.bones = whole.bones;
    body.materials = whole.materials;
    if (ref.part.empty()) {
        body.meshes = whole.meshes;
    } else {
        for (const kke::ModelMesh& m : whole.meshes)
            if (m.name == ref.part) body.meshes.push_back(m);
        if (body.meshes.empty()) {
            if (m_warned.insert(key(ref)).second) log->warn("{}: no part {} in {}", ref.pack, ref.part, ref.file);
            m_looks[k] = nullptr;
            return nullptr;
        }
    }
    body.boundsMin = whole.boundsMin;
    body.boundsMax = whole.boundsMax;
    if (skin > 0) {
        const std::vector<std::string> skins = skinTextures(whole);
        if (skin < static_cast<int>(skins.size()))
            for (kke::ModelMaterial& m : body.materials)
                if (!m.albedoTexture.empty()) m.albedoTexture = skins[static_cast<size_t>(skin)];
    }
    auto look = std::make_unique<Look>();
    look->name = ref.part.empty() ? ref.file : ref.part;
    look->rig = rigFor(a->path, whole);
    look->triangles = body.triangleCount();
    look->model = m_models.add(std::move(body), "horde/" + k);
    if (!look->model) {
        m_looks[k] = nullptr;
        return nullptr;
    }
    const Look* raw = look.get();
    m_looks[k] = std::move(look);
    return raw;
}

const Look* Art::mannequin(const glm::vec3& color) {
    if (!m_ual) return nullptr;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "ual#%.2f,%.2f,%.2f", static_cast<double>(color.r), static_cast<double>(color.g), static_cast<double>(color.b));
    if (auto it = m_looks.find(buf); it != m_looks.end()) return it->second.get();
    kke::ModelData body = *m_ual;
    body.animations.clear();
    for (kke::ModelMaterial& mat : body.materials) mat.baseColor = mat.name.find("Joint") != std::string::npos ? glm::vec3(0.15f) : color;
    auto look = std::make_unique<Look>();
    look->name = "UAL mannequin";
    look->rig = rigFor("ual", *m_ual);
    look->triangles = body.triangleCount();
    look->model = m_models.add(std::move(body), std::string("horde/") + buf);
    const Look* raw = look->model ? look.get() : nullptr;
    m_looks[buf] = raw ? std::move(look) : nullptr;
    return raw;
}

kke::ModelModule::ModelId Art::prop(const ArtRef& ref) {
    if (ref.empty()) return 0;
    const std::string k = key(ref);
    if (auto it = m_props.find(k); it != m_props.end()) return it->second;
    const kke::CatalogAsset* a = find(ref.pack, ref.file);
    if (!a) {
        m_props[k] = 0;
        return 0;
    }
    try {
        kke::ModelData m = kke::loadModel(a->path, kke::packLoadOptions(m_catalog, *a));
        const glm::vec3 c = (m.boundsMin + m.boundsMax) * 0.5f;
        const glm::vec3 size = m.boundsMax - m.boundsMin;
        if (glm::length(glm::vec2(c.x, c.z)) > std::max(size.x, size.z)) {
            // Made away from its origin (Goblin War Camp's arrow): centre it.
            for (kke::ModelMesh& mesh : m.meshes)
                for (kke::ModelVertex& v : mesh.vertices) v.position -= c;
            m.boundsMin -= c;
            m.boundsMax -= c;
        }
        const kke::ModelModule::ModelId id = m_models.add(std::move(m), "horde/prop/" + k);
        m_props[k] = id;
        m_propSize[id] = size;
        return id;
    } catch (const std::exception& e) {
        kke::log::get("Horde")->warn("{}: {}", a->path, e.what());
        m_props[k] = 0;
        return 0;
    }
}

glm::vec3 Art::propSize(kke::ModelModule::ModelId prop) const {
    auto it = m_propSize.find(prop);
    return it == m_propSize.end() ? glm::vec3(0.0f) : it->second;
}

glm::mat4 Art::grip(const Rig& rig, const std::string& grip, bool left) const {
    const int handBone = left ? rig.handL : rig.handR;
    const int lowerBone = left ? rig.forearmL : rig.forearmR;
    if (handBone < 0) return glm::mat4(1.0f);
    const glm::mat4 hand = rig.rest[static_cast<size_t>(handBone)];
    const glm::vec3 handPos(hand[3]);
    const glm::vec3 fwd = kke::modelForward(rig.data);
    const glm::vec3 armDir = lowerBone >= 0 ? glm::normalize(handPos - glm::vec3(rig.rest[static_cast<size_t>(lowerBone)][3])) : glm::vec3(left ? 1.0f : -1.0f, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    // The prop's +Y (blade, shaft, bow limb) onto a direction in the rest
    // pose (arms out, palms down): forward for a weapon in the fist, up for
    // a staff. A bow and a crossbow are made for aiming, where the arm
    // points ahead and the wrist has turned the palm inward: what was
    // forward in the rest pose is then up (a bow's limbs), what was up is
    // then across (a crossbow's limbs), and the crossbow's stock runs along
    // the arm. The palm is a few centimetres past the wrist.
    glm::vec3 y = fwd;
    glm::vec3 at = handPos + armDir * 0.07f;
    if (grip == "staff") y = up;
    if (grip == "bow") at = handPos + armDir * 0.06f;
    if (grip == "crossbow") {
        y = up;
        at = handPos + armDir * 0.2f;
    }
    const glm::vec3 x = glm::normalize(glm::cross(y, armDir));
    const glm::vec3 z = glm::cross(x, y); // the arm, for a crossbow: its stock
    glm::mat4 want = glm::translate(glm::mat4(1.0f), at) * glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(0, 0, 0, 1));
    if (grip == "twohand") want = want * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.18f, 0.0f)); // held further down the haft
    return glm::inverse(hand) * want;
}

glm::mat4 Art::attach(const Rig& rig, int bone) const {
    if (bone < 0) return glm::mat4(1.0f);
    // Made in a Synty character's axes (facing +z) at the bone's rest
    // position; turned round for a model that faces the other way.
    const glm::mat4 rest = rig.rest[static_cast<size_t>(bone)];
    return glm::inverse(rest) * glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(rest[3])), glm::radians(-rig.yaw), glm::vec3(0, 1, 0));
}

} // namespace horde
