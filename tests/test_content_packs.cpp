// Content packs: DLC and mods (docs/MODDING.md): pack.json, discovery,
// load order, the layered mount, and what a multiplayer session compares.

#include "kke/ContentPacks.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace packs = kke::packs;
namespace seal = kke::seal;
namespace fs = std::filesystem;
using packs::Dependency;
using packs::Kind;
using packs::Version;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("kke_packs_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string read(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

packs::PackManifest pack(const std::string& id, Kind kind = Kind::Mod, std::vector<std::string> deps = {}) {
    packs::PackManifest m;
    m.id = id;
    m.title = id;
    m.kind = kind;
    for (const std::string& d : deps) m.dependencies.push_back(*Dependency::parse(d));
    return m;
}

std::vector<std::string> ids(const packs::MountPlan& plan) {
    std::vector<std::string> out;
    for (const auto& p : plan.order) out.push_back(p.id);
    return out;
}

std::string reason(const packs::MountPlan& plan, const std::string& id) {
    for (const auto& s : plan.skipped)
        if (s.id == id) return s.reason;
    return {};
}

} // namespace

// ------------------------------------------------------------ versions and dependencies

TEST(ContentPacks, Versions) {
    EXPECT_EQ(Version::parse("1.2.3")->str(), "1.2.3");
    EXPECT_EQ(Version::parse("2")->str(), "2.0.0");
    EXPECT_EQ(Version::parse(" 1.4-beta+7 ")->str(), "1.4.0");
    EXPECT_LT(*Version::parse("1.9"), *Version::parse("1.10"));
    EXPECT_FALSE(Version::parse(""));
    EXPECT_FALSE(Version::parse("1..2"));
    EXPECT_FALSE(Version::parse("1.2.3.4"));
    EXPECT_FALSE(Version::parse("v1"));
    EXPECT_FALSE(Version::parse("99999999999"));
}

TEST(ContentPacks, FactorioDependencySyntax) {
    auto d = Dependency::parse("base_weapons >= 1.2");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->type, Dependency::Type::Required);
    EXPECT_EQ(d->id, "base_weapons");
    EXPECT_TRUE(d->accepts(*Version::parse("1.2")));
    EXPECT_TRUE(d->accepts(*Version::parse("3")));
    EXPECT_FALSE(d->accepts(*Version::parse("1.1.9")));
    EXPECT_EQ(d->str(), "base_weapons >= 1.2.0");

    EXPECT_EQ(Dependency::parse("? hd_textures")->type, Dependency::Type::Optional);
    EXPECT_EQ(Dependency::parse("!old_ui")->type, Dependency::Type::Incompatible);
    EXPECT_EQ(Dependency::parse("~ lib_core")->type, Dependency::Type::NoOrder);
    EXPECT_TRUE(Dependency::parse("x<2")->accepts(*Version::parse("1.9")));
    EXPECT_TRUE(Dependency::parse("lib")->accepts(*Version::parse("0.0.1")));

    std::string error;
    EXPECT_FALSE(Dependency::parse("", &error));
    EXPECT_FALSE(Dependency::parse("bad id", &error)) << "a bare version needs an operator";
    EXPECT_FALSE(Dependency::parse("x => 1", &error));
    EXPECT_FALSE(Dependency::parse("../escape", &error));
    EXPECT_FALSE(error.empty());
}

// ------------------------------------------------------------ pack.json

TEST(ContentPacks, ManifestRoundTrip) {
    const char* text = R"({
        "id": "com.example.mygame.frost", "title": "Frost", "version": "1.1.0", "kind": "dlc",
        "author": "Example", "game": "com.example.mygame", "game_version": ">= 0.3",
        "dependencies": ["base_weapons >= 1.0", "? hd_textures"], "load_after": ["ui_tweaks"],
        "entitlement": "frost", "host_share": true, "multiplayer": "everyone" })";
    packs::PackManifest m;
    std::string error;
    ASSERT_TRUE(packs::parseManifest(text, m, &error)) << error;
    EXPECT_EQ(m.kind, Kind::Dlc);
    EXPECT_TRUE(m.hostShare);
    ASSERT_EQ(m.dependencies.size(), 2u);
    EXPECT_EQ(m.dependencies[1].type, Dependency::Type::Optional);
    EXPECT_EQ(m.loadAfter, std::vector<std::string>{ "ui_tweaks" });

    packs::PackManifest back;
    ASSERT_TRUE(packs::parseManifest(packs::toJson(m), back, &error)) << error;
    EXPECT_EQ(packs::toJson(back), packs::toJson(m));
}

TEST(ContentPacks, ManifestRejectsMistakesWithAReason) {
    auto bad = [](const std::string& text) {
        packs::PackManifest m;
        std::string error;
        EXPECT_FALSE(packs::parseManifest(text, m, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        return error;
    };
    bad("not json");
    bad(R"({"title": "No id"})");
    bad(R"({"id": "has space", "title": "x"})");
    bad(R"({"id": "x"})");
    bad(R"({"id": "x", "title": "x", "version": "one"})");
    bad(R"({"id": "x", "title": "x", "kind": "expansion"})");
    bad(R"({"id": "x", "title": "x", "multiplayer": "some"})");
    bad(R"({"id": "x", "title": "x", "dependencies": "y"})");
    bad(R"({"id": "x", "title": "x", "dependencies": ["x"]})");
    bad(R"({"id": "x", "title": "x", "game_version": "soon"})");
    bad(R"({"id": "x", "title": "x", "public_key": "abc"})");
    bad(R"({"id": "x", "title": ["x"]})");
    EXPECT_NE(bad(R"({"id": "x", "title": "x", "entitlement": "e"})").find("DLC"), std::string::npos)
        << "a mod can't claim an entitlement";
}

// ------------------------------------------------------------ discovery

TEST(ContentPacks, DiscoverSkipsBrokenPacksAndKeepsTheNewestCopy) {
    TempDir t;
    const fs::path mods = t.path / "mods", workshop = t.path / "workshop";
    write(mods / "axe" / "pack.json", R"({"id": "axe", "title": "Axe", "version": "1.0"})");
    write(workshop / "3112233" / "pack.json", R"({"id": "axe", "title": "Axe", "version": "1.2"})");
    write(workshop / "3112234" / "pack.json", R"({"id": "hats", "title": "Hats"})");
    write(mods / "broken" / "pack.json", "{");
    write(mods / "not_a_pack" / "readme.txt", "hi");

    std::vector<packs::Problem> problems;
    auto found = packs::discover({ { mods, "mods" }, { workshop, "workshop" }, { t.path / "missing", "modio" } }, &problems);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].id, "axe");
    EXPECT_EQ(found[0].version, "1.2");
    EXPECT_EQ(found[0].source, "workshop");
    EXPECT_EQ(found[0].root, workshop / "3112233");
    EXPECT_EQ(found[1].id, "hats");
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_EQ(problems[0].pack, "broken");
    EXPECT_NE(problems[1].message.find("installed twice"), std::string::npos);

    // Same version: the earlier root (the modder's own folder) wins.
    write(workshop / "3112233" / "pack.json", R"({"id": "axe", "title": "Axe", "version": "1.0"})");
    found = packs::discover({ { mods, "mods" }, { workshop, "workshop" } });
    EXPECT_EQ(found[0].source, "mods");
}

TEST(ContentPacks, SealedPacksAreVerified) {
    TempDir t;
    seal::KeyPair keys = seal::keyPairFromSeed({ 7 });
    const fs::path p = t.path / "mods" / "sealed";
    write(p / "pack.json", R"({"id": "sealed", "title": "S", "public_key": ")" + seal::toHex(keys.publicKey) + "\"}");
    write(p / "scripts" / "a.lua", "print('a')");
    seal::Manifest m;
    ASSERT_TRUE(seal::build(p, m));
    seal::sign(m, keys.secret);
    ASSERT_TRUE(seal::save(m, p / seal::kSealFileName));

    auto found = packs::discover({ { t.path / "mods", "mods" } });
    ASSERT_EQ(found.size(), 1u);
    EXPECT_TRUE(found[0].sealed) << found[0].sealProblem;

    write(p / "scripts" / "a.lua", "damage = 9999");
    std::vector<packs::Problem> problems;
    found = packs::discover({ { t.path / "mods", "mods" } }, &problems);
    EXPECT_FALSE(found[0].sealed);
    EXPECT_NE(found[0].sealProblem.find("scripts/a.lua"), std::string::npos) << found[0].sealProblem;
    ASSERT_EQ(problems.size(), 1u);
}

// ------------------------------------------------------------ mods.json

TEST(ContentPacks, ModListOrderAndPersistence) {
    TempDir t;
    packs::ModList list;
    EXPECT_TRUE(list.enabled("anything")) << "packs the list doesn't know are on";
    list.setEnabled("b", false);
    list.move("a", 0);
    list.move("c", 99);
    list.move("c", 1);
    ASSERT_EQ(list.entries.size(), 3u);
    EXPECT_EQ(list.entries[0].id, "a");
    EXPECT_EQ(list.entries[1].id, "c");
    EXPECT_EQ(list.entries[2].id, "b");
    EXPECT_FALSE(list.enabled("b"));

    const fs::path file = t.path / "mods.json";
    packs::ModList loaded;
    ASSERT_TRUE(packs::ModList::load(file, loaded)) << "a missing file is an empty list";
    EXPECT_TRUE(loaded.entries.empty());
    ASSERT_TRUE(list.save(file));
    ASSERT_TRUE(packs::ModList::load(file, loaded));
    EXPECT_EQ(loaded.toJson(), list.toJson());

    write(file, R"({"format": "kke-modlist-1", "packs": [{"id": "a"}, {"id": "a"}, {"id": "bad id"}, {"id": "z", "enabled": false}]})");
    ASSERT_TRUE(packs::ModList::load(file, loaded));
    ASSERT_EQ(loaded.entries.size(), 2u) << "repeats and junk from hand edits are dropped";
    EXPECT_FALSE(loaded.enabled("z"));
    std::string error;
    write(file, R"({"packs": []})");
    EXPECT_FALSE(packs::ModList::load(file, loaded, &error));
    EXPECT_FALSE(error.empty());
}

// ------------------------------------------------------------ resolve

TEST(ContentPacks, DlcFirstThenDependenciesThenThePlayersOrder) {
    std::vector<packs::PackManifest> all = {
        pack("zz_ui"),
        pack("weapons_plus", Kind::Mod, { "lib" }),
        pack("lib"),
        pack("frost", Kind::Dlc),
        pack("hats"),
    };
    packs::ModList list;
    list.move("weapons_plus", 0);
    list.move("hats", 1);
    list.move("lib", 2);
    const packs::MountPlan plan = packs::resolve(all, list, {});
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "frost", "lib", "weapons_plus", "hats", "zz_ui" }));
    EXPECT_TRUE(plan.skipped.empty());
    EXPECT_TRUE(plan.warnings.empty());
}

TEST(ContentPacks, LoadAfterAndLoadBeforeOnlyOrder) {
    auto a = pack("a"), b = pack("b"), c = pack("c");
    a.loadAfter = { "c", "not_installed" };
    b.loadBefore = { "c" };
    const packs::MountPlan plan = packs::resolve({ a, b, c }, {}, {});
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "b", "c", "a" }));
    EXPECT_TRUE(plan.skipped.empty()) << "load_after never requires anything";
}

TEST(ContentPacks, MissingDependenciesCascadeWithReasons) {
    std::vector<packs::PackManifest> all = {
        pack("needs_lib2", Kind::Mod, { "lib >= 2" }),
        pack("lib"),
        pack("needs_ghost", Kind::Mod, { "ghost" }),
        pack("needs_needs_ghost", Kind::Mod, { "needs_ghost" }),
        pack("needs_off", Kind::Mod, { "off" }),
        pack("off"),
        pack("optional", Kind::Mod, { "? ghost", "~ lib" }),
    };
    all[1].version = "1.5";
    packs::ModList list;
    list.setEnabled("off", false);
    const packs::MountPlan plan = packs::resolve(all, list, {});
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "lib", "optional" }));
    EXPECT_EQ(reason(plan, "needs_lib2"), "needs lib >= 2.0.0 (installed: 1.5)");
    EXPECT_EQ(reason(plan, "needs_ghost"), "needs ghost, which isn't installed");
    EXPECT_NE(reason(plan, "needs_needs_ghost").find("needs needs_ghost, which isn't mounted"), std::string::npos);
    EXPECT_EQ(reason(plan, "needs_off"), "needs off, which isn't mounted (turned off)");
}

TEST(ContentPacks, IncompatiblePacksDontMountTogether) {
    const packs::MountPlan plan = packs::resolve({ pack("new_ui", Kind::Mod, { "! old_ui < 2" }), pack("old_ui") }, {}, {});
    EXPECT_EQ(ids(plan), std::vector<std::string>{ "old_ui" });
    EXPECT_EQ(reason(plan, "new_ui"), "incompatible with old_ui 0.1.0");

    auto fixed = pack("old_ui");
    fixed.version = "2.0";
    EXPECT_EQ(packs::resolve({ pack("new_ui", Kind::Mod, { "! old_ui < 2" }), fixed }, {}, {}).order.size(), 2u);
}

TEST(ContentPacks, CyclesAreBrokenWhereThePlayerSaid) {
    auto a = pack("a"), b = pack("b");
    a.loadAfter = { "b" };
    b.loadAfter = { "a" };
    packs::ModList list;
    list.move("b", 0);
    const packs::MountPlan plan = packs::resolve({ a, b }, list, {});
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "a", "b" })) << "b came first in the list, so its rule (after a) is kept";
    ASSERT_EQ(plan.warnings.size(), 1u);
    EXPECT_NE(plan.warnings[0].message.find("cycle"), std::string::npos);
}

TEST(ContentPacks, ModsCantLoadBeforeDlc) {
    auto m = pack("early_mod");
    m.loadBefore = { "frost" };
    const packs::MountPlan plan = packs::resolve({ m, pack("frost", Kind::Dlc) }, {}, {});
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "frost", "early_mod" }));
    ASSERT_EQ(plan.warnings.size(), 1u);
}

TEST(ContentPacks, GameAndGameVersion) {
    auto other = pack("other_game_mod"), old = pack("old_mod"), any = pack("any");
    other.game = "com.example.other";
    old.gameVersion = "< 0.3";
    packs::ResolveOptions o;
    o.gameId = "com.example.mygame";
    o.gameVersion = "0.3.1";
    const packs::MountPlan plan = packs::resolve({ other, old, any }, {}, o);
    EXPECT_EQ(ids(plan), std::vector<std::string>{ "any" });
    EXPECT_EQ(reason(plan, "other_game_mod"), "made for another game (com.example.other)");
    EXPECT_EQ(reason(plan, "old_mod"), "needs game version < 0.3.0 (this is 0.3.1)");
}

TEST(ContentPacks, DlcNeedsOwnershipOrAHostWhoSharesIt) {
    auto frost = pack("frost", Kind::Dlc), maps = pack("maps", Kind::Dlc), free = pack("free_dlc", Kind::Dlc);
    frost.entitlement = "frost";
    frost.hostShare = true;
    maps.entitlement = "maps";
    auto needsFrost = pack("frost_tweaks", Kind::Mod, { "frost" });

    packs::ResolveOptions o;
    packs::MountPlan plan = packs::resolve({ frost, maps, free, needsFrost }, {}, o);
    EXPECT_EQ(ids(plan), std::vector<std::string>{ "free_dlc" });
    EXPECT_EQ(reason(plan, "frost"), "not owned (frost)");

    // A licence's "dlc" extra says what the player bought.
    o.entitlements = packs::Entitlements::fromLicenseExtra({ { "dlc", " maps , frost" }, { "edition", "deluxe" } });
    EXPECT_EQ(o.entitlements.owned.size(), 2u);
    plan = packs::resolve({ frost, maps, free, needsFrost }, {}, o);
    EXPECT_EQ(plan.order.size(), 4u);
    EXPECT_TRUE(plan.borrowed.empty());

    // Friend's pass: a guest without it plays the host's shared DLC, but
    // not one the host doesn't share.
    o.entitlements = {};
    o.entitlements.hostShared = { "frost", "maps" };
    plan = packs::resolve({ frost, maps, free, needsFrost }, {}, o);
    EXPECT_EQ(ids(plan), (std::vector<std::string>{ "free_dlc", "frost", "frost_tweaks" }));
    EXPECT_EQ(plan.borrowed, std::vector<std::string>{ "frost" });
    EXPECT_EQ(reason(plan, "maps"), "not owned (maps)");
}

TEST(ContentPacks, OfficialKeyStopsModsPosingAsDlc) {
    TempDir t;
    const seal::KeyPair dev = seal::keyPairFromSeed({ 1 }), other = seal::keyPairFromSeed({ 2 });
    auto makeDlc = [&](const std::string& id, const seal::KeyPair& keys) {
        const fs::path root = t.path / id;
        write(root / "pack.json", "{\"id\": \"" + id + "\", \"title\": \"x\", \"kind\": \"dlc\"}");
        write(root / "maps" / "m.json", "{}");
        seal::Manifest m;
        seal::build(root, m);
        seal::sign(m, keys.secret);
        seal::save(m, root / seal::kSealFileName);
        packs::PackManifest p;
        packs::loadManifest(root, p);
        return p;
    };
    packs::ResolveOptions o;
    o.officialKey = dev.publicKey;
    const packs::MountPlan plan = packs::resolve({ makeDlc("real", dev), makeDlc("fake", other) }, {}, o);
    EXPECT_EQ(ids(plan), std::vector<std::string>{ "real" });
    EXPECT_NE(reason(plan, "fake").find("not signed by the game's developer"), std::string::npos);
}

// ------------------------------------------------------------ mount

TEST(ContentPacks, NormalizeKeepsPathsInside) {
    using M = packs::Mount;
    EXPECT_EQ(M::normalize("Scripts\\Main.LUA"), "scripts/main.lua");
    EXPECT_EQ(M::normalize("./a//b/./c"), "a/b/c");
    EXPECT_EQ(M::normalize("../secrets"), "");
    EXPECT_EQ(M::normalize("a/../../b"), "");
    EXPECT_EQ(M::normalize("/etc/passwd"), "");
    EXPECT_EQ(M::normalize("C:/Windows"), "");
    EXPECT_EQ(M::normalize("file.txt:stream"), "");
}

TEST(ContentPacks, LastLayerWinsCaseInsensitivelyWithConflicts) {
    TempDir t;
    const fs::path base = t.path / "data", frost = t.path / "dlc" / "frost", axe = t.path / "mods" / "axe";
    write(base / "scripts" / "main.lua", "base main");
    write(base / "scripts" / "weapons" / "axe.lua", "base axe");
    write(base / "maps" / "town.json", "town");
    write(frost / "pack.json", R"({"id": "frost", "title": "Frost", "kind": "dlc"})");
    write(frost / "maps" / "glacier.json", "glacier");
    write(frost / "scripts" / "weapons" / "axe.lua", "frost axe");
    write(axe / "pack.json", R"({"id": "axe", "title": "Axe"})");
    write(axe / "Scripts" / "Weapons" / "Axe.lua", "mod axe"); // made on Windows
    write(axe / "kke.seal", "{}");

    const auto found = packs::discover({ { t.path / "dlc", "dlc" }, { t.path / "mods", "mods" } });
    const packs::MountPlan plan = packs::resolve(found, {}, {});
    const packs::Mount mount(base, plan);
    ASSERT_EQ(mount.layers().size(), 3u);

    EXPECT_EQ(read(mount.resolve("scripts/weapons/axe.lua")), "mod axe");
    EXPECT_EQ(mount.owner("SCRIPTS\\weapons\\AXE.lua"), "axe");
    EXPECT_EQ(read(mount.resolve("scripts/main.lua")), "base main");
    EXPECT_EQ(mount.owner("maps/glacier.json"), "frost");
    EXPECT_TRUE(mount.resolve("pack.json").empty()) << "a pack's own metadata isn't data";
    EXPECT_TRUE(mount.resolve("kke.seal").empty());
    EXPECT_TRUE(mount.resolve("../data/scripts/main.lua").empty());
    EXPECT_FALSE(mount.exists("scripts/missing.lua"));

    EXPECT_EQ(mount.list("maps"), (std::vector<std::string>{ "maps/glacier.json", "maps/town.json" }));
    EXPECT_EQ(mount.list("", ".LUA"), (std::vector<std::string>{ "scripts/main.lua", "Scripts/Weapons/Axe.lua" }));
    EXPECT_TRUE(mount.list("..").empty());
    EXPECT_EQ(mount.fileCount(), 4u);

    const auto conflicts = mount.conflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].path, "Scripts/Weapons/Axe.lua");
    EXPECT_EQ(conflicts[0].layers, (std::vector<std::string>{ "base", "frost", "axe" }));

    // The player moves the DLC's version back on top? DLC always loads
    // before mods, so the mod still wins; turning the mod off restores it.
    packs::ModList list;
    list.setEnabled("axe", false);
    const packs::Mount without(base, packs::resolve(found, list, {}));
    EXPECT_EQ(read(without.resolve("scripts/weapons/axe.lua")), "frost axe");
}

TEST(ContentPacks, SymlinksArentFollowed) {
    TempDir t;
    write(t.path / "outside" / "secret.txt", "secret");
    write(t.path / "mod" / "ok.txt", "ok");
    std::error_code ec;
    fs::create_directory_symlink(t.path / "outside", t.path / "mod" / "link", ec);
    if (ec) GTEST_SKIP() << "can't make symlinks here: " << ec.message();
    packs::Mount mount;
    mount.addLayer("mod", t.path / "mod");
    EXPECT_TRUE(mount.exists("ok.txt"));
    EXPECT_FALSE(mount.exists("link/secret.txt"));
}

// ------------------------------------------------------------ sessions

TEST(ContentPacks, SessionContentComparesWhatEveryoneNeeds) {
    TempDir t;
    write(t.path / "mods" / "axe" / "pack.json", R"({"id": "axe", "title": "Axe"})");
    write(t.path / "mods" / "axe" / "scripts" / "axe.lua", "damage = 10");
    write(t.path / "mods" / "hud" / "pack.json", R"({"id": "hud", "title": "HUD", "multiplayer": "local"})");
    write(t.path / "dlc" / "frost" / "pack.json", R"({"id": "frost", "title": "F", "kind": "dlc", "entitlement": "frost", "host_share": true})");

    packs::ResolveOptions hostOptions;
    hostOptions.entitlements.owned = { "frost" };
    const auto found = packs::discover({ { t.path / "dlc", "dlc" }, { t.path / "mods", "mods" } });
    std::string error;
    const auto host = packs::sessionContent(packs::resolve(found, {}, hostOptions), &error);
    ASSERT_EQ(host.size(), 2u) << error << " (the local HUD isn't part of the session)";
    EXPECT_EQ(host[0].id, "frost");
    EXPECT_TRUE(host[0].hostShared);
    EXPECT_EQ(host[1].id, "axe");
    EXPECT_EQ(host[1].digest.size(), 64u);

    EXPECT_TRUE(packs::compare(host, host).ok());

    // The guest's axe.lua differs, they have a mod the host doesn't, and
    // no frost: exactly what the join screen needs to say.
    auto guest = host;
    guest.erase(guest.begin());
    guest[0].digest[0] = guest[0].digest[0] == 'a' ? 'b' : 'a';
    guest.push_back({ "cheats", "1.0.0", std::string(64, '0'), false });
    const packs::SessionDiff d = packs::compare(host, guest);
    EXPECT_FALSE(d.ok());
    ASSERT_EQ(d.missing.size(), 1u);
    EXPECT_EQ(d.missing[0].id, "frost");
    ASSERT_EQ(d.different.size(), 1u);
    EXPECT_EQ(d.different[0].digest, host[1].digest) << "what the guest needs is the host's copy";
    ASSERT_EQ(d.extra.size(), 1u);
    EXPECT_EQ(d.extra[0].id, "cheats");
}
