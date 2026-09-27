#include "kke/ContentPacks.h"
#include "kke/DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <tuple>
#include <system_error>

namespace kke::packs {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    });
}

bool validOp(const std::string& op) {
    return op.empty() || op == "<" || op == "<=" || op == "=" || op == ">=" || op == ">";
}

bool compareOp(const Version& v, const std::string& op, const Version& want) {
    if (op.empty()) return true;
    if (op == "<") return v < want;
    if (op == "<=") return v <= want;
    if (op == "=") return v == want;
    if (op == ">=") return v >= want;
    if (op == ">") return v > want;
    return false;
}

// "op version" (op optional: a bare version means "at least").
bool parseConstraint(const std::string& text, std::string& op, Version& version) {
    std::string s = trim(text);
    size_t i = 0;
    while (i < s.size() && (s[i] == '<' || s[i] == '>' || s[i] == '=')) ++i;
    op = s.substr(0, i);
    if (op.empty()) op = ">=";
    if (!validOp(op)) return false;
    const std::optional<Version> v = Version::parse(trim(s.substr(i)));
    if (!v) return false;
    version = *v;
    return true;
}

std::vector<std::string> stringArray(const json& j, const char* key, std::string& error) {
    std::vector<std::string> out;
    if (!j.contains(key)) return out;
    if (!j[key].is_array()) {
        error = std::string("\"") + key + "\" must be an array of strings";
        return out;
    }
    for (const json& e : j[key]) {
        if (!e.is_string()) {
            error = std::string("\"") + key + "\" must be an array of strings";
            return {};
        }
        out.push_back(e.get<std::string>());
    }
    return out;
}

} // namespace

const char* toString(Kind k) {
    return k == Kind::Dlc ? "dlc" : "mod";
}

// ------------------------------------------------------------ versions

std::optional<Version> Version::parse(const std::string& text) {
    std::string s = trim(text);
    const size_t cut = s.find_first_of("-+");
    if (cut != std::string::npos) s.resize(cut);
    if (s.empty()) return std::nullopt;
    Version v;
    int* parts[3] = { &v.major, &v.minor, &v.patch };
    size_t part = 0, i = 0;
    while (true) {
        if (part >= 3) return std::nullopt;
        size_t start = i;
        long long n = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            n = n * 10 + (s[i] - '0');
            if (n > 1000000000) return std::nullopt;
            ++i;
        }
        if (i == start) return std::nullopt;
        *parts[part++] = static_cast<int>(n);
        if (i == s.size()) break;
        if (s[i] != '.') return std::nullopt;
        ++i;
    }
    return v;
}

std::string Version::str() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<Dependency> Dependency::parse(const std::string& text, std::string* error) {
    std::string s = trim(text);
    Dependency d;
    if (!s.empty() && (s[0] == '?' || s[0] == '!' || s[0] == '~')) {
        d.type = s[0] == '?' ? Type::Optional : s[0] == '!' ? Type::Incompatible : Type::NoOrder;
        s = trim(s.substr(1));
    }
    size_t i = 0;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '<' && s[i] != '>' && s[i] != '=') ++i;
    d.id = s.substr(0, i);
    if (!validId(d.id)) {
        fail(error, "dependency \"" + text + "\": expected a pack id (letters, digits, '.', '-', '_')");
        return std::nullopt;
    }
    const std::string rest = trim(s.substr(i));
    if (!rest.empty()) {
        if (!parseConstraint(rest, d.op, d.version) || rest.find_first_of("<>=") != 0) {
            fail(error, "dependency \"" + text + "\": expected \"id\", \"id >= 1.2\" (operators < <= = >= >)");
            return std::nullopt;
        }
    }
    return d;
}

bool Dependency::accepts(const Version& v) const {
    return compareOp(v, op, version);
}

std::string Dependency::str() const {
    std::string s;
    switch (type) {
    case Type::Optional: s = "? "; break;
    case Type::Incompatible: s = "! "; break;
    case Type::NoOrder: s = "~ "; break;
    case Type::Required: break;
    }
    s += id;
    if (!op.empty()) s += " " + op + " " + version.str();
    return s;
}

// ------------------------------------------------------------ pack.json

bool parseManifest(const std::string& text, PackManifest& out, std::string* error) {
    json j;
    if (!datafile::parseAny(text, j, error)) return false; // JSON or YAML
    return parseManifestData(j, out, error);
}

bool parseManifestData(const json& j, PackManifest& out, std::string* error) {
    if (!j.is_object()) {
        fail(error, "expected a JSON object");
        return false;
    }
    PackManifest m;
    try {
        m.id = j.value("id", std::string());
        m.title = datafile::text(j, "title");
        m.version = datafile::text(j, "version", m.version);
        m.author = j.value("author", std::string());
        m.description = j.value("description", std::string());
        m.preview = j.value("preview", std::string());
        m.game = j.value("game", std::string());
        m.gameVersion = datafile::text(j, "game_version");
        m.entitlement = j.value("entitlement", std::string());
        m.hostShare = j.value("host_share", false);
        m.publicKey = j.value("public_key", std::string());
        const std::string kind = j.value("kind", std::string("mod"));
        if (kind == "dlc") m.kind = Kind::Dlc;
        else if (kind == "mod") m.kind = Kind::Mod;
        else {
            fail(error, "\"kind\" must be \"mod\" or \"dlc\", not \"" + kind + "\"");
            return false;
        }
        const std::string mp = j.value("multiplayer", std::string("everyone"));
        if (mp == "everyone") m.multiplayer = Multiplayer::Everyone;
        else if (mp == "local") m.multiplayer = Multiplayer::Local;
        else {
            fail(error, "\"multiplayer\" must be \"everyone\" or \"local\", not \"" + mp + "\"");
            return false;
        }
    } catch (const json::type_error& e) {
        fail(error, std::string("a field has the wrong type: ") + e.what());
        return false;
    }
    if (!validId(m.id)) {
        fail(error, "\"id\" is required: letters, digits, '.', '-', '_' (e.g. \"com.example.mygame.frost\")");
        return false;
    }
    if (m.title.empty()) {
        fail(error, "\"title\" is required");
        return false;
    }
    if (!Version::parse(m.version)) {
        fail(error, "\"version\" \"" + m.version + "\" isn't a version like 1.2.3");
        return false;
    }
    if (!m.gameVersion.empty()) {
        std::string op;
        Version v;
        if (!parseConstraint(m.gameVersion, op, v)) {
            fail(error, "\"game_version\" \"" + m.gameVersion + "\" isn't like \">= 0.3\"");
            return false;
        }
    }
    if (!m.publicKey.empty()) {
        seal::PublicKey k{};
        if (!seal::fromHex(m.publicKey, k)) {
            fail(error, "\"public_key\" must be 64 hex digits (kke_seal keygen prints it)");
            return false;
        }
    }
    if (m.kind == Kind::Mod && (!m.entitlement.empty() || m.hostShare)) {
        fail(error, "\"entitlement\" and \"host_share\" are for DLC (\"kind\": \"dlc\")");
        return false;
    }
    std::string arrayError;
    const std::vector<std::string> deps = stringArray(j, "dependencies", arrayError);
    m.loadAfter = stringArray(j, "load_after", arrayError);
    m.loadBefore = stringArray(j, "load_before", arrayError);
    if (!arrayError.empty()) {
        fail(error, arrayError);
        return false;
    }
    for (const std::string& d : deps) {
        std::string depError;
        std::optional<Dependency> dep = Dependency::parse(d, &depError);
        if (!dep) {
            fail(error, depError);
            return false;
        }
        if (dep->id == m.id) {
            fail(error, "a pack can't depend on itself");
            return false;
        }
        m.dependencies.push_back(*dep);
    }
    out = std::move(m);
    return true;
}

bool loadManifest(const fs::path& packFolder, PackManifest& out, std::string* error, std::string* warning) {
    datafile::Loaded loaded;
    if (!datafile::load(packFolder, kManifestStem, loaded, error)) return false;
    if (warning) *warning = loaded.warning;
    std::string why;
    if (!parseManifestData(loaded.data, out, &why)) {
        fail(error, loaded.file.string() + ": " + why);
        return false;
    }
    out.root = packFolder;
    return true;
}

std::string toJson(const PackManifest& m) {
    json j = json::object();
    j["id"] = m.id;
    j["title"] = m.title;
    j["version"] = m.version;
    j["kind"] = toString(m.kind);
    if (!m.author.empty()) j["author"] = m.author;
    if (!m.description.empty()) j["description"] = m.description;
    if (!m.preview.empty()) j["preview"] = m.preview;
    if (!m.game.empty()) j["game"] = m.game;
    if (!m.gameVersion.empty()) j["game_version"] = m.gameVersion;
    if (!m.dependencies.empty()) {
        json deps = json::array();
        for (const Dependency& d : m.dependencies) deps.push_back(d.str());
        j["dependencies"] = deps;
    }
    if (!m.loadAfter.empty()) j["load_after"] = m.loadAfter;
    if (!m.loadBefore.empty()) j["load_before"] = m.loadBefore;
    if (!m.entitlement.empty()) j["entitlement"] = m.entitlement;
    if (m.hostShare) j["host_share"] = true;
    if (m.multiplayer == Multiplayer::Local) j["multiplayer"] = "local";
    if (!m.publicKey.empty()) j["public_key"] = m.publicKey;
    return j.dump(2) + "\n";
}

// ------------------------------------------------------------ discovery

std::vector<PackManifest> discover(const std::vector<SearchRoot>& roots, std::vector<Problem>* problems) {
    std::vector<PackManifest> found;
    std::map<std::string, size_t> byId;
    auto report = [&](const std::string& pack, const std::string& message) {
        if (problems) problems->push_back({ pack, message });
    };
    for (const SearchRoot& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root.path, ec)) continue;
        std::vector<fs::path> folders;
        for (fs::directory_iterator it(root.path, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_symlink(e2) || !it->is_directory(e2)) continue;
            if (datafile::exists(it->path(), kManifestStem)) folders.push_back(it->path());
        }
        if (ec) report(root.path.string(), "can't list: " + ec.message());
        std::sort(folders.begin(), folders.end());
        for (const fs::path& folder : folders) {
            PackManifest m;
            std::string error, warning;
            if (!loadManifest(folder, m, &error, &warning)) {
                report(folder.filename().string(), error);
                continue;
            }
            if (!warning.empty()) report(m.id, warning);
            m.source = root.name;
            std::error_code e2;
            const bool hasSeal = fs::exists(folder / seal::kSealFileName, e2);
            if (!m.publicKey.empty()) {
                seal::PublicKey key{};
                seal::fromHex(m.publicKey, key);
                if (!hasSeal) {
                    m.sealProblem = "has a public_key but no kke.seal";
                } else {
                    const seal::Report r = seal::verify(folder, key);
                    m.sealed = r.ok();
                    if (!m.sealed) m.sealProblem = r.summary();
                }
                if (!m.sealed) report(m.id, "not verified: " + m.sealProblem);
            }
            auto it = byId.find(m.id);
            if (it == byId.end()) {
                byId[m.id] = found.size();
                found.push_back(std::move(m));
                continue;
            }
            PackManifest& kept = found[it->second];
            const Version mine = Version::parse(m.version).value_or(Version{}), theirs = Version::parse(kept.version).value_or(Version{});
            if (mine > theirs) {
                report(m.id, "installed twice: using " + m.version + " (" + m.source + ") over " + kept.version + " (" + kept.source + ")");
                kept = std::move(m);
            } else {
                report(m.id, "installed twice: using " + kept.version + " (" + kept.source + ") over " + m.version + " (" + m.source + ")");
            }
        }
    }
    return found;
}

// ------------------------------------------------------------ mods.json

const ModList::Entry* ModList::find(const std::string& id) const {
    for (const Entry& e : entries)
        if (e.id == id) return &e;
    return nullptr;
}

bool ModList::enabled(const std::string& id) const {
    const Entry* e = find(id);
    return !e || e->enabled;
}

void ModList::setEnabled(const std::string& id, bool on) {
    for (Entry& e : entries)
        if (e.id == id) {
            e.enabled = on;
            return;
        }
    entries.push_back({ id, on });
}

void ModList::move(const std::string& id, size_t index) {
    Entry entry{ id, true };
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (it->id == id) {
            entry = *it;
            entries.erase(it);
            break;
        }
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(std::min(index, entries.size())), entry);
}

std::string ModList::toJson() const {
    json packs = json::array();
    for (const Entry& e : entries) packs.push_back({ { "id", e.id }, { "enabled", e.enabled } });
    return json{ { "format", "kke-modlist-1" }, { "packs", packs } }.dump(2) + "\n";
}

bool ModList::fromJson(const std::string& text, ModList& out, std::string* error) {
    json j;
    if (!datafile::parseAny(text, j, error)) return false; // JSON or YAML
    try {
        if (j.value("format", std::string()) != "kke-modlist-1") {
            fail(error, "not a kke-modlist-1 file");
            return false;
        }
        ModList list;
        for (const json& e : j.at("packs")) {
            const std::string id = e.at("id").get<std::string>();
            if (!validId(id) || list.find(id)) continue; // tolerate hand edits: skip junk and repeats
            list.entries.push_back({ id, e.value("enabled", true) });
        }
        out = std::move(list);
        return true;
    } catch (const json::exception& e) {
        fail(error, std::string("mods.json: ") + e.what());
        return false;
    }
}

bool ModList::save(const fs::path& file, std::string* error) const {
    return datafile::saveFile(file, json::parse(toJson()), error);
}

bool ModList::load(const fs::path& file, ModList& out, std::string* error) {
    const fs::path folder = file.parent_path().empty() ? fs::path(".") : file.parent_path();
    const std::string stem = file.stem().string();
    if (!datafile::exists(folder, stem)) {
        out = {};
        return true;
    }
    datafile::Loaded loaded;
    if (!datafile::load(folder, stem, loaded, error)) return false;
    std::string why;
    if (!fromJson(loaded.data.dump(), out, &why)) {
        fail(error, loaded.file.string() + ": " + why);
        return false;
    }
    return true;
}

// ------------------------------------------------------------ entitlements

Entitlements Entitlements::fromLicenseExtra(const std::map<std::string, std::string>& extra) {
    Entitlements e;
    auto it = extra.find("dlc");
    if (it == extra.end()) return e;
    std::stringstream ss(it->second);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) e.owned.insert(item);
    }
    return e;
}

// ------------------------------------------------------------ resolve

MountPlan resolve(const std::vector<PackManifest>& available, const ModList& list, const ResolveOptions& options) {
    MountPlan plan;
    std::map<std::string, std::string> skippedWhy;
    auto skip = [&](const std::string& id, const std::string& reason) {
        plan.skipped.push_back({ id, reason });
        skippedWhy[id] = reason;
    };

    // 1. what the player turned on and may use
    std::vector<const PackManifest*> active;
    std::optional<Version> gameVersion;
    if (!options.gameVersion.empty()) gameVersion = Version::parse(options.gameVersion);
    for (const PackManifest& p : available) {
        if (!list.enabled(p.id)) {
            skip(p.id, "turned off");
            continue;
        }
        if (!options.gameId.empty() && !p.game.empty() && p.game != options.gameId) {
            skip(p.id, "made for another game (" + p.game + ")");
            continue;
        }
        if (gameVersion && !p.gameVersion.empty()) {
            std::string op;
            Version want;
            if (parseConstraint(p.gameVersion, op, want) && !compareOp(*gameVersion, op, want)) {
                skip(p.id, "needs game version " + op + " " + want.str() + " (this is " + gameVersion->str() + ")");
                continue;
            }
        }
        if (p.kind == Kind::Dlc) {
            if (options.officialKey) {
                const seal::Report r = seal::verify(p.root, *options.officialKey);
                if (!r.ok()) {
                    skip(p.id, "DLC not signed by the game's developer: " + r.summary());
                    continue;
                }
            }
            if (!options.entitlements.owns(p.entitlement)) {
                if (p.hostShare && options.entitlements.hostShared.count(p.entitlement)) {
                    plan.borrowed.push_back(p.id);
                } else {
                    skip(p.id, "not owned (" + p.entitlement + ")");
                    continue;
                }
            }
        }
        active.push_back(&p);
    }

    // 2. dependencies, until nothing changes (dropping one can strand another)
    auto findActive = [&](const std::string& id) -> const PackManifest* {
        for (const PackManifest* p : active)
            if (p->id == id) return p;
        return nullptr;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < active.size(); ++i) {
            const PackManifest& p = *active[i];
            std::string reason;
            for (const Dependency& d : p.dependencies) {
                const PackManifest* other = findActive(d.id);
                const Version otherVersion = other ? Version::parse(other->version).value_or(Version{}) : Version{};
                if (d.type == Dependency::Type::Incompatible) {
                    if (other && d.accepts(otherVersion)) reason = "incompatible with " + d.id + " " + other->version;
                } else if (!other) {
                    if (d.type == Dependency::Type::Optional) continue;
                    auto why = skippedWhy.find(d.id);
                    reason = "needs " + d.id + (why != skippedWhy.end() ? ", which isn't mounted (" + why->second + ")" : ", which isn't installed");
                } else if (!d.accepts(otherVersion)) {
                    reason = "needs " + d.id + " " + d.op + " " + d.version.str() + " (installed: " + other->version + ")";
                }
                if (!reason.empty()) break;
            }
            if (reason.empty()) continue;
            skip(p.id, reason);
            plan.borrowed.erase(std::remove(plan.borrowed.begin(), plan.borrowed.end(), p.id), plan.borrowed.end());
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
        }
    }

    // 3. order: go through the packs in the player's order (DLC first) and
    // place each one right after whatever it needs to follow, so a mod the
    // player put first pulls its library up with it.
    const size_t n = active.size();
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < n; ++i) index[active[i]->id] = i;
    auto key = [&](size_t i) {
        const PackManifest& p = *active[i];
        size_t pos = list.entries.size();
        for (size_t k = 0; k < list.entries.size(); ++k)
            if (list.entries[k].id == p.id) pos = k;
        return std::tuple<int, size_t, std::string>(p.kind == Kind::Dlc ? 0 : 1, pos, p.id);
    };
    auto less = [&](size_t a, size_t b) { return key(a) < key(b); };
    std::vector<std::set<size_t>> before(n); // before[b] = packs that must come before b
    auto edge = [&](size_t first, size_t second, const std::string& why) {
        if (first == second) return;
        if (active[first]->kind == Kind::Mod && active[second]->kind == Kind::Dlc) {
            plan.warnings.push_back({ active[second]->id, why + ": ignored, DLC always loads before mods" });
            return;
        }
        before[second].insert(first);
    };
    for (size_t i = 0; i < n; ++i) {
        const PackManifest& p = *active[i];
        for (const Dependency& d : p.dependencies) {
            if (d.type != Dependency::Type::Required && d.type != Dependency::Type::Optional) continue;
            auto it = index.find(d.id);
            if (it != index.end()) edge(it->second, i, p.id + " depends on " + d.id);
        }
        for (const std::string& a : p.loadAfter) {
            auto it = index.find(a);
            if (it != index.end()) edge(it->second, i, p.id + " loads after " + a);
        }
        for (const std::string& b : p.loadBefore) {
            auto it = index.find(b);
            if (it != index.end()) edge(i, it->second, p.id + " loads before " + b);
        }
    }
    std::vector<size_t> sorted(n);
    for (size_t i = 0; i < n; ++i) sorted[i] = i;
    std::sort(sorted.begin(), sorted.end(), less);
    enum class Mark { None, Visiting, Placed };
    std::vector<Mark> mark(n, Mark::None);
    auto visit = [&](auto& self, size_t i) -> void {
        mark[i] = Mark::Visiting;
        std::vector<size_t> first(before[i].begin(), before[i].end());
        std::sort(first.begin(), first.end(), less);
        for (size_t f : first) {
            if (mark[f] == Mark::Visiting) {
                // A cycle: the rules of the pack reached first (the one the
                // player put earlier) are kept; this one is dropped.
                plan.warnings.push_back({ active[i]->id, "load order cycle: " + active[i]->id + " should load after " + active[f]->id +
                                                             ", which should load after it; " + active[f]->id + " loads last" });
                continue;
            }
            if (mark[f] == Mark::None) self(self, f);
        }
        mark[i] = Mark::Placed;
        plan.order.push_back(*active[i]);
    };
    for (size_t i : sorted)
        if (mark[i] == Mark::None) visit(visit, i);
    return plan;
}

// ------------------------------------------------------------ mount

std::string Mount::normalize(const std::string& path) {
    std::string s = path;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s[0] == '/') return {};
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('/', start);
        if (end == std::string::npos) end = s.size();
        const std::string part = s.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string::npos) return {};
        if (!out.empty()) out += '/';
        out += lower(part);
    }
    return out;
}

Mount::Mount(const fs::path& baseRoot, const MountPlan& plan) {
    addLayer("base", baseRoot, false);
    for (const PackManifest& p : plan.order) addLayer(p.id, p.root);
}

void Mount::addLayer(const std::string& id, const fs::path& root, bool isPack) {
    const size_t layer = m_layers.size();
    m_layers.push_back({ id, root });
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2)) {
            it.disable_recursion_pending(); // never follow a link out of the pack
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        const std::string rel = it->path().lexically_relative(root).generic_string();
        if (isPack && (datafile::isVariant(rel, kManifestStem) || rel == seal::kSealFileName)) continue;
        const std::string k = normalize(rel);
        if (k.empty()) continue;
        auto& layers = m_files[k].layers;
        if (!layers.empty() && layers.back().first == layer) {
            // Two spellings of one path in one folder (case-sensitive file
            // systems): keep one, the same one every time.
            layers.back().second = std::min(layers.back().second, rel);
            continue;
        }
        layers.emplace_back(layer, rel);
    }
}

fs::path Mount::resolve(const std::string& path) const {
    const std::string k = normalize(path);
    if (k.empty()) return {};
    auto it = m_files.find(k);
    if (it == m_files.end()) return {};
    const auto& [layer, rel] = it->second.layers.back();
    return m_layers[layer].root / fs::path(rel);
}

std::string Mount::owner(const std::string& path) const {
    auto it = m_files.find(normalize(path));
    if (it == m_files.end()) return {};
    return m_layers[it->second.layers.back().first].id;
}

std::vector<std::string> Mount::list(const std::string& dir, const std::string& extension) const {
    std::vector<std::string> out;
    const std::string d = normalize(dir);
    if (d.empty() && !dir.empty() && normalize(dir + "/x").empty()) return out; // dir leaves the root
    const std::string prefix = d.empty() ? std::string() : d + "/";
    const std::string ext = lower(extension);
    for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
        if (!ext.empty() && (it->first.size() < ext.size() || it->first.compare(it->first.size() - ext.size(), ext.size(), ext) != 0))
            continue;
        out.push_back(it->second.layers.back().second);
    }
    return out;
}

std::vector<Mount::Conflict> Mount::conflicts() const {
    std::vector<Conflict> out;
    for (const auto& file : m_files) {
        const Entry& entry = file.second;
        if (entry.layers.size() < 2) continue;
        Conflict c;
        c.path = entry.layers.back().second;
        for (const auto& layer : entry.layers) c.layers.push_back(m_layers[layer.first].id);
        out.push_back(std::move(c));
    }
    return out;
}

// ------------------------------------------------------------ sessions

std::vector<SessionPack> sessionContent(const MountPlan& plan, std::string* error) {
    std::vector<SessionPack> out;
    for (const PackManifest& p : plan.order) {
        if (p.multiplayer != Multiplayer::Everyone) continue;
        seal::Manifest m;
        std::string why;
        if (!seal::build(p.root, m, &why)) {
            fail(error, p.id + ": " + why);
            return {};
        }
        out.push_back({ p.id, p.version, seal::toHex(seal::digest(m)), p.kind == Kind::Dlc && p.hostShare });
    }
    return out;
}

SessionDiff compare(const std::vector<SessionPack>& host, const std::vector<SessionPack>& guest) {
    SessionDiff d;
    std::map<std::string, const SessionPack*> mine;
    for (const SessionPack& g : guest) mine[g.id] = &g;
    for (const SessionPack& h : host) {
        auto it = mine.find(h.id);
        if (it == mine.end()) d.missing.push_back(h);
        else if (it->second->version != h.version || it->second->digest != h.digest) d.different.push_back(h);
        if (it != mine.end()) mine.erase(it);
    }
    for (const SessionPack& g : guest)
        if (mine.count(g.id)) d.extra.push_back(g);
    return d;
}

} // namespace kke::packs
