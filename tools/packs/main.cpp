// kke_packs: make, check and order content packs (DLC and mods;
// kke/ContentPacks.h, docs/MODDING.md).
//
//   kke_packs new <folder> <id> <title> [--dlc]   a pack.json to start from
//   kke_packs check <folder>                      is this pack.json right? is its seal?
//   kke_packs order <base-data> [options]         what mounts, in what order, and who
//                                                 overrides whom (a modder's LOOT)
//     --dlc <dir> --mods <dir> --workshop <dir> --modio <dir>   search roots, in priority order
//     --list <mods.json>      the player's load order and on/off choices
//     --game <id> --game-version <v>
//     --own <a,b,...>         entitlements the player owns
//     --shared <a,b,...>      entitlements the session's host shares
//
// Exit code 0 when everything asked for is fine, 1 when something isn't.

#include "kke/ContentPacks.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <system_error>

namespace packs = kke::packs;
namespace fs = std::filesystem;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  kke_packs new <folder> <id> <title> [--dlc]\n"
                 "  kke_packs check <folder>\n"
                 "  kke_packs order <base-data> [--dlc <dir>] [--mods <dir>] [--workshop <dir>] [--modio <dir>]\n"
                 "                  [--list <mods.json>] [--game <id>] [--game-version <v>] [--own <a,b>] [--shared <a,b>]\n");
    return 2;
}

std::set<std::string> commaList(const std::string& s) {
    std::set<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.insert(item);
    return out;
}

int makeNew(int argc, char** argv) {
    if (argc < 5) return usage();
    packs::PackManifest m;
    const fs::path folder = argv[2];
    m.id = argv[3];
    m.title = argv[4];
    m.kind = argc > 5 && std::strcmp(argv[5], "--dlc") == 0 ? packs::Kind::Dlc : packs::Kind::Mod;
    std::string error;
    packs::PackManifest check;
    if (!packs::parseManifest(packs::toJson(m), check, &error)) {
        std::fprintf(stderr, "kke_packs: %s\n", error.c_str());
        return 1;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path file = folder / packs::kManifestFile;
    if (fs::exists(file, ec)) {
        std::fprintf(stderr, "kke_packs: %s already exists\n", file.string().c_str());
        return 1;
    }
    std::ofstream(file) << packs::toJson(m);
    std::printf("wrote %s: put files in %s the way they sit in the game's data folder\n", file.string().c_str(), folder.string().c_str());
    return 0;
}

int check(const char* folder) {
    packs::PackManifest m;
    std::string error;
    if (!packs::loadManifest(folder, m, &error)) {
        std::fprintf(stderr, "kke_packs: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s %s \"%s\" (%s)\n", m.id.c_str(), m.version.c_str(), m.title.c_str(), packs::toString(m.kind));
    for (const packs::Dependency& d : m.dependencies) std::printf("  depends: %s\n", d.str().c_str());
    // discover() checks the seal the same way the game will.
    std::vector<packs::Problem> problems;
    const fs::path path = fs::absolute(folder);
    const auto found = packs::discover({ { path.parent_path(), "check" } }, &problems);
    for (const packs::PackManifest& p : found) {
        if (p.id != m.id || p.root != path) continue;
        if (!p.publicKey.empty()) std::printf("  seal: %s\n", p.sealed ? "verified" : p.sealProblem.c_str());
        else std::printf("  seal: none (add public_key and run kke_seal sign to prove the files are yours)\n");
        return p.publicKey.empty() || p.sealed ? 0 : 1;
    }
    return 0;
}

int order(int argc, char** argv) {
    if (argc < 3) return usage();
    const fs::path base = argv[2];
    std::vector<packs::SearchRoot> roots;
    packs::ModList list;
    packs::ResolveOptions options;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        const std::string v = argv[++i];
        if (a == "--dlc" || a == "--mods" || a == "--workshop" || a == "--modio") roots.push_back({ v, a.substr(2) });
        else if (a == "--list") {
            std::string error;
            if (!packs::ModList::load(v, list, &error)) {
                std::fprintf(stderr, "kke_packs: %s\n", error.c_str());
                return 1;
            }
        } else if (a == "--game") options.gameId = v;
        else if (a == "--game-version") options.gameVersion = v;
        else if (a == "--own") options.entitlements.owned = commaList(v);
        else if (a == "--shared") options.entitlements.hostShared = commaList(v);
        else return usage();
    }
    std::vector<packs::Problem> problems;
    const auto found = packs::discover(roots, &problems);
    const packs::MountPlan plan = packs::resolve(found, list, options);
    const packs::Mount mount(base, plan);

    std::printf("mount order (later wins):\n  0. base  %s\n", base.string().c_str());
    for (size_t i = 0; i < plan.order.size(); ++i) {
        const packs::PackManifest& p = plan.order[i];
        std::printf("  %zu. %s %s  [%s, %s]%s\n", i + 1, p.id.c_str(), p.version.c_str(), packs::toString(p.kind), p.source.c_str(),
                    p.sealed ? " sealed" : "");
    }
    for (const std::string& b : plan.borrowed) std::printf("borrowed from the host: %s\n", b.c_str());
    for (const packs::Skipped& s : plan.skipped) std::printf("not mounted: %s: %s\n", s.id.c_str(), s.reason.c_str());
    for (const packs::Problem& p : problems) std::printf("problem: %s: %s\n", p.pack.c_str(), p.message.c_str());
    for (const packs::Problem& w : plan.warnings) std::printf("warning: %s: %s\n", w.pack.c_str(), w.message.c_str());
    const auto conflicts = mount.conflicts();
    std::printf("%zu files, %zu overridden\n", mount.fileCount(), conflicts.size());
    for (const packs::Mount::Conflict& c : conflicts) {
        std::string chain;
        for (const std::string& l : c.layers) chain += (chain.empty() ? "" : " < ") + l;
        std::printf("  %s: %s\n", c.path.c_str(), chain.c_str());
    }
    return problems.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "new") return makeNew(argc, argv);
    if (cmd == "check" && argc == 3) return check(argv[2]);
    if (cmd == "order") return order(argc, argv);
    return usage();
}
