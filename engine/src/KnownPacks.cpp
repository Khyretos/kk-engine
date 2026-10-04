#include "kke/KnownPacks.h"
#include "kke/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <system_error>

namespace kke {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Markers are files the games themselves load where possible, so a pack
// that has them is one the games can use. Names were checked against
// the extracted packs (no other pack in the share has them).
const std::vector<KnownPack> kPacks = {
    { "POLYGON_Town", "Environment", "assets/synty", { "SK_Character_Father_01.fbx", "PolygonTown_Texture_01_A.png" },
      { "kke_demo", "flying_demo", "pet_companion", "sandbox", "synty_demo" }, "houses, shops, streets and townspeople" },
    { "POLYGON_Nature", "Environment", "assets/synty", { "River_Plane_WaterFall_01.fbx", "MountainSkybox.fbx" },
      { "kke_demo", "racing", "synty_demo" }, "trees, rocks, grass and water" },
    { "POLYGON_Farm", "Environment", "assets/synty", { "SK_Chr_Farmer_Male_01.fbx", "PolygonFarm_Texture_01_A.png" },
      { "farm_demo" }, "barns, fences, crops and farmers" },
    { "POLYGON_Prototype", "Environment", "assets/synty", { "SK_Character_Dummy_Male_01.fbx", "PolygonPrototype_Texture_01.png" },
      { "platoon", "jiggle_demo", "synty_demo" }, "grey-box walls and crates, the dummy character" },
    { "POLYGON_Street_Racer", "Vehicles", "assets/synty", { "PolygonStreetRacer_Veh_Tex_01_Race_Purple.png" },
      { "racing" }, "cars, paint jobs and track props" },
    { "POLYGON_StuntPlane", "Vehicles", "assets/synty", { "SM_Veh_Plane_Stunt_01.fbx" }, { "flying_demo" }, "the stunt plane" },
    { "POLYGON_Shops", "Props", "assets/synty", { "SM_Prop_Sport_Tennis_Racket_01.fbx", "PolygonShops_Texture_01_A.png" },
      { "tennis" }, "the tennis racket and ball" },
    { "POLYGON_City_Characters", "Characters", "assets/synty", { "SK_Character_FireFighter.fbx", "SK_Character_FastFoodGuy.fbx" },
      { "party", "jiggle_demo" }, "modern-day people" },
    { "POLYGON_Fantasy_Characters", "Characters", "assets/synty", { "SK_Character_Female_Druid.fbx", "SK_Character_Female_Queen.fbx" },
      { "goblin_horde", "jiggle_demo" }, "knights, kings, peasants and wizards" },
    { "POLYGON_Dogs", "Characters", "assets/synty", { "Unity_SK_Animals_Dog_01.fbx" }, { "farm_demo", "pet_companion" },
      "the farm dog and the fox; every pet breed, its toys, bowls and the agility course" },
    { "POLYGON_Goblin_War_Camp", "Characters", "assets/synty", { "SM_Chr_Attach_Goggles_01.fbx", "SM_Wep_Bow_01.fbx" },
      { "goblin_horde" }, "goblins, their bows, staffs, clubs and hats (without it, mannequins stand in)" },
    { "POLYGON_Dungeon_Pack", "Characters", "assets/synty", { "Character_Goblin_WarChief.fbx" }, { "goblin_horde" },
      "dungeon heroes, goblin chiefs and shamans, big axes and hammers" },
    { "POLYGON_Fantasy_Rivals", "Characters", "assets/synty", { "SK_BR_Character_Troll_01.fbx" }, { "goblin_horde" },
      "the giants: troll, big ork, barbarian, pig butcher" },
    { "SIDEKICK_Goblin_Fighters", "Characters", "assets/synty", { "GoblinFighter_01.sk" }, {},
      "modular Sidekick goblins (kke/Sidekick.h reads them)" },
    { "Farm Animals Animated  by Quaternius", "Characters", "assets/synty", { "Llama.fbx" },
      { "farm_demo", "pet_companion", "synty_demo" }, "sheep, cows, pigs, horses and the pug (CC0)" },
    { "ANIMATION_Goblin_Locomotion", "Animations", "assets/synty", { "A_MOD_GBL_BodyLook_D_Additive_Neut.fbx" }, {},
      "goblin walk, run and attack clips for the Sidekick goblins" },
    { "Universal Animation Library 2", "Animations", "assets/synty", { "UAL2.fbx" }, { "duel", "goblin_horde", "tennis", "kke_demo" },
      "extra mannequin clips: sword swings, vaults, climbs (CC0)" },
    { "Universal Animation Library", "Animations", "assets/synty", { "UAL1.fbx" }, {},
      "the full mannequin library (CC0); the games already ship its Standard set" },
    { "ANIMATION_Base_Locomotion", "Animations", "assets/synty", { "A_BodyLook_Additive_Neut.fbx" }, {}, "Synty's walk, run and jump clips" },
    { "POLYGON_Particle_FX", "Effects", "assets/synty", { "FX_CrystalShard_01.fbx" }, {}, "Synty's particle meshes and textures" },
    { "SIDEKICK_Starter", "Characters", "assets/synty", { "StarterPack.uproject" }, {}, "an Unreal project: KKE can't read .uasset files" },
    { "Fish Pack Animated by Quaternius", "Characters", "assets/synty", { "Manta ray.fbx" }, {}, "fish, sharks and a dolphin (CC0)" },
    { "Easy Animated Enemy Pack", "Characters", "assets/synty", { "Snake_angry.fbx" }, {}, "small animated enemies" },
    { "Xelu_Free_Controller&Key_Prompts", "Sprites", "assets/sprites", { "XboxSeriesX_A.png" }, {},
      "button glyphs (CC0); the engine already ships them in assets/prompts/xelu" },
    { "INTERFACE_Apocalypse_HUD", "Sprites", "assets/sprites", { "SPR_Apocalypse_Bar_Metal01.png" }, {}, "HUD bars, frames, cursors and item icons" },
    { "INTERFACE_SciFi_Soldier_HUD", "Sprites", "assets/sprites", {}, {}, "HUD bars, frames and icons" },
};

// marker file name (lower case) -> pack
const std::map<std::string, const KnownPack*>& markerIndex() {
    static const std::map<std::string, const KnownPack*> index = [] {
        std::map<std::string, const KnownPack*> m;
        for (const KnownPack& p : kPacks)
            for (const std::string& marker : p.markers) m[lower(marker)] = &p;
        return m;
    }();
    return index;
}

std::mutex g_cacheMutex;
std::map<std::string, PackIdentity> g_identityCache; // canonical folder -> answer

std::string canonical(const std::string& folder) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(fs::path(folder), ec), ec);
    return ec ? folder : p.string();
}

bool namedLikeAPack(const std::string& folderName) {
    const std::string key = packKey(folderName);
    for (const char* prefix : { "polygon", "sidekick", "animation", "interface", "synty" })
        if (key.rfind(prefix, 0) == 0) return true;
    return false;
}

std::vector<fs::path> sortedSubfolders(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec)) out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

const std::vector<KnownPack>& knownPacks() { return kPacks; }

std::string packKey(const std::string& nameOrFolder) {
    static const std::regex kVersion(R"([_ .-]?v[0-9]+([._][0-9]+)*$)", std::regex::icase);
    std::string base = std::regex_replace(packBaseName(nameOrFolder), kVersion, "");
    std::string key;
    for (unsigned char c : base)
        if (std::isalnum(c)) key += static_cast<char>(std::tolower(c));
    // "Pack" on the end is spelling, not a name: Synty sells "POLYGON -
    // Dungeon Pack" and it unzips as POLYGON_Dungeon (soucouyant), while
    // POLYGON_Pirate_Pack keeps it.
    if (key.size() > 8 && key.ends_with("pack")) key.resize(key.size() - 4);
    return key.empty() ? lower(nameOrFolder) : key;
}

bool samePack(const std::string& a, const std::string& b) { return a == b || packKey(a) == packKey(b); }

const KnownPack* knownPack(const std::string& nameOrFolder) {
    const std::string key = packKey(nameOrFolder);
    for (const KnownPack& p : kPacks)
        if (packKey(p.name) == key) return &p;
    return nullptr;
}

PackIdentity identifyPackFolder(const std::string& folder) {
    const fs::path path(folder);
    std::string name = path.filename().string();
    if (name.empty() || name == ".") name = fs::path(canonical(folder)).filename().string();
    if (const KnownPack* p = knownPack(name)) return { p->name, PackIdentity::How::Name };
    const std::string key = canonical(folder);
    // A link named anything that points at a pack's folder.
    if (const KnownPack* p = knownPack(fs::path(key).filename().string())) return { p->name, PackIdentity::How::Name };

    PackIdentity result{ packBaseName(name), PackIdentity::How::Unknown };
    // A folder named the way Synty names its packs is the pack its name
    // says ("POLYGON_Casino" is Casino, not a renamed Street Racer): not
    // searched, so a game asking for two packs out of seventy doesn't read
    // the other sixty-eight.
    if (namedLikeAPack(name)) return result;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto found = g_identityCache.find(key);
        if (found != g_identityCache.end()) return found->second;
    }
    const auto& index = markerIndex();
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        auto hit = index.find(lower(it->path().filename().string()));
        if (hit != index.end()) {
            result = { hit->second->name, PackIdentity::How::Contents };
            break;
        }
    }
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_identityCache[key] = result;
    return result;
}

std::string findPackFolder(const std::string& root, const std::string& pack) {
    if (root.empty()) return {};
    const std::vector<fs::path> top = sortedSubfolders(root);
    // Names first (no reading inside folders), then contents.
    for (const fs::path& dir : top)
        if (samePack(dir.filename().string(), pack)) return dir.string();
    for (const fs::path& dir : top)
        if (!knownPack(dir.filename().string()))
            for (const fs::path& inner : sortedSubfolders(dir))
                if (samePack(inner.filename().string(), pack)) return inner.string();
    for (const fs::path& dir : top) {
        if (knownPack(dir.filename().string())) continue; // a known name is that pack, not this one
        PackIdentity id = identifyPackFolder(dir.string());
        if (id.how == PackIdentity::How::Contents && samePack(id.name, pack)) return dir.string();
    }
    return {};
}

std::string findFileInPack(const std::string& root, const std::string& pack, const std::string& fileName) {
    const std::string dir = findPackFolder(root, pack);
    if (dir.empty()) return {};
    std::vector<fs::path> hits;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->path().filename() == fileName && it->is_regular_file(ec)) hits.push_back(it->path());
    if (hits.empty()) return {};
    std::sort(hits.begin(), hits.end());
    return hits.front().string();
}

std::string findPackFile(const std::string& pack, const std::string& fileName, const std::string& executableDir) {
    return findFileInPack(findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, executableDir), pack, fileName);
}

std::string PackContents::kind() const {
    if (files == 0) return "Empty";
    if (models + sidekick > 0) {
        if (animations * 2 > models) return "Animations";
        if (sidekick > 0 || skinned * 2 > models) return "Characters";
        return "Models";
    }
    if (engineOnly * 2 > files) return "Unreal/Unity project";
    const size_t most = std::max({ images, audio, fonts });
    if (most == 0) return "Other";
    if (most == images) return "Sprites";
    if (most == audio) return "Audio";
    return "Fonts";
}

std::string PackContents::folder() const {
    const std::string k = kind();
    if (k == "Characters" || k == "Animations" || k == "Models") return "assets/synty";
    if (k == "Sprites") return "assets/sprites";
    return {};
}

PackContents analyzePackFolder(const std::string& folder) {
    static const std::set<std::string> kModel = { ".fbx", ".obj", ".gltf", ".glb" };
    static const std::set<std::string> kImage = { ".png", ".tga", ".jpg", ".jpeg", ".psd" };
    static const std::set<std::string> kAudio = { ".wav", ".ogg", ".mp3", ".flac" };
    static const std::set<std::string> kFont = { ".ttf", ".otf" };
    static const std::set<std::string> kEngine = { ".uasset", ".umap", ".unitypackage", ".prefab", ".unity", ".mat", ".meta" };
    PackContents c;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (p.string().find(".mayaSwatches") != std::string::npos) continue; // Maya's thumbnails
        ++c.files;
        const std::string ext = lower(p.extension().string());
        const std::string stem = lower(p.stem().string());
        if (kModel.count(ext)) {
            ++c.models;
            if (stem.rfind("sk_", 0) == 0) ++c.skinned;
            if (stem.rfind("a_", 0) == 0 || lower(p.string()).find("animation") != std::string::npos) ++c.animations;
        } else if (kImage.count(ext)) ++c.images;
        else if (kAudio.count(ext)) ++c.audio;
        else if (kFont.count(ext)) ++c.fonts;
        else if (ext == ".sk") ++c.sidekick;
        else if (kEngine.count(ext)) ++c.engineOnly;
    }
    return c;
}

} // namespace kke
