#include "kke/DataFile.h"
#include "kke/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
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

json toJson(const YAML::Node& node) {
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
        for (const YAML::Node& e : node) a.push_back(toJson(e));
        return a;
    }
    case YAML::NodeType::Map: {
        json o = json::object();
        for (const auto& kv : node) {
            if (!kv.first.IsScalar()) throw std::runtime_error("keys must be plain text");
            o[kv.first.Scalar()] = toJson(kv.second);
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

} // namespace

std::optional<Format> formatOf(const fs::path& file) {
    const std::string ext = lower(file.extension().string());
    if (ext == ".json") return Format::Json;
    if (ext == ".yml" || ext == ".yaml") return Format::Yaml;
    return std::nullopt;
}

bool parse(const std::string& text, Format format, json& out, std::string* error) {
    try {
        if (format == Format::Json) out = json::parse(text);
        else out = toJson(YAML::Load(text));
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
    // The newest file wins; on a tie, the earlier of .json, .yml, .yaml.
    size_t newest = 0;
    for (size_t i = 1; i < files.size(); ++i)
        if (modified(files[i]) > modified(files[newest])) newest = i;
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
        log::get("Data")->warn("{}", result.warning);
    }
    out = std::move(result);
    return true;
}

fs::path saveTarget(const fs::path& folder, const std::string& stem) {
    const std::vector<fs::path> files = variants(folder, stem);
    if (files.empty()) return folder / (stem + ".json");
    size_t newest = 0;
    for (size_t i = 1; i < files.size(); ++i)
        if (modified(files[i]) > modified(files[newest])) newest = i;
    return files[newest];
}

std::string text(const json& object, const char* key, const std::string& fallback) {
    if (!object.contains(key) || object[key].is_null()) return fallback;
    const json& v = object[key];
    if (v.is_number()) return v.dump();
    return v.get<std::string>(); // throws json::type_error for anything else, as value() would
}

} // namespace kke::datafile
