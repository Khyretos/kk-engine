#include "kke/Localization.h"

#include "kke/Bidi.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kke {

using nlohmann::json;

const char* pluralName(PluralCategory c) {
    switch (c) {
    case PluralCategory::Zero: return "zero";
    case PluralCategory::One: return "one";
    case PluralCategory::Two: return "two";
    case PluralCategory::Few: return "few";
    case PluralCategory::Many: return "many";
    case PluralCategory::Other: return "other";
    }
    return "other";
}

namespace {

// "pt_br" -> "pt-BR", "ZH-hant-tw" -> "zh-Hant-TW".
std::string normalizeLanguage(std::string code) {
    std::replace(code.begin(), code.end(), '_', '-');
    std::vector<std::string> parts;
    std::stringstream ss(code);
    for (std::string p; std::getline(ss, p, '-');)
        if (!p.empty()) parts.push_back(p);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string p = parts[i];
        for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (i > 0 && p.size() == 2) for (char& c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (i > 0 && p.size() == 4) p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
        out += (i ? "-" : "") + p;
    }
    return out;
}

// The operands CLDR's rules are written in.
struct Operands {
    double n;     // absolute value
    long long i;  // integer digits
    int v;        // visible fraction digits
    long long f;  // the fraction digits as a number
};

Operands operandsOf(double value) {
    Operands o{};
    o.n = std::fabs(value);
    o.i = static_cast<long long>(std::floor(std::min(o.n, 9.0e18)));
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", o.n - std::floor(o.n));
    std::string frac = buf; // "0.500000"
    frac = frac.substr(2);
    while (!frac.empty() && frac.back() == '0') frac.pop_back();
    o.v = static_cast<int>(frac.size());
    o.f = frac.empty() ? 0 : std::stoll(frac);
    return o;
}

bool within(long long x, long long lo, long long hi) { return x >= lo && x <= hi; }

} // namespace

std::string baseLanguage(const std::string& language) {
    std::string out = normalizeLanguage(language);
    const size_t dash = out.find('-');
    return dash == std::string::npos ? out : out.substr(0, dash);
}

bool isRtlLanguage(const std::string& language) {
    static const std::set<std::string> rtl = { "ar", "he", "fa", "ur", "yi", "ps", "sd", "ug", "dv", "ckb", "ku" };
    return rtl.count(baseLanguage(language)) != 0;
}

PluralCategory pluralCategory(const std::string& language, double value) {
    using P = PluralCategory;
    const std::string full = normalizeLanguage(language);
    const std::string lang = baseLanguage(language);
    const Operands o = operandsOf(value);
    const bool integer = o.v == 0;
    const long long i = o.i, m10 = o.i % 10, m100 = o.i % 100;

    static const std::set<std::string> none = { "ja", "zh", "ko", "vi", "th", "id", "ms", "lo", "km", "my" };
    static const std::set<std::string> oneIsI1 = { "en", "de", "nl", "sv", "fi", "et", "it", "ca", "gl", "ur", "yi", "sw" };
    static const std::set<std::string> oneIsN1 = { "es", "hu", "el", "bg", "tr", "nb", "no", "nn", "sq", "eu", "ka", "kk", "az", "uz", "ta", "te", "ml", "mn" };
    static const std::set<std::string> oneIsI0 = { "hi", "bn", "fa", "gu", "kn", "zu", "am", "as", "mr" };

    if (none.count(lang)) return P::Other;
    if (oneIsI1.count(lang)) return integer && i == 1 ? P::One : P::Other;
    if (oneIsN1.count(lang)) return o.n == 1.0 ? P::One : P::Other;
    if (oneIsI0.count(lang)) return i == 0 || o.n == 1.0 ? P::One : P::Other;
    if (lang == "fr") return i == 0 || i == 1 ? P::One : P::Other;
    if (lang == "pt") return full == "pt-PT" ? (integer && i == 1 ? P::One : P::Other) : (i == 0 || i == 1 ? P::One : P::Other);
    if (lang == "da") return o.n == 1.0 || (!integer && (i == 0 || i == 1)) ? P::One : P::Other;
    if (lang == "is") return integer && m10 == 1 && m100 != 11 ? P::One : P::Other;
    if (lang == "ru" || lang == "uk" || lang == "be") {
        if (!integer) return P::Other;
        if (m10 == 1 && m100 != 11) return P::One;
        if (within(m10, 2, 4) && !within(m100, 12, 14)) return P::Few;
        return P::Many;
    }
    if (lang == "pl") {
        if (!integer) return P::Other;
        if (i == 1) return P::One;
        if (within(m10, 2, 4) && !within(m100, 12, 14)) return P::Few;
        return P::Many;
    }
    if (lang == "cs" || lang == "sk") {
        if (!integer) return P::Many;
        if (i == 1) return P::One;
        if (within(i, 2, 4)) return P::Few;
        return P::Other;
    }
    if (lang == "hr" || lang == "sr" || lang == "bs" || lang == "sh") {
        if (integer) {
            if (m10 == 1 && m100 != 11) return P::One;
            if (within(m10, 2, 4) && !within(m100, 12, 14)) return P::Few;
            return P::Other;
        }
        const long long fm10 = o.f % 10, fm100 = o.f % 100;
        if (fm10 == 1 && fm100 != 11) return P::One;
        if (within(fm10, 2, 4) && !within(fm100, 12, 14)) return P::Few;
        return P::Other;
    }
    if (lang == "ar") {
        if (!integer) return P::Other;
        if (i == 0) return P::Zero;
        if (i == 1) return P::One;
        if (i == 2) return P::Two;
        if (within(m100, 3, 10)) return P::Few;
        if (within(m100, 11, 99)) return P::Many;
        return P::Other;
    }
    if (lang == "he") {
        if (integer && i == 1) return P::One;
        if (integer && i == 2) return P::Two;
        return P::Other;
    }
    if (lang == "ro") {
        if (integer && i == 1) return P::One;
        if (!integer || i == 0 || within(m100, 1, 19)) return P::Few;
        return P::Other;
    }
    if (lang == "sl") {
        if (integer && m100 == 1) return P::One;
        if (integer && m100 == 2) return P::Two;
        if ((integer && within(m100, 3, 4)) || !integer) return P::Few;
        return P::Other;
    }
    if (lang == "lt") {
        if (!integer) return P::Many;
        if (m10 == 1 && !within(m100, 11, 19)) return P::One;
        if (within(m10, 2, 9) && !within(m100, 11, 19)) return P::Few;
        return P::Other;
    }
    if (lang == "lv") {
        if (m10 == 0 || within(m100, 11, 19)) return P::Zero;
        if (m10 == 1 && m100 != 11) return P::One;
        return P::Other;
    }
    if (lang == "cy") {
        if (!integer) return P::Other;
        if (i == 0) return P::Zero;
        if (i == 1) return P::One;
        if (i == 2) return P::Two;
        if (i == 3) return P::Few;
        if (i == 6) return P::Many;
        return P::Other;
    }
    if (lang == "ga") {
        if (!integer) return P::Other;
        if (i == 1) return P::One;
        if (i == 2) return P::Two;
        if (within(i, 3, 6)) return P::Few;
        if (within(i, 7, 10)) return P::Many;
        return P::Other;
    }
    return P::Other;
}

NumberFormat numberFormat(const std::string& language) {
    const std::string lang = baseLanguage(language);
    static const std::set<std::string> comma = { "de", "es", "it", "pt", "nl", "tr", "id", "da", "el", "ro", "hr", "sl", "vi", "ca", "gl", "sr", "bs" };
    static const std::set<std::string> space = { "ru", "pl", "cs", "sk", "uk", "sv", "fi", "nb", "no", "nn", "bg", "hu", "lt", "lv", "et", "be" };
    NumberFormat f;
    if (comma.count(lang)) {
        f.decimal = ",";
        f.group = ".";
    } else if (lang == "fr") {
        f.decimal = ",";
        f.group = "\xE2\x80\xAF"; // narrow no-break space
    } else if (space.count(lang)) {
        f.decimal = ",";
        f.group = "\xC2\xA0"; // no-break space
    }
    return f;
}

std::string formatNumber(double value, const NumberFormat& format, int decimals) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";
    char buf[512];
    const int digits = decimals < 0 ? 6 : std::min(decimals, 12);
    std::snprintf(buf, sizeof buf, "%.*f", digits, std::fabs(value));
    std::string text = buf;
    if (decimals < 0 && text.find('.') != std::string::npos) {
        while (text.back() == '0') text.pop_back();
        if (text.back() == '.') text.pop_back();
    }
    const size_t dot = text.find('.');
    std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
    const std::string fraction = dot == std::string::npos ? "" : text.substr(dot + 1);
    std::string grouped;
    for (size_t k = 0; k < whole.size(); ++k) {
        if (k > 0 && (whole.size() - k) % 3 == 0) grouped += format.group;
        grouped += whole[k];
    }
    const bool negative = value < 0.0 && (grouped != "0" || !fraction.empty()) && text.find_first_not_of("0.") != std::string::npos;
    return (negative ? "-" : "") + grouped + (fraction.empty() ? "" : format.decimal + fraction);
}

// ---- tables ---------------------------------------------------------------------

namespace {

constexpr size_t kMaxKeys = 200000;

bool flatten(const json& j, const std::string& prefix, int depth, std::map<std::string, std::string>& out, std::string& error) {
    if (depth > 16) {
        error = "'" + prefix + "': nested more than 16 deep";
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
        if (it->is_string()) {
            if (out.size() >= kMaxKeys) {
                error = "more than " + std::to_string(kMaxKeys) + " strings";
                return false;
            }
            out[key] = it->get<std::string>();
        } else if (it->is_object()) {
            if (!flatten(*it, key, depth + 1, out, error)) return false;
        } else {
            error = "'" + key + "' must be text or an object of text";
            return false;
        }
    }
    return true;
}

} // namespace

bool Localizer::addTable(const std::string& language, const json& table, std::string* error) {
    std::string problem;
    const std::string lang = normalizeLanguage(language);
    if (lang.empty()) problem = "no language name";
    else if (!table.is_object()) problem = "a table is a JSON object of strings";
    std::map<std::string, std::string> flat;
    if (problem.empty()) flatten(table, "", 0, flat, problem);
    if (!problem.empty()) {
        if (error) *error = lang + ": " + problem;
        return false;
    }
    auto& dest = m_tables[lang];
    for (auto& [k, v] : flat) dest[k] = std::move(v);
    ++m_version;
    return true;
}

bool Localizer::addTableText(const std::string& language, const std::string& jsonText, std::string* error) {
    json j = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        if (error) *error = language + ": not valid JSON";
        return false;
    }
    return addTable(language, j, error);
}

bool Localizer::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = path + ": can't open";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = path + ": not valid JSON";
        return false;
    }
    std::string lang = std::filesystem::path(path).stem().string();
    json strings = j;
    if (j.is_object() && j.contains("strings") && j["strings"].is_object()) {
        if (j.contains("language") && j["language"].is_string()) lang = j["language"].get<std::string>();
        strings = j["strings"];
    }
    std::string problem;
    if (!addTable(lang, strings, &problem)) {
        if (error) *error = path + ": " + problem;
        return false;
    }
    return true;
}

size_t Localizer::loadFolder(const std::string& folder, std::vector<std::string>* errors) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    size_t loaded = 0;
    for (const fs::path& f : files) {
        std::string problem;
        if (f.filename() == "fonts.json") {
            std::ifstream in(f, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            json j = json::parse(ss.str(), nullptr, false);
            if (j.is_discarded() || !addFonts(j, &problem)) {
                if (errors) errors->push_back(f.string() + ": " + (problem.empty() ? "not valid JSON" : problem));
            }
            continue;
        }
        if (loadFile(f.string(), &problem)) ++loaded;
        else if (errors) errors->push_back(problem);
    }
    return loaded;
}

bool Localizer::setLanguage(const std::string& language) {
    const std::string lang = normalizeLanguage(language);
    if (!hasLanguage(lang) && !hasLanguage(baseLanguage(lang))) return false;
    if (lang != m_language) {
        m_language = lang;
        ++m_version;
    }
    return true;
}

void Localizer::setFallback(const std::string& language) {
    m_fallback = normalizeLanguage(language);
    ++m_version;
}

std::vector<std::string> Localizer::languages() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : m_tables) out.push_back(k);
    return out;
}

std::vector<std::string> Localizer::untranslated(const std::string& language) const {
    std::vector<std::string> out;
    const auto fb = m_tables.find(m_fallback);
    if (fb == m_tables.end()) return out;
    const std::string lang = normalizeLanguage(language);
    for (const auto& [key, text] : fb->second)
        if (!findIn(lang, key) && !findIn(baseLanguage(lang), key)) out.push_back(key);
    return out;
}

const std::string* Localizer::findIn(const std::string& language, const std::string& key) const {
    const auto t = m_tables.find(language);
    if (t == m_tables.end()) return nullptr;
    const auto k = t->second.find(key);
    return k == t->second.end() ? nullptr : &k->second;
}

const std::string* Localizer::find(const std::string& key) const {
    for (const std::string& lang : { m_language, baseLanguage(m_language), m_fallback, baseLanguage(m_fallback) })
        if (const std::string* s = findIn(lang, key)) return s;
    return nullptr;
}

void Localizer::note(const std::string& key) const {
    if (m_missing.size() < 10000 && m_missingSet.insert(key).second) m_missing.push_back(key);
}

void Localizer::clearMissing() const {
    m_missing.clear();
    m_missingSet.clear();
}

std::string Localizer::format(const std::string& pattern, const LocArgs& args, std::vector<std::string>* unfilled) {
    std::string out;
    out.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '{') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
                out += '{';
                ++i;
                continue;
            }
            const size_t close = pattern.find('}', i + 1);
            if (close == std::string::npos) {
                out += c;
                continue;
            }
            const std::string name = pattern.substr(i + 1, close - i - 1);
            const auto a = args.find(name);
            if (a != args.end()) {
                out += a->second;
            } else {
                out += pattern.substr(i, close - i + 1);
                if (unfilled) unfilled->push_back(name);
            }
            i = close;
        } else if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            out += '}';
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

std::string Localizer::text(const std::string& key, const LocArgs& args) const {
    const std::string* s = find(key);
    if (!s) {
        note(key);
        return key;
    }
    return args.empty() && s->find('{') == std::string::npos ? *s : format(*s, args);
}

std::string Localizer::plural(const std::string& key, double n, const LocArgs& args) const {
    const std::string* s = nullptr;
    if (n == 0.0) s = find(key + ".zero"); // "No items", whatever the language's own rules say
    if (!s) s = find(key + "." + pluralName(pluralCategory(m_language, n)));
    if (!s) s = find(key + ".other");
    if (!s) s = find(key);
    if (!s) {
        note(key);
        return key;
    }
    LocArgs all = args;
    if (!all.count("n")) all["n"] = number(n);
    return format(*s, all);
}

std::string Localizer::display(const std::string& text) const {
    if (!rtl()) return bidi::display(text, -1);
    return bidi::display(text, 1);
}

bool Localizer::addFonts(const json& map, std::string* error) {
    if (!map.is_object()) {
        if (error) *error = "fonts: an object like {\"ar\": [\"NotoSansArabic-Regular.ttf\"]}";
        return false;
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        std::vector<std::string> files;
        if (it->is_string()) {
            files.push_back(it->get<std::string>());
        } else if (it->is_array()) {
            for (const json& f : *it) {
                if (!f.is_string()) {
                    if (error) *error = "fonts." + it.key() + ": file names are text";
                    return false;
                }
                files.push_back(f.get<std::string>());
            }
        } else {
            if (error) *error = "fonts." + it.key() + ": a file name or a list of them";
            return false;
        }
        m_fonts[normalizeLanguage(it.key())] = std::move(files);
    }
    ++m_version;
    return true;
}

std::vector<std::string> Localizer::fonts() const { return fonts(m_language); }

std::vector<std::string> Localizer::fonts(const std::string& language) const {
    std::vector<std::string> out;
    const std::string lang = normalizeLanguage(language);
    for (const std::string& l : { lang, baseLanguage(lang) }) {
        const auto f = m_fonts.find(l);
        if (f == m_fonts.end()) continue;
        for (const std::string& file : f->second)
            if (std::find(out.begin(), out.end(), file) == out.end()) out.push_back(file);
    }
    return out;
}

} // namespace kke
