#include "kke/SpriteCatalog.h"
#include "kke/AssetCatalog.h"
#include "kke/KnownPacks.h"
#include "kke/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <system_error>

namespace kke {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isImage(const fs::path& p) {
    static const std::set<std::string> kExt = { ".png", ".tga", ".jpg", ".jpeg", ".bmp" };
    return kExt.count(lower(p.extension().string())) != 0;
}

constexpr const char* kPrefix = "sprite:";

} // namespace

SpriteCatalog SpriteCatalog::scan(const std::string& root) {
    SpriteCatalog catalog;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return catalog;
    std::set<std::string> packs;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().filename().string().rfind('.', 0) == 0) { // .mayaSwatches and other hidden folders
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && isImage(it->path())) files.push_back(it->path());
    }
    if (ec) log::get("Assets")->warn("can't read all of the sprite folder '{}': {}", root, ec.message());
    std::sort(files.begin(), files.end());
    std::map<std::string, std::string> packNames; // top folder -> pack name
    for (const fs::path& file : files) {
        const fs::path rel = file.lexically_relative(root);
        Sprite s;
        s.name = file.stem().string();
        s.path = file.string();
        if (std::distance(rel.begin(), rel.end()) > 1) {
            const std::string top = rel.begin()->string();
            auto known = packNames.find(top);
            if (known == packNames.end()) known = packNames.emplace(top, identifyPackFolder((fs::path(root) / top).string()).name).first;
            s.pack = known->second;
        } else {
            s.pack = "sprites";
        }
        s.category = file.parent_path() == fs::path(root) ? std::string() : file.parent_path().filename().string();
        packs.insert(s.pack);
        catalog.sprites.push_back(std::move(s));
    }
    catalog.packs.assign(packs.begin(), packs.end());
    std::stable_sort(catalog.sprites.begin(), catalog.sprites.end(), [](const Sprite& a, const Sprite& b) {
        if (a.pack != b.pack) return a.pack < b.pack;
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    return catalog;
}

const Sprite* SpriteCatalog::find(const std::string& name, const std::string& pack) const {
    if (!pack.empty())
        for (const Sprite& s : sprites)
            if (s.name == name && samePack(s.pack, pack)) return &s;
    for (const Sprite& s : sprites)
        if (s.name == name) return &s;
    return nullptr;
}

std::string findSpriteFolder(const std::string& executableDir) { return findAssetFolder("assets/sprites", { "KKE_SPRITES_DIR" }, executableDir); }

bool isSpriteReference(const std::string& reference) { return reference.rfind(kPrefix, 0) == 0; }

std::string resolveSprite(const std::string& reference, const std::string& executableDir) {
    if (!isSpriteReference(reference)) return {};
    static std::mutex mutex;
    static bool scanned = false;
    static SpriteCatalog catalog;
    std::lock_guard<std::mutex> lock(mutex);
    if (!scanned) {
        scanned = true;
        const std::string root = findSpriteFolder(executableDir);
        catalog = SpriteCatalog::scan(root);
        if (!root.empty()) log::get("Assets")->info("sprites: {} in {} pack(s) in {}", catalog.sprites.size(), catalog.packs.size(), root);
    }
    std::string name = reference.substr(std::char_traits<char>::length(kPrefix));
    std::string pack;
    if (const size_t slash = name.rfind('/'); slash != std::string::npos) {
        pack = name.substr(0, slash);
        name = name.substr(slash + 1);
    }
    const Sprite* s = catalog.find(name, pack);
    return s ? s->path : std::string();
}

} // namespace kke
