#include "kke/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <map>
#include <cstdlib>
#include <regex>
#include <set>

namespace kke {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool isModelFile(const fs::path& p) {
    std::string ext = lower(p.extension().string());
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

// Folder names that mean "this is inside a pack", never a pack itself.
bool isAssetTypeFolder(const std::string& name) {
    static const std::set<std::string> kNames = {
        "characters", "character", "fbx", "obj", "staticmeshes", "static_meshes", "meshes", "models", "textures",
        "materials", "prefabs", "_sourcefiles", "sourcefiles", "source", "animations", "props", "environment",
        "buildings", "vehicles", "weapons", "fx", "icons",
    };
    return kNames.count(lower(name)) != 0;
}

bool containsModels(const fs::path& dir) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && isModelFile(it->path())) return true;
    }
    return false;
}

const std::vector<std::string>& categoryOrder() {
    static const std::vector<std::string> order = { "Characters", "Character parts", "Buildings", "Props", "Environment",
                                                    "Vehicles", "Weapons", "FX", "Primitives", "Icons", "Scenes", "Other" };
    return order;
}

} // namespace

std::string categoryForAssetName(const std::string& name) {
    std::string n = lower(name);
    if (startsWith(n, "sk_")) return "Characters";
    if (startsWith(n, "sm_chr_") || startsWith(n, "sm_character")) return "Character parts";
    if (startsWith(n, "sm_bld_") || startsWith(n, "sm_building")) return "Buildings";
    if (startsWith(n, "sm_prop_")) return "Props";
    if (startsWith(n, "sm_env_") || startsWith(n, "sm_generic_") || startsWith(n, "sm_nature")) return "Environment";
    if (startsWith(n, "sm_veh_") || startsWith(n, "sm_vehicle")) return "Vehicles";
    if (startsWith(n, "sm_wep_") || startsWith(n, "sm_weapon")) return "Weapons";
    if (startsWith(n, "fx_") || startsWith(n, "sm_fx_")) return "FX";
    if (startsWith(n, "sm_primitive")) return "Primitives";
    if (startsWith(n, "sm_icon")) return "Icons";
    if (n.find("demo") != std::string::npos || n.find("scene") != std::string::npos) return "Scenes";
    return "Other";
}

AssetCatalog AssetCatalog::scan(const std::string& rootPath) {
    AssetCatalog catalog;
    std::error_code ec;
    fs::path root(rootPath);
    if (!fs::is_directory(root, ec)) return catalog;

    // Decide what the packs are.
    std::vector<fs::path> packDirs;
    bool rootIsPack = false;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) {
            if (entry.is_regular_file(ec) && isModelFile(entry.path())) rootIsPack = true;
            continue;
        }
        std::string name = entry.path().filename().string();
        if (isAssetTypeFolder(name)) rootIsPack = true;
        else if (containsModels(entry.path())) packDirs.push_back(entry.path());
    }
    if (rootIsPack || packDirs.empty()) {
        packDirs.clear();
        if (containsModels(root)) packDirs.push_back(root);
    }
    std::sort(packDirs.begin(), packDirs.end());

    for (const fs::path& dir : packDirs) {
        CatalogPack pack;
        pack.name = fs::weakly_canonical(dir, ec).filename().string();
        if (pack.name.empty()) pack.name = dir.filename().string();
        pack.root = dir.string();

        std::map<std::string, fs::path> byStem; // lower-case stem -> chosen file
        std::vector<fs::path> images;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                if (lower(p.filename().string()) == "textures") pack.textureDirs.push_back(p.string());
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = lower(p.extension().string());
            if (ext == ".png" || ext == ".tga" || ext == ".jpg") images.push_back(p);
            if (!isModelFile(p)) continue;
            std::string key = lower(p.stem().string());
            auto found = byStem.find(key);
            // Prefer FBX over OBJ (FBX carries skeletons/materials);
            // otherwise keep the first seen in sorted order for stability.
            if (found == byStem.end() || (lower(found->second.extension().string()) != ".fbx" && ext == ".fbx")) byStem[key] = p;
        }
        std::sort(pack.textureDirs.begin(), pack.textureDirs.end());
        std::sort(images.begin(), images.end());
        for (const fs::path& img : images) {
            std::string n = lower(img.stem().string());
            bool helper = n.find("emission") != std::string::npos || n.find("normal") != std::string::npos ||
                          n.find("_mask") != std::string::npos || n.find("metallic") != std::string::npos;
            if (helper) continue;
            if (n.find("grid") != std::string::npos) { pack.overlayTextures.push_back(img.string()); continue; }
            if (img.string().find(".mayaSwatches") != std::string::npos) continue; // Maya's thumbnails
            // "..._texture_NN" or "..._texture_NN_X" (Town's _01_A, _01_B),
            // or a bare "Texture_01" (small packs, Pirates): a variant of the atlas.
            static const std::regex kVariant(R"((^|_)texture_\d+(_[a-z0-9]{1,3})?$)");
            if (std::regex_search(n, kVariant)) {
                pack.textureVariants.push_back(img.string());
                if (pack.defaultTexture.empty() && n.find("_texture_01") != std::string::npos) pack.defaultTexture = img.string();
            }
        }
        if (pack.defaultTexture.empty() && !pack.textureVariants.empty()) pack.defaultTexture = pack.textureVariants.front();
        if (pack.defaultTexture.empty()) {
            // Packs that don't follow the _Texture_01 naming (Nature's
            // "PolygonNature_01", Pumpkin's "POLYGON_Pumpkin_Tex", a lone
            // "Adults.png"): the atlas is the shallowest image, preferring
            // atlas-like names over ground/leaf/grass detail textures.
            int bestScore = 1 << 30;
            for (const fs::path& img : images) {
                std::string n = lower(img.stem().string());
                if (img.string().find(".mayaSwatches") != std::string::npos || n.find("emission") != std::string::npos ||
                    n.find("normal") != std::string::npos || n.find("_mask") != std::string::npos || n.find("grid") != std::string::npos)
                    continue;
                const fs::path rel = fs::relative(img, dir, ec);
                const int depth = static_cast<int>(std::distance(rel.begin(), rel.end()));
                int score = depth * 10;
                if (n.find("polygon") != std::string::npos || n.find("tex") != std::string::npos) score -= 5;
                if (n.find("_01") != std::string::npos) score -= 2;
                if (score < bestScore) { bestScore = score; pack.defaultTexture = img.string(); }
            }
        }

        for (auto& [key, path] : byStem) {
            CatalogAsset a;
            a.name = path.stem().string();
            a.path = path.string();
            a.pack = pack.name;
            a.category = categoryForAssetName(a.name);
            a.skinned = a.category == "Characters";
            catalog.assets.push_back(std::move(a));
            ++pack.assetCount;
        }
        catalog.packs.push_back(std::move(pack));
    }

    auto rank = [](const std::string& c) {
        const auto& order = categoryOrder();
        return static_cast<int>(std::find(order.begin(), order.end(), c) - order.begin());
    };
    std::sort(catalog.assets.begin(), catalog.assets.end(), [&](const CatalogAsset& x, const CatalogAsset& y) {
        if (x.pack != y.pack) return x.pack < y.pack;
        if (x.category != y.category) return rank(x.category) < rank(y.category);
        return x.name < y.name;
    });
    return catalog;
}

const CatalogPack* AssetCatalog::pack(const std::string& name) const {
    for (const CatalogPack& p : packs) if (p.name == name) return &p;
    return nullptr;
}

std::vector<std::string> AssetCatalog::categories() const {
    std::vector<std::string> result;
    for (const std::string& c : categoryOrder()) {
        if (std::any_of(assets.begin(), assets.end(), [&](const CatalogAsset& a) { return a.category == c; })) result.push_back(c);
    }
    return result;
}

std::vector<const CatalogAsset*> AssetCatalog::filter(const std::string& packName, const std::string& category, const std::string& search) const {
    std::string needle = lower(search);
    std::vector<const CatalogAsset*> result;
    for (const CatalogAsset& a : assets) {
        if (!packName.empty() && a.pack != packName) continue;
        if (!category.empty() && a.category != category) continue;
        if (!needle.empty() && lower(a.name).find(needle) == std::string::npos) continue;
        result.push_back(&a);
    }
    return result;
}

const CatalogAsset* AssetCatalog::find(const std::string& name) const {
    for (const CatalogAsset& a : assets) if (a.name == name) return &a;
    return nullptr;
}

const CatalogAsset* AssetCatalog::find(const std::string& name, const std::vector<std::string>& preferred) const {
    for (const std::string& want : preferred) {
        for (const CatalogAsset& a : assets) {
            if (a.name != name) continue;
            const CatalogPack* p = pack(a.pack);
            if (a.pack == want || (p && fs::path(p->root).filename().string() == want)) return &a;
        }
    }
    return find(name);
}

std::string findAssetFolder(const std::string& relative, const std::vector<std::string>& envVars, const std::string& executableDir,
                            std::vector<std::string>* searched) {
    std::error_code ec;
    auto consider = [&](const fs::path& p) {
        if (searched) searched->push_back(p.string());
        return fs::is_directory(p, ec);
    };
    for (const std::string& var : envVars) {
        if (const char* v = std::getenv(var.c_str())) {
            if (consider(v)) return fs::weakly_canonical(v, ec).string();
        }
    }
    for (fs::path base : { fs::current_path(ec), fs::path(executableDir) }) {
        if (base.empty()) continue;
        for (int up = 0; up <= 4; ++up) {
            fs::path candidate = base / relative;
            if (consider(candidate)) return fs::weakly_canonical(candidate, ec).string();
            if (!base.has_parent_path() || base.parent_path() == base) break;
            base = base.parent_path();
        }
    }
    return {};
}

namespace {
// Words of a name, minus Synty prefixes and numbers, lower case.
std::vector<std::string> nameWords(const std::string& name) {
    std::vector<std::string> words;
    std::string word;
    for (char c : name + "_") {
        if (c == '_' || c == ' ' || c == ':') {
            if (word.size() >= 4 && word != "Generic") words.push_back(lower(word));
            word.clear();
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            word += c;
        }
    }
    return words;
}
} // namespace

std::string AssetCatalog::namedTexture(const CatalogAsset& asset, const std::string& material) const {
    const CatalogPack* p = pack(asset.pack);
    if (!p) return {};
    const std::vector<std::string> assetWords = nameWords(asset.name);
    const std::vector<std::string> materialWords = nameWords(material);
    if (!material.empty() && materialWords.empty()) return {};
    std::string best;
    int bestScore = 0;
    std::error_code ec;
    for (const std::string& dir : p->textureDirs) {
        for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::follow_directory_symlink, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string stem = lower(e.path().stem().string());
            const std::string ext = lower(e.path().extension().string());
            if (ext != ".png" && ext != ".tga" && ext != ".jpg") continue;
            if (stem.find("normal") != std::string::npos || stem.find("emissive") != std::string::npos ||
                stem.find("metallic") != std::string::npos || stem.find("mask") != std::string::npos)
                continue;
            auto has = [&](const std::string& w) { return stem.find(w) != std::string::npos; };
            int score = 0;
            if (material.empty()) {
                // The pack atlas isn't "named after" anything.
                if (has("texture")) continue;
                for (const std::string& w : assetWords)
                    if (stem.find("_" + w + "_") != std::string::npos ||
                        (stem.size() > w.size() && stem.compare(stem.size() - w.size() - 1, std::string::npos, "_" + w) == 0))
                        score = 2;
            } else {
                bool named = false;
                for (const std::string& w : materialWords) named = named || has(w);
                if (!named) continue;
                score = 1;
                for (const std::string& w : assetWords) if (has(w)) score += 2;
                if (has("generic")) score += 1;
            }
            const std::string path = e.path().string();
            if (score > bestScore || (score == bestScore && score > 0 && path.size() < best.size())) {
                bestScore = score;
                best = path;
            }
        }
    }
    return best;
}

} // namespace kke
