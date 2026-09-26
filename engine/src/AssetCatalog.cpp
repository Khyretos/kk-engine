#include "kke/AssetCatalog.h"
#include "kke/Log.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <map>
#include <cstdlib>
#include <regex>
#include <set>
#include <thread>

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

// Errors worth trying again: network shares and busy devices recover.
bool isTransient(const std::error_code& ec) {
    if (ec == std::errc::io_error || ec == std::errc::resource_unavailable_try_again || ec == std::errc::interrupted ||
        ec == std::errc::device_or_resource_busy || ec == std::errc::timed_out)
        return true;
#if defined(ESTALE) && !defined(_WIN32)
    if (ec.category() == std::system_category() && ec.value() == ESTALE) return true; // NFS handle went stale
#endif
    return false;
}

// Reads the file system for one scan: every read is retried on transient
// errors, and what still fails is recorded (and logged), never dropped.
class Walker {
public:
    Walker(const CatalogScanOptions& options, std::vector<CatalogScanError>& errors) : m_options(options), m_errors(errors) {}

    // Runs `op(ec, attempt)` until it succeeds, fails for good, or runs
    // out of attempts. Returns the last error (empty on success).
    template <class Op>
    std::error_code retry(Op op) {
        std::error_code ec;
        const int attempts = std::max(1, m_options.attempts);
        int delayMs = m_options.firstRetryDelayMs;
        for (int attempt = 1; attempt <= attempts; ++attempt) {
            ec.clear();
            op(ec, attempt);
            if (!ec || !isTransient(ec) || attempt == attempts) return ec;
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            delayMs *= 2;
        }
        return ec;
    }

    void fail(const fs::path& path, const std::string& what, const std::error_code& ec) {
        std::string message = what + ": " + ec.message();
        if (isTransient(ec)) message += " (after " + std::to_string(std::max(1, m_options.attempts)) + " attempts)";
        log::get("Assets")->warn("can't read '{}': {}", path.string(), message);
        m_errors.push_back({ path.string(), message });
    }

    // The entries of one folder, sorted by path. False (and recorded) if
    // it can't be listed.
    bool list(const fs::path& dir, std::vector<fs::directory_entry>& out) {
        std::error_code ec = retry([&](std::error_code& e, int attempt) {
            out.clear();
            if (m_options.injectListingError) {
                e = m_options.injectListingError(dir.string(), attempt);
                if (e) return;
            }
            fs::directory_iterator it(dir, e);
            for (; !e && it != fs::directory_iterator(); it.increment(e)) out.push_back(*it);
        });
        if (ec) {
            fail(dir, "listing the folder", ec);
            out.clear();
            return false;
        }
        std::sort(out.begin(), out.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path() < b.path(); });
        return true;
    }

    // Every folder and regular file under `dir` (depth first, sorted),
    // until `visit(path, isDirectory)` returns false. Symlinked folders
    // are only entered with `followLinks`; a broken link is an error.
    template <class Visit>
    bool walk(const fs::path& dir, bool followLinks, Visit visit) {
        std::vector<fs::directory_entry> entries;
        if (!list(dir, entries)) return true;
        for (const fs::directory_entry& entry : entries) {
            fs::file_status st;
            std::error_code ec = retry([&](std::error_code& e, int) { st = entry.symlink_status(e); });
            if (ec) { fail(entry.path(), "reading file type", ec); continue; }
            const bool link = fs::is_symlink(st);
            if (link) {
                ec = retry([&](std::error_code& e, int) { st = fs::status(entry.path(), e); });
                if (ec) { fail(entry.path(), "following link", ec); continue; }
                if (!fs::exists(st)) { fail(entry.path(), "following link", std::make_error_code(std::errc::no_such_file_or_directory)); continue; }
            }
            if (fs::is_directory(st)) {
                if (!visit(entry.path(), true)) return false;
                if ((!link || followLinks) && !walk(entry.path(), followLinks, visit)) return false;
            } else if (fs::is_regular_file(st)) {
                if (!visit(entry.path(), false)) return false;
            }
        }
        return true;
    }

private:
    const CatalogScanOptions& m_options;
    std::vector<CatalogScanError>& m_errors;
};

bool containsModels(Walker& walker, const fs::path& dir) {
    bool found = false;
    walker.walk(dir, false, [&](const fs::path& p, bool isDir) {
        if (!isDir && isModelFile(p)) found = true;
        return !found;
    });
    return found;
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

AssetCatalog AssetCatalog::scan(const std::string& rootPath, const CatalogScanOptions& options) {
    AssetCatalog catalog;
    Walker walker(options, catalog.errors);
    fs::path root(rootPath);
    fs::file_status rootStatus;
    std::error_code ec = walker.retry([&](std::error_code& e, int) { rootStatus = fs::status(root, e); });
    if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) walker.fail(root, "reading the asset folder", ec);
    if (!fs::is_directory(rootStatus)) return catalog;

    // Decide what the packs are.
    std::vector<fs::path> packDirs;
    bool rootIsPack = false;
    std::vector<fs::directory_entry> top;
    walker.list(root, top);
    for (const fs::directory_entry& entry : top) {
        fs::file_status st;
        ec = walker.retry([&](std::error_code& e, int) { st = fs::status(entry.path(), e); });
        if (ec) { walker.fail(entry.path(), "reading file type", ec); continue; }
        if (!fs::is_directory(st)) {
            if (fs::is_regular_file(st) && isModelFile(entry.path())) rootIsPack = true;
            continue;
        }
        std::string name = entry.path().filename().string();
        if (isAssetTypeFolder(name)) rootIsPack = true;
        else if (containsModels(walker, entry.path())) packDirs.push_back(entry.path());
    }
    if (rootIsPack || packDirs.empty()) {
        packDirs.clear();
        if (containsModels(walker, root)) packDirs.push_back(root);
    }
    std::sort(packDirs.begin(), packDirs.end());

    for (const fs::path& dir : packDirs) {
        CatalogPack pack;
        pack.name = fs::weakly_canonical(dir, ec).filename().string();
        if (ec || pack.name.empty()) pack.name = dir.filename().string(); // "pack/." and the like
        pack.root = dir.string();

        std::map<std::string, fs::path> byStem; // lower-case stem -> chosen file
        std::vector<fs::path> images;
        walker.walk(dir, false, [&](const fs::path& p, bool isDir) {
            if (isDir) {
                if (lower(p.filename().string()) == "textures") pack.textureDirs.push_back(p.string());
                return true;
            }
            std::string ext = lower(p.extension().string());
            if (ext == ".png" || ext == ".tga" || ext == ".jpg") images.push_back(p);
            if (!isModelFile(p)) return true;
            std::string key = lower(p.stem().string());
            auto found = byStem.find(key);
            // Prefer FBX over OBJ (FBX carries skeletons/materials);
            // otherwise keep the first seen in sorted order for stability.
            if (found == byStem.end() || (lower(found->second.extension().string()) != ".fbx" && ext == ".fbx")) byStem[key] = p;
            return true;
        });
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
                const fs::path rel = img.lexically_relative(dir);
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
    const CatalogScanOptions options;
    std::vector<CatalogScanError> errors; // logged by the walker
    Walker walker(options, errors);
    auto consider = [&](const fs::path& p) {
        if (searched) searched->push_back(p.string());
        fs::file_status st;
        std::error_code ec = walker.retry([&](std::error_code& e, int) { st = fs::status(p, e); });
        // Not being there is the normal answer; anything else is worth saying.
        if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) walker.fail(p, "checking for the asset folder", ec);
        return fs::is_directory(st);
    };
    auto canonical = [&](const fs::path& p) {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(p, ec);
        return ec ? p.string() : c.string(); // the path as given still names the folder
    };
    std::error_code cwdError;
    fs::path cwd = fs::current_path(cwdError);
    if (cwdError) walker.fail(".", "reading the working directory", cwdError);
    for (const std::string& var : envVars) {
        if (const char* v = std::getenv(var.c_str())) {
            if (consider(v)) return canonical(v);
        }
    }
    for (fs::path base : { cwd, fs::path(executableDir) }) {
        if (base.empty()) continue;
        for (int up = 0; up <= 4; ++up) {
            fs::path candidate = base / relative;
            if (consider(candidate)) return canonical(candidate);
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
    const CatalogScanOptions options;
    std::vector<CatalogScanError> errors; // logged by the walker; this lookup has nowhere else to put them
    Walker walker(options, errors);
    for (const std::string& dir : p->textureDirs) {
        walker.walk(dir, true, [&](const fs::path& file, bool isDir) {
            if (isDir) return true;
            const std::string stem = lower(file.stem().string());
            const std::string ext = lower(file.extension().string());
            if (ext != ".png" && ext != ".tga" && ext != ".jpg") return true;
            if (stem.find("normal") != std::string::npos || stem.find("emissive") != std::string::npos ||
                stem.find("metallic") != std::string::npos || stem.find("mask") != std::string::npos)
                return true;
            auto has = [&](const std::string& w) { return stem.find(w) != std::string::npos; };
            int score = 0;
            if (material.empty()) {
                // The pack atlas isn't "named after" anything.
                if (has("texture")) return true;
                for (const std::string& w : assetWords)
                    if (stem.find("_" + w + "_") != std::string::npos ||
                        (stem.size() > w.size() && stem.compare(stem.size() - w.size() - 1, std::string::npos, "_" + w) == 0))
                        score = 2;
            } else {
                bool named = false;
                for (const std::string& w : materialWords) named = named || has(w);
                if (!named) return true;
                score = 1;
                for (const std::string& w : assetWords) if (has(w)) score += 2;
                if (has("generic")) score += 1;
            }
            const std::string path = file.string();
            if (score > bestScore || (score == bestScore && score > 0 && path.size() < best.size())) {
                bestScore = score;
                best = path;
            }
            return true;
        });
    }
    return best;
}

} // namespace kke
