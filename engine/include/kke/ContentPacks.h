#pragma once

#include "kke/PackSeal.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kke::packs {

// Content packs: DLC and mods (docs/MODDING.md).
//
// A pack is a folder with a pack.json (or pack.yml) at its root whose other files mirror
// the game's own data folder: a pack that ships scripts/weapons/axe.lua
// adds (or replaces) the game's scripts/weapons/axe.lua. That is the
// convention Nexus Mods and Vortex expect (an archive's root is the
// game's data root), what a Steam Workshop item or a mod.io download
// unpacks to, and what a modder can make by copying a folder.
//
// The pieces, all pure file-system logic (tests/test_content_packs.cpp):
//   discover()  finds packs in search roots (the game's dlc/ folder, the
//               player's mods/ folder, Workshop and mod.io download folders)
//   resolve()   decides which packs mount and in what order: ownership
//               for DLC, the player's load order (mods.json), dependencies
//               and load-after rules (Factorio's dependency syntax)
//   Mount       layers the base data and every mounted pack, last one
//               wins per file, case-insensitively, and lists conflicts
//   sessionContent() / compare()  what a multiplayer session runs, so a
//               joining player can be told what they're missing

enum class Kind { Dlc, Mod };
const char* toString(Kind k);

// Semantic-ish version: "1", "1.2", "1.2.3" (missing parts are 0; any
// "-suffix" or "+build" is ignored for comparison).
struct Version {
    int major = 0, minor = 0, patch = 0;
    static std::optional<Version> parse(const std::string& s);
    std::string str() const;
    auto operator<=>(const Version&) const = default;
};

// One entry of pack.json's "dependencies", in Factorio's syntax:
//   "base_weapons >= 1.2"   required, at least that version
//   "? hd_textures"         optional: loads first if present
//   "! old_ui"              incompatible: this pack won't mount with it
//   "~ lib_core"            required, but no load-order constraint
// Operators: <, <=, =, >=, >. Ids are letters, digits, '.', '-', '_'.
struct Dependency {
    enum class Type { Required, Optional, Incompatible, NoOrder };
    Type type = Type::Required;
    std::string id;
    std::string op;               // "" when any version will do
    Version version;
    static std::optional<Dependency> parse(const std::string& s, std::string* error = nullptr);
    bool accepts(const Version& v) const;
    std::string str() const;
};

// Whether everyone in a multiplayer session needs the pack.
enum class Multiplayer {
    Everyone, // default: gameplay content; every player needs the same pack (and version)
    Local,    // cosmetic, UI, audio: only this machine, never checked on join
};

// pack.json.
struct PackManifest {
    std::string id;               // required, the dedup key, e.g. "com.example.mygame.frost_dlc"
    std::string title;            // required
    std::string version = "0.1.0";
    Kind kind = Kind::Mod;
    std::string author;
    std::string description;
    std::string preview;          // image, relative to the pack (Workshop and mod.io want one)
    std::string game;             // the GameManifest::id this is for; empty = any game
    std::string gameVersion;      // e.g. ">= 0.3"; empty = any
    std::vector<Dependency> dependencies;
    std::vector<std::string> loadAfter, loadBefore; // order only, never required
    // DLC: the entitlement that owns it (a licence's "dlc" extra, Steam's
    // DLC app id through the optional Steamworks module...). Empty = free.
    std::string entitlement;
    // DLC: a player who owns it can share it with everyone in the session
    // they host (the "friend's pass": one copy is enough to play together).
    bool hostShare = false;
    Multiplayer multiplayer = Multiplayer::Everyone;
    // The pack's author key (kke_seal keygen), hex. With a kke.seal in the
    // pack it proves the files are the author's and unchanged, and that an
    // update comes from the same author.
    std::string publicKey;

    // Not in pack.json: filled in by discover().
    std::filesystem::path root;
    std::string source;           // the search root's name: "dlc", "mods", "workshop", "modio"...
    bool sealed = false;          // a kke.seal verified against publicKey
    std::string sealProblem;      // why a present kke.seal didn't verify
};

constexpr const char* kManifestFile = "pack.json";
constexpr const char* kManifestStem = "pack"; // pack.json, pack.yml or pack.yaml (kke/DataFile.h)

// Parses a pack.json (or the same in YAML); false with `error` (naming the file) when it's not
// valid JSON, misses id/title, or has a bad version or dependency.
bool parseManifest(const std::string& json, PackManifest& out, std::string* error = nullptr);
bool parseManifestData(const nlohmann::json& data, PackManifest& out, std::string* error = nullptr);
// pack.json, pack.yml or pack.yaml; when there's more than one and they
// differ, the newest wins and `warning` says so (kke/DataFile.h).
bool loadManifest(const std::filesystem::path& packFolder, PackManifest& out, std::string* error = nullptr, std::string* warning = nullptr);
std::string toJson(const PackManifest& m);

// Something a scan or a resolve wants the player (or modder) to know.
struct Problem {
    std::string pack;             // id, or the folder when there's no id
    std::string message;
};

struct SearchRoot {
    std::filesystem::path path;
    std::string name;             // becomes PackManifest::source
};

// Every immediate subfolder of each root with a valid pack.json. Bad
// manifests are skipped with a Problem; one broken mod never stops the
// rest. The same id in two places keeps the higher version, and on a tie
// the earlier root (so a modder's local mods/ copy beats the Workshop's).
std::vector<PackManifest> discover(const std::vector<SearchRoot>& roots, std::vector<Problem>* problems = nullptr);

// The player's choices: which packs are on, and the mod load order.
// mods.json (or mods.yml): {"format": "kke-modlist-1", "packs": [{"id": "...", "enabled": true}, ...]}
// Packs it doesn't mention are on, after the listed ones, by id: a new
// download or Workshop subscription just works, as in Vortex and Factorio.
struct ModList {
    struct Entry {
        std::string id;
        bool enabled = true;
    };
    std::vector<Entry> entries;

    const Entry* find(const std::string& id) const;
    bool enabled(const std::string& id) const;    // true when not listed
    void setEnabled(const std::string& id, bool on);
    // Moves `id` (adding it if needed) to position `index` in the order.
    void move(const std::string& id, size_t index);

    std::string toJson() const;
    static bool fromJson(const std::string& json, ModList& out, std::string* error = nullptr);
    // JSON or YAML by the file's extension; datafile::saveTarget() says
    // which of mods.json / mods.yml the player already has.
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const;
    // `file` names the list (".../mods.json"); mods.yml or mods.yaml next
    // to it work the same, the newest winning when they differ. None at
    // all is an empty list (not an error).
    static bool load(const std::filesystem::path& file, ModList& out, std::string* error = nullptr);
};

// What the player owns. Where it comes from is the game's choice: a
// Kreative DRM licence (fromLicenseExtra), the optional Steamworks
// module, a console store... The engine only asks "owned?".
struct Entitlements {
    std::set<std::string> owned;
    // Lent for this session by the host (their hostShare DLC).
    std::set<std::string> hostShared;
    bool owns(const std::string& e) const { return e.empty() || owned.count(e) > 0; }
    // A licence's extra "dlc" field: comma separated, spaces ignored.
    static Entitlements fromLicenseExtra(const std::map<std::string, std::string>& extra);
};

struct ResolveOptions {
    std::string gameId;           // skip packs made for another game
    std::string gameVersion;      // checked against PackManifest::gameVersion
    Entitlements entitlements;
    // The game developer's key: when set, a pack of kind DLC only mounts
    // if it is sealed with this key (a mod can't pass itself off as DLC).
    std::optional<seal::PublicKey> officialKey;
};

struct Skipped {
    std::string id;
    std::string reason;
};

struct MountPlan {
    std::vector<PackManifest> order;      // mount order: first is lowest priority
    std::vector<Skipped> skipped;         // disabled or unable to mount, with why
    std::vector<Problem> warnings;        // cycles, rules that couldn't be honoured
    // DLC mounted only because the host shares it.
    std::vector<std::string> borrowed;
};

// Decides what mounts, in what order:
//   1. drop disabled packs, packs for another game or game version, and
//      DLC the player doesn't own (unless the host shares it);
//   2. drop packs whose required dependencies are missing, too old or
//      dropped themselves, and packs that declare an enabled pack
//      incompatible (repeat until nothing changes);
//   3. order: all DLC before all mods (mods may override DLC); within
//      that, the player's mods.json order (then id), with each pack's
//      dependencies and loadAfter/loadBefore packs pulled in just before
//      it. In a cycle the rules of the pack the player put first are
//      kept, with a warning.
MountPlan resolve(const std::vector<PackManifest>& available, const ModList& list, const ResolveOptions& options);

// The layered view the game reads files through.
class Mount {
public:
    struct Layer {
        std::string id;           // "base" for the game's own data
        std::filesystem::path root;
    };
    struct Conflict {
        std::string path;                  // relative, as the winner spells it
        std::vector<std::string> layers;   // every layer with the file, lowest first; back() wins
    };

    Mount() = default;
    // The base data folder, then the plan's packs in order.
    Mount(const std::filesystem::path& baseRoot, const MountPlan& plan);

    // Layers added later win. Indexes the folder's files straight away
    // (symlinks aren't followed; pack.json/.yml and kke.seal at a pack's root
    // are the pack's own and aren't mounted).
    void addLayer(const std::string& id, const std::filesystem::path& root, bool isPack = true);

    // The real file for a data path ("scripts/main.lua", any case, '/' or
    // '\\'), from the highest layer that has it. Empty when none does, or
    // when the path tries to leave the data folder ("../", absolute).
    std::filesystem::path resolve(const std::string& path) const;
    bool exists(const std::string& path) const { return !resolve(path).empty(); }
    // Which layer serves the path ("" when none).
    std::string owner(const std::string& path) const;
    // Every data path under `dir` (recursive), merged over all layers,
    // sorted; with `extension` (".lua") only those.
    std::vector<std::string> list(const std::string& dir = {}, const std::string& extension = {}) const;
    // Files more than one layer has.
    std::vector<Conflict> conflicts() const;

    const std::vector<Layer>& layers() const { return m_layers; }
    size_t fileCount() const { return m_files.size(); }

    // "Scripts\\Main.LUA" -> "scripts/main.lua"; empty for paths that
    // leave the root. The key every lookup uses.
    static std::string normalize(const std::string& path);

private:
    struct Entry {
        // (layer index, the path as that layer spells it), lowest layer first.
        std::vector<std::pair<size_t, std::string>> layers;
    };
    std::vector<Layer> m_layers;
    std::map<std::string, Entry> m_files;  // normalized path -> entry
};

// What a multiplayer session runs: every mounted pack that everyone
// needs, with a digest of its files, so a joining player's copy can be
// compared with the host's.
struct SessionPack {
    std::string id;
    std::string version;
    std::string digest;           // hex seal::digest of the pack's files
    bool hostShared = false;      // DLC the host lends to guests
    bool operator==(const SessionPack&) const = default;
};
std::vector<SessionPack> sessionContent(const MountPlan& plan, std::string* error = nullptr);

struct SessionDiff {
    std::vector<SessionPack> missing;     // the host has it, the guest doesn't
    std::vector<SessionPack> different;   // both have it, another version or other files (the host's)
    std::vector<SessionPack> extra;       // the guest has it, the host doesn't: turn it off to join
    bool ok() const { return missing.empty() && different.empty() && extra.empty(); }
};
SessionDiff compare(const std::vector<SessionPack>& host, const std::vector<SessionPack>& guest);

} // namespace kke::packs
