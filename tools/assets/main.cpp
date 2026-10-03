// kke_assets: what is in your asset folders, which pack each folder is,
// and what every game needs (kke/KnownPacks.h, docs/ASSETS.md).
//
//   kke_assets                    the asset folder (KKE_ASSETS_DIR or assets/synty)
//                                 and the sprite folder (KKE_SPRITES_DIR or assets/sprites)
//   kke_assets FOLDER [FOLDER2]   these folders instead
//   kke_assets needs [GAME]       the packs each game uses, nothing read from disk
//
// Folder names don't matter: a pack is recognised by its name in any
// spelling, or by files only it has. Exit code 0; 2 for bad arguments.

#include "kke/AssetCatalog.h"
#include "kke/KnownPacks.h"
#include "kke/SpriteCatalog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

int usage() {
    std::fprintf(stderr, "usage:\n  kke_assets [FOLDER...]\n  kke_assets needs [GAME]\n");
    return 2;
}

std::string join(const std::vector<std::string>& items, const char* sep) {
    std::string out;
    for (const std::string& s : items) out += (out.empty() ? "" : sep) + s;
    return out;
}

// Every game named in the table, sorted.
std::vector<std::string> games() {
    std::set<std::string> all;
    for (const kke::KnownPack& p : kke::knownPacks()) all.insert(p.usedBy.begin(), p.usedBy.end());
    return { all.begin(), all.end() };
}

std::vector<const kke::KnownPack*> packsFor(const std::string& game) {
    std::vector<const kke::KnownPack*> out;
    for (const kke::KnownPack& p : kke::knownPacks())
        if (std::find(p.usedBy.begin(), p.usedBy.end(), game) != p.usedBy.end()) out.push_back(&p);
    return out;
}

int needs(const std::string& only) {
    bool any = false;
    for (const std::string& game : games()) {
        if (!only.empty() && game != only) continue;
        any = true;
        std::printf("%s\n", game.c_str());
        for (const kke::KnownPack* p : packsFor(game)) std::printf("  %-38s %-12s %s\n", p->name.c_str(), p->folder.c_str(), p->usedFor.c_str());
    }
    if (!any) {
        std::printf("no game called '%s' uses an art pack; the games that do: %s\n", only.c_str(), join(games(), ", ").c_str());
        return 2;
    }
    std::printf("\nEvery game runs without its packs too: blocks and plain colours stand in.\n");
    return 0;
}

struct Found {
    std::string folder; // relative to the scanned root
    std::string pack;
    kke::PackIdentity::How how = kke::PackIdentity::How::Unknown;
    kke::PackContents contents;
};

// The pack folders under `root`, as the engine sees them: each folder,
// or the packs inside a folder that only holds packs ("Synty/POLYGON_Town").
std::vector<Found> look(const fs::path& root) {
    std::vector<fs::path> top;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec) && it->path().filename().string().rfind('.', 0) != 0) top.push_back(it->path());
    std::sort(top.begin(), top.end());
    std::vector<Found> out;
    for (const fs::path& dir : top) {
        std::vector<fs::path> dirs{ dir };
        if (!kke::knownPack(dir.filename().string())) {
            std::vector<fs::path> nested;
            for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
                if (it->is_directory(ec) && kke::knownPack(it->path().filename().string())) nested.push_back(it->path());
            std::sort(nested.begin(), nested.end());
            if (!nested.empty()) dirs = nested;
        }
        for (const fs::path& d : dirs) {
            const kke::PackIdentity id = kke::identifyPackFolder(d.string());
            out.push_back({ d.lexically_relative(root).string(), id.name, id.how, kke::analyzePackFolder(d.string()) });
        }
    }
    return out;
}

void report(const fs::path& root, const char* role, std::map<std::string, std::string>& have) {
    std::printf("%s: %s\n", role, root.string().c_str());
    const std::vector<Found> found = look(root);
    if (found.empty()) std::printf("  (no pack folders here)\n");
    // Which folder this is: the sprite folder, the asset folder, or one
    // named on the command line (then each pack just says where it goes).
    const std::string here = std::strcmp(role, "Sprite folder") == 0 ? "assets/sprites" : std::strcmp(role, "Asset folder") == 0 ? "assets/synty" : "";
    for (const Found& f : found) {
        const kke::KnownPack* known = f.how == kke::PackIdentity::How::Unknown ? nullptr : kke::knownPack(f.pack);
        std::string what;
        if (known) {
            what = known->name + (f.how == kke::PackIdentity::How::Contents ? " (by its files)" : "");
            what += known->usedBy.empty() ? "; no game uses it yet" : "; used by " + join(known->usedBy, ", ");
        } else {
            what = "not a pack the games use (" + f.contents.kind() + ", " + std::to_string(f.contents.files) + " files)";
        }
        const std::string belongs = known ? known->folder : f.contents.folder();
        if (belongs.empty() && !known) what += "; the engine doesn't load this kind of pack";
        else if (here.empty() && belongs == "assets/sprites") what += "; a sprite pack (assets/sprites)";
        else if (!here.empty() && belongs != here) what += "; belongs in " + belongs;
        std::printf("  %-44s %s\n", f.folder.c_str(), what.c_str());
        if (known && !have.count(known->name)) have[known->name] = (root / f.folder).string();
        else if (known) std::printf("  %-44s (a second copy: %s is used)\n", "", have[known->name].c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) return usage();
    if (argc >= 2 && std::strcmp(argv[1], "needs") == 0) return argc > 3 ? usage() : needs(argc == 3 ? argv[2] : "");

    std::map<std::string, std::string> have; // pack -> folder it was found in
    if (argc >= 2) {
        for (int i = 1; i < argc; ++i) {
            std::error_code ec;
            if (!fs::is_directory(argv[i], ec)) {
                std::fprintf(stderr, "kke_assets: '%s' is not a folder\n", argv[i]);
                return 2;
            }
            report(argv[i], "Folder", have);
        }
    } else {
        const std::string exeDir = fs::absolute(fs::path(argv[0])).parent_path().string();
        std::vector<std::string> searched;
        const std::string models = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, exeDir, &searched);
        if (models.empty()) std::printf("Asset folder: none found (set KKE_ASSETS_DIR, or make assets/synty). Looked in:\n  %s\n", join(searched, "\n  ").c_str());
        else report(models, "Asset folder", have);
        const std::string sprites = kke::findSpriteFolder(exeDir);
        if (sprites.empty()) std::printf("Sprite folder: none (set KKE_SPRITES_DIR, or make assets/sprites)\n");
        else report(sprites, "Sprite folder", have);
    }

    std::printf("\nWhat the games need:\n");
    for (const std::string& game : games()) {
        std::vector<std::string> parts;
        for (const kke::KnownPack* p : packsFor(game)) parts.push_back((have.count(p->name) ? "ok " : "MISSING ") + p->name);
        std::printf("  %-14s %s\n", game.c_str(), join(parts, ", ").c_str());
    }
    std::set<std::string> missingPacks;
    for (const std::string& game : games())
        for (const kke::KnownPack* p : packsFor(game))
            if (!have.count(p->name)) missingPacks.insert(p->name);
    if (missingPacks.empty()) std::printf("\nEverything the games use is here.\n");
    else
        std::printf("\n%zu pack(s) missing: %s.\nThose games still run, with blocks standing in. A bake (tools/packaging/bake_with_art.sh)\n"
                    "includes only what is here.\n",
                    missingPacks.size(), join({ missingPacks.begin(), missingPacks.end() }, ", ").c_str());
    return 0;
}
