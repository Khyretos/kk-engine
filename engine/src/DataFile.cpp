#include "kke/DataFile.h"
#include "kke/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>

namespace kke::datafile {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

constexpr const char* kExtensions[] = { ".json", ".yml", ".yaml" };

// A plain (unquoted) YAML scalar as YAML 1.2's core schema reads it. A
// number only when it survives the round trip, so "1.10" and "007" stay text.
json plainScalar(const std::string& s) {
    if (s.empty() || s == "~" || s == "null" || s == "Null" || s == "NULL") return nullptr;
    if (s == "true" || s == "True" || s == "TRUE") return true;
    if (s == "false" || s == "False" || s == "FALSE") return false;
    static const std::regex integer("-?[0-9]+");
    static const std::regex real("-?(\\.[0-9]+|[0-9]+(\\.[0-9]*)?)([eE][-+]?[0-9]+)?");
    if (std::regex_match(s, integer)) {
        try {
            const long long v = std::stoll(s);
            if (std::to_string(v) == s) return v;
        } catch (const std::out_of_range&) {
        }
        return s;
    }
    if (std::regex_match(s, real)) {
        try {
            const json v = std::stod(s);
            if (v.dump() == s) return v;
        } catch (const std::out_of_range&) {
        }
    }
    return s;
}

constexpr size_t kMaxValues = 1'000'000; // after aliases are expanded
constexpr int kMaxDepth = 256;

// Aliases share nodes, so a small file can expand into billions of values:
// `budget` counts every value converted.
json toJson(const YAML::Node& node, size_t& budget, int depth = 0) {
    if (++budget > kMaxValues) throw std::runtime_error("too much data (more than a million values once aliases are expanded)");
    if (depth > kMaxDepth) throw std::runtime_error("nested more than " + std::to_string(kMaxDepth) + " levels deep");
    switch (node.Type()) {
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined:
        return nullptr;
    case YAML::NodeType::Scalar:
        // "!" is how yaml-cpp marks a quoted scalar: always text.
        if (node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str") return node.Scalar();
        return plainScalar(node.Scalar());
    case YAML::NodeType::Sequence: {
        json a = json::array();
        for (const YAML::Node& e : node) a.push_back(toJson(e, budget, depth + 1));
        return a;
    }
    case YAML::NodeType::Map: {
        json o = json::object();
        for (const auto& kv : node) {
            if (!kv.first.IsScalar()) throw std::runtime_error("keys must be plain text");
            o[kv.first.Scalar()] = toJson(kv.second, budget, depth + 1);
        }
        return o;
    }
    }
    return nullptr;
}

void emit(YAML::Emitter& out, const json& j) {
    switch (j.type()) {
    case json::value_t::object:
        out << YAML::BeginMap;
        for (auto it = j.begin(); it != j.end(); ++it) {
            out << YAML::Key;
            if (plainScalar(it.key()).is_string()) out << it.key();
            else out << YAML::DoubleQuoted << it.key();
            out << YAML::Value;
            emit(out, it.value());
        }
        out << YAML::EndMap;
        break;
    case json::value_t::array:
        out << YAML::BeginSeq;
        for (const json& e : j) emit(out, e);
        out << YAML::EndSeq;
        break;
    case json::value_t::string: {
        const std::string& s = j.get_ref<const std::string&>();
        // Quote text that would read back as something else ("1.2", "true", "").
        if (plainScalar(s).is_string()) out << s;
        else out << YAML::DoubleQuoted << s;
        break;
    }
    case json::value_t::null: out << YAML::Null; break;
    case json::value_t::boolean: out << j.get<bool>(); break;
    default: out << j.dump(); break; // numbers, in JSON's spelling (reads back as the same number)
    }
}

fs::file_time_type modified(const fs::path& file) {
    std::error_code ec;
    const fs::file_time_type t = fs::last_write_time(file, ec);
    return ec ? fs::file_time_type::min() : t;
}

size_t newestOf(const std::vector<fs::path>& files) {
    // The newest file wins; on a tie, the earlier of .json, .yml, .yaml.
    size_t newest = 0;
    for (size_t i = 1; i < files.size(); ++i)
        if (modified(files[i]) > modified(files[newest])) newest = i;
    return newest;
}

// A conflict is logged once per pair of file versions, however often the
// file is read (settings and input maps are re-read while playing).
void warnOnce(const std::string& key, const std::string& message) {
    static std::mutex mutex;
    static std::set<std::string> warned;
    {
        std::lock_guard lock(mutex);
        if (!warned.insert(key).second) return;
    }
    log::get("Data")->warn("{}", message);
}

std::string versionKey(const std::vector<fs::path>& files) {
    std::string key;
    for (const fs::path& f : files) key += f.string() + "@" + std::to_string(modified(f).time_since_epoch().count()) + "|";
    return key;
}

bool readWhole(const fs::path& file, std::string& text) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    if (f.bad()) return false;
    text = ss.str();
    return true;
}

bool startsLikeJson(const std::string& text) {
    const size_t i = text.find_first_not_of(" \t\r\n");
    return i != std::string::npos && (text[i] == '{' || text[i] == '[');
}

} // namespace

std::optional<Format> formatOf(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    if (ext == ".json") return Format::Json;
    if (ext == ".yml" || ext == ".yaml") return Format::Yaml;
    return std::nullopt;
}

bool parse(const std::string& text, Format format, json& out, std::string* error) {
    try {
        size_t budget = 0;
        if (format == Format::Json) out = json::parse(text, nullptr, true, /*ignore_comments=*/true);
        else out = toJson(YAML::Load(text), budget);
        return true;
    } catch (const json::exception& e) {
        fail(error, std::string("invalid JSON: ") + e.what());
    } catch (const YAML::Exception& e) {
        fail(error, std::string("invalid YAML: ") + e.what());
    } catch (const std::runtime_error& e) {
        fail(error, std::string("invalid YAML: ") + e.what());
    }
    return false;
}

std::string dump(const json& data, Format format) {
    if (format == Format::Json) return data.dump(2) + "\n";
    YAML::Emitter out;
    emit(out, data);
    return std::string(out.c_str()) + "\n";
}

bool loadFile(const fs::path& file, json& out, std::string* error) {
    const std::optional<Format> format = formatOf(file);
    if (!format) {
        fail(error, file.string() + ": not a .json, .yml or .yaml file");
        return false;
    }
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        fail(error, "can't read " + file.string());
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string why;
    if (!parse(ss.str(), *format, out, &why)) {
        fail(error, file.string() + ": " + why);
        return false;
    }
    return true;
}

bool saveFile(const fs::path& file, const json& data, std::string* error) {
    const std::optional<Format> format = formatOf(file);
    if (!format) {
        fail(error, file.string() + ": not a .json, .yml or .yaml file");
        return false;
    }
    const fs::path tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << dump(data, *format))) {
            fail(error, "can't write " + tmp.string());
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fail(error, "can't replace " + file.string() + ": " + ec.message());
        return false;
    }
    return true;
}

std::vector<fs::path> variants(const fs::path& folder, const std::string& stem) {
    std::vector<fs::path> out;
    for (const char* ext : kExtensions) {
        const fs::path p = folder / (stem + ext);
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) out.push_back(p);
    }
    return out;
}

bool exists(const fs::path& folder, const std::string& stem) {
    return !variants(folder, stem).empty();
}

bool isVariant(const std::string& fileName, const std::string& stem) {
    for (const char* ext : kExtensions)
        if (fileName == stem + ext) return true;
    return false;
}

bool load(const fs::path& folder, const std::string& stem, Loaded& out, std::string* error) {
    const std::vector<fs::path> files = variants(folder, stem);
    if (files.empty()) {
        fail(error, "no " + stem + ".json or " + stem + ".yml in " + folder.string());
        return false;
    }
    const size_t newest = newestOf(files);
    Loaded result;
    result.file = files[newest];
    if (!loadFile(result.file, result.data, error)) return false;
    std::vector<std::string> differing;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i == newest) continue;
        json other;
        std::string why;
        if (!loadFile(files[i], other, &why) || other != result.data) differing.push_back(files[i].filename().string());
    }
    if (!differing.empty()) {
        std::string names;
        for (const std::string& d : differing) names += (names.empty() ? "" : " and ") + d;
        result.warning = result.file.filename().string() + " and " + names + " in " + folder.string() + " differ: using " +
                         result.file.filename().string() + ", the most recently changed. Delete the other one to silence this.";
        warnOnce(versionKey(files), result.warning);
    }
    out = std::move(result);
    return true;
}

fs::path saveTarget(const fs::path& folder, const std::string& stem) {
    const std::vector<fs::path> files = variants(folder, stem);
    if (files.empty()) return folder / (stem + ".json");
    return files[newestOf(files)];
}

std::string text(const json& object, const char* key, const std::string& fallback) {
    if (!object.contains(key) || object[key].is_null()) return fallback;
    const json& v = object[key];
    if (v.is_number()) return v.dump();
    return v.get<std::string>(); // throws json::type_error for anything else, as value() would
}

// ---- By path

bool parseAny(const std::string& text, json& out, std::string* error) {
    std::string jsonError, yamlError;
    if (parse(text, Format::Json, out, &jsonError)) return true;
    if (parse(text, Format::Yaml, out, &yamlError)) return true;
    fail(error, startsLikeJson(text) ? jsonError : yamlError);
    return false;
}

fs::path resolve(const fs::path& file) {
    if (!formatOf(file)) return file;
    const std::vector<fs::path> files = variants(file.parent_path(), file.stem().string());
    if (files.empty()) return file;
    const size_t newest = newestOf(files);
    if (files.size() > 1) {
        // Same check as load(): twins are fine when they say the same.
        std::string text;
        json chosen, other;
        const bool chosenOk = readWhole(files[newest], text) && parseAny(text, chosen);
        std::string differing;
        for (size_t i = 0; i < files.size(); ++i) {
            if (i == newest) continue;
            if (!chosenOk || !readWhole(files[i], text) || !parseAny(text, other) || other != chosen)
                differing += (differing.empty() ? "" : " and ") + files[i].filename().string();
        }
        if (!differing.empty())
            warnOnce(versionKey(files), files[newest].filename().string() + " and " + differing + " in " + file.parent_path().string() +
                                            " differ: using " + files[newest].filename().string() +
                                            ", the most recently changed. Delete the other one to silence this.");
    }
    return files[newest];
}

bool readText(const fs::path& file, std::string& text, bool* exists, fs::path* used) {
    const fs::path p = resolve(file);
    if (used) *used = p;
    std::error_code ec;
    const bool there = fs::is_regular_file(p, ec);
    if (exists) *exists = there;
    return there && readWhole(p, text);
}

bool loadPath(const fs::path& file, json& out, std::string* error, bool* exists, fs::path* used) {
    std::string text;
    fs::path p;
    bool there = false;
    const bool ok = readText(file, text, &there, &p);
    if (exists) *exists = there;
    if (used) *used = p;
    if (!ok) {
        fail(error, there ? "can't read " + p.string() : std::string());
        return false;
    }
    std::string why;
    if (!parseAny(text, out, &why)) {
        fail(error, p.string() + ": " + why);
        return false;
    }
    return true;
}

fs::path saveTarget(const fs::path& file) { return resolve(file); }

std::string forFile(const std::string& jsonText, const fs::path& target) {
    if (formatOf(target) != Format::Yaml) return jsonText;
    json v;
    return parse(jsonText, Format::Json, v) ? dump(v, Format::Yaml) : jsonText;
}

std::string nameOf(const fs::path& file, const std::string& kind) {
    if (!formatOf(file)) return {};
    const std::string stem = file.stem().string();
    if (stem.size() <= kind.size() || stem.compare(stem.size() - kind.size(), kind.size(), kind) != 0) return {};
    return stem.substr(0, stem.size() - kind.size());
}

} // namespace kke::datafile
