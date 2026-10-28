#include "kke/Localization.h"

#include "kke/Bidi.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kke {

namespace {

using Json = nlohmann::json;

const char* const kPluralNames[] = { "zero", "one", "two", "few", "many", "other" };

bool isFormsObject(const Json& o) {
    return o.is_object() && o.contains("other");
}

// An object made only of plural names, without "other": a translator's mistake.
bool lacksOther(const Json& o) {
    if (!o.is_object() || o.empty() || o.contains("other")) return false;
    for (auto it = o.begin(); it != o.end(); ++it) {
        bool known = false;
        for (const char* n : kPluralNames)
            if (it.key() == n) known = true;
        if (!known) return false;
    }
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "pt_br" -> "pt-BR"
std::string normalize(std::string code) {
    for (char& c : code)
        if (c == '_') c = '-';
    const size_t dash = code.find('-');
    std::string lang = lower(code.substr(0, dash));
    if (dash == std::string::npos) return lang;
    std::string rest = code.substr(dash + 1);
    if (rest.size() == 2)
        for (char& c : rest) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return lang + "-" + rest;
}

// ---- plural rules (Unicode CLDR, plural categories for cardinals)

struct Operands {
    long long i = 0; // the whole part
    int v = 0;       // visible fraction digits
    long long f = 0; // the fraction digits as a number
    long long t = 0; // the same without trailing zeros (here the same as f)
};

Operands operandsOf(double n) {
    n = std::fabs(n);
    Operands o;
    o.i = static_cast<long long>(std::floor(n));
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f", n);
    std::string frac = std::strchr(buf, '.') + 1;
    while (!frac.empty() && frac.back() == '0') frac.pop_back();
    o.v = static_cast<int>(frac.size());
    o.f = o.t = frac.empty() ? 0 : std::atoll(frac.c_str());
    return o;
}

bool in(long long x, long long lo, long long hi) { return x >= lo && x <= hi; }

std::string categoryOf(const std::string& code, double n) {
    const Operands o = operandsOf(n);
    const long long i = o.i;
    const bool whole = o.v == 0;
    const bool isOne = whole && i == 1; // n = 1
    const std::string base = code.substr(0, code.find('-'));

    static const std::set<std::string> noPlural = { "ja", "zh", "ko", "vi", "th", "id", "ms", "my", "lo", "km", "jv" };
    static const std::set<std::string> oneIfOne = { "en", "de", "nl", "sv", "nb", "nn", "no", "ca", "fi", "et", "it", "ur", "sw", "gl", "fy", "af", "eo", "sq" };
    static const std::set<std::string> nIsOne = { "es", "el", "hu", "tr", "bg", "eu", "ka", "kk", "az", "mn", "ta", "te", "ml", "kn", "mr", "ne", "uz" };
    static const std::set<std::string> zeroOrOne = { "hi", "bn", "gu", "fa", "zu", "am", "as" };

    if (noPlural.count(base)) return "other";
    if (code == "pt-PT") return isOne ? "one" : "other";
    if (oneIfOne.count(base)) return isOne ? "one" : "other";
    if (nIsOne.count(base)) return isOne ? "one" : "other";
    if (zeroOrOne.count(base)) return (i == 0 || isOne) ? "one" : "other";
    if (base == "fr" || base == "pt") return (i == 0 || i == 1) ? "one" : "other";
    if (base == "da") return (isOne || (o.t != 0 && (i == 0 || i == 1))) ? "one" : "other";
    if (base == "is") return ((o.t == 0 && i % 10 == 1 && i % 100 != 11) || o.t != 0) ? "one" : "other";
    if (base == "ru" || base == "uk" || base == "be") {
        if (!whole) return "other";
        if (i % 10 == 1 && i % 100 != 11) return "one";
        if (in(i % 10, 2, 4) && !in(i % 100, 12, 14)) return "few";
        return "many";
    }
    if (base == "pl") {
        if (isOne) return "one";
        if (!whole) return "other";
        if (in(i % 10, 2, 4) && !in(i % 100, 12, 14)) return "few";
        return "many";
    }
    if (base == "cs" || base == "sk") {
        if (isOne) return "one";
        if (whole && in(i, 2, 4)) return "few";
        if (!whole) return "many";
        return "other";
    }
    if (base == "hr" || base == "sr" || base == "bs") {
        if ((whole && i % 10 == 1 && i % 100 != 11) || (o.f % 10 == 1 && o.f % 100 != 11)) return "one";
        if ((whole && in(i % 10, 2, 4) && !in(i % 100, 12, 14)) || (in(o.f % 10, 2, 4) && !in(o.f % 100, 12, 14))) return "few";
        return "other";
    }
    if (base == "sl") {
        if (whole && i % 100 == 1) return "one";
        if (whole && i % 100 == 2) return "two";
        if ((whole && in(i % 100, 3, 4)) || !whole) return "few";
        return "other";
    }
    if (base == "ro") {
        if (isOne) return "one";
        if (!whole || i == 0 || (i != 1 && in(i % 100, 1, 19))) return "few";
        return "other";
    }
    if (base == "lt") {
        if (!whole) return "many";
        if (i % 10 == 1 && !in(i % 100, 11, 19)) return "one";
        if (in(i % 10, 2, 9) && !in(i % 100, 11, 19)) return "few";
        return "other";
    }
    if (base == "lv") {
        if (whole && (i % 10 == 0 || in(i % 100, 11, 19))) return "zero";
        if (whole && i % 10 == 1 && i % 100 != 11) return "one";
        return "other";
    }
    if (base == "cy") {
        if (!whole) return "other";
        if (i == 0) return "zero";
        if (i == 1) return "one";
        if (i == 2) return "two";
        if (i == 3) return "few";
        if (i == 6) return "many";
        return "other";
    }
    if (base == "ga") {
        if (!whole) return "other";
        if (i == 1) return "one";
        if (i == 2) return "two";
        if (in(i, 3, 6)) return "few";
        if (in(i, 7, 10)) return "many";
        return "other";
    }
    if (base == "ar") {
        if (!whole) return "other";
        if (i == 0) return "zero";
        if (i == 1) return "one";
        if (i == 2) return "two";
        if (in(i % 100, 3, 10)) return "few";
        if (in(i % 100, 11, 99)) return "many";
        return "other";
    }
    if (base == "he") {
        if ((whole && i == 1) || (i == 0 && !whole)) return "one";
        if (whole && i == 2) return "two";
        return "other";
    }
    return isOne ? "one" : "other"; // a language this list doesn't know
}

// {name} placeholders of a template, without the escaped braces.
std::set<std::string> placeholdersOf(const std::string& s) {
    std::set<std::string> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '{') { ++i; continue; }
        if (s[i] == '}' && i + 1 < s.size() && s[i + 1] == '}') { ++i; continue; }
        if (s[i] == '{') {
            const size_t end = s.find('}', i);
            if (end == std::string::npos) break;
            out.insert(s.substr(i + 1, end - i - 1));
            i = end;
        }
    }
    return out;
}

std::string digitsAs(const std::string& s, const std::string& system) {
    const char32_t zero = system == "persian" ? 0x06F0 : system == "arabic-indic" ? 0x0660 : 0;
    if (!zero) return s;
    std::string result;
    for (char c : s) {
        if (c >= '0' && c <= '9') result += bidi::encode(std::u32string(1, zero + static_cast<char32_t>(c - '0')));
        else result.push_back(c);
    }
    return result;
}

void defaultsForNumbers(Localization::Language& l) {
    const std::string base = l.code.substr(0, l.code.find('-'));
    static const std::set<std::string> commaDot = { "de", "nl", "es", "it", "pt", "id", "tr", "da", "el", "vi", "ro", "hr", "sl", "sr", "bs" };
    static const std::set<std::string> commaSpace = { "ru", "uk", "pl", "cs", "sk", "sv", "nb", "nn", "no", "fi", "bg", "hu", "lt", "lv", "et", "be" };
    if (base == "fr") { l.decimal = ","; l.group = "\xE2\x80\xAF"; }       // narrow no-break space
    else if (commaSpace.count(base)) { l.decimal = ","; l.group = "\xC2\xA0"; } // no-break space
    else if (commaDot.count(base)) { l.decimal = ","; l.group = "."; }
    if (base == "ar" || base == "fa") { l.decimal = "\xD9\xAB"; l.group = "\xD9\xAC"; }
    if (base == "ar" || base == "he" || base == "fa" || base == "ur") l.rtl = true;
}

} // namespace

Localization& Localization::get() {
    static Localization instance;
    return instance;
}

Localization::Localization() = default;

std::string Localization::baseCode(const std::string& code) {
    return code.substr(0, code.find('-'));
}

bool Localization::loadJson(std::string_view text, std::string* error) {
    Json doc;
    try {
        doc = Json::parse(text);
    } catch (const std::exception& e) {
        if (error) *error = std::string("not JSON: ") + e.what();
        return false;
    }
    if (!doc.is_object() || !doc.contains("language") || !doc["language"].is_string()) {
        if (error) *error = "needs a \"language\": \"code\"";
        return false;
    }
    const std::string code = normalize(doc["language"].get<std::string>());
    if (code.empty()) {
        if (error) *error = "\"language\" is empty";
        return false;
    }
    if (!doc.contains("strings") || !doc["strings"].is_object()) {
        if (error) *error = code + ": needs a \"strings\" object";
        return false;
    }

    // Flatten into a scratch map first: a file with one bad entry changes nothing.
    std::map<std::string, Entry> parsed;
    std::string problem;
    std::function<void(const Json&, const std::string&)> walk = [&](const Json& node, const std::string& prefix) {
        for (auto it = node.begin(); it != node.end() && problem.empty(); ++it) {
            const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
            const Json& v = it.value();
            if (v.is_string()) {
                parsed[key].text = v.get<std::string>();
            } else if (isFormsObject(v)) {
                Entry e;
                for (auto f = v.begin(); f != v.end(); ++f) {
                    if (!f.value().is_string()) {
                        problem = key + "." + f.key() + " is not text";
                        return;
                    }
                    e.forms[f.key()] = f.value().get<std::string>();
                }
                parsed[key] = std::move(e);
            } else if (lacksOther(v)) {
                problem = key + ": plural forms need an \"other\"";
            } else if (v.is_object()) {
                walk(v, key);
            } else {
                problem = key + " is not text";
            }
        }
    };
    walk(doc["strings"], "");
    if (!problem.empty()) {
        if (error) *error = code + ": " + problem;
        return false;
    }

    std::lock_guard lock(m_mutex);
    Table& t = m_tables[code];
    const bool fresh = t.info.code.empty();
    if (fresh) {
        t.info.code = code;
        defaultsForNumbers(t.info);
    }
    if (doc.contains("name") && doc["name"].is_string()) t.info.name = doc["name"].get<std::string>();
    else if (t.info.name.empty()) t.info.name = code;
    if (doc.contains("fallback") && doc["fallback"].is_string()) t.info.fallback = normalize(doc["fallback"].get<std::string>());
    if (doc.contains("rtl") && doc["rtl"].is_boolean()) t.info.rtl = doc["rtl"].get<bool>();
    if (doc.contains("fonts") && doc["fonts"].is_array())
        for (const Json& f : doc["fonts"])
            if (f.is_string() && std::find(t.info.fonts.begin(), t.info.fonts.end(), f.get<std::string>()) == t.info.fonts.end())
                t.info.fonts.push_back(f.get<std::string>());
    if (doc.contains("numbers") && doc["numbers"].is_object()) {
        const Json& nm = doc["numbers"];
        if (nm.contains("decimal") && nm["decimal"].is_string()) t.info.decimal = nm["decimal"].get<std::string>();
        if (nm.contains("group") && nm["group"].is_string()) t.info.group = nm["group"].get<std::string>();
    }
    if (doc.contains("digits") && doc["digits"].is_string()) t.info.digits = doc["digits"].get<std::string>();
    for (auto& [k, e] : parsed) t.entries[k] = std::move(e);
    t.info.strings = t.entries.size();
    return true;
}

bool Localization::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "can't read " + path;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    std::string inner;
    if (!loadJson(text.str(), &inner)) {
        if (error) *error = path + ": " + inner;
        return false;
    }
    return true;
}

int Localization::loadFolder(const std::string& folder, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    int loaded = 0;
    std::string problems;
    for (const fs::path& f : files) {
        std::string one;
        if (loadFile(f.string(), &one)) ++loaded;
        else problems += one + "\n";
    }
    if (error) *error = problems;
    return loaded;
}

void Localization::clear() {
    std::lock_guard lock(m_mutex);
    m_tables.clear();
    m_missing.clear();
}

void Localization::setDefaultLanguage(const std::string& code) {
    std::lock_guard lock(m_mutex);
    m_default = normalize(code);
}

const Localization::Table* Localization::table(const std::string& code) const {
    auto it = m_tables.find(code);
    return it == m_tables.end() ? nullptr : &it->second;
}

bool Localization::setLanguage(const std::string& requested) {
    const std::string code = lower(requested) == "pseudo" ? "pseudo" : normalize(requested);
    bool changed = false;
    {
        std::lock_guard lock(m_mutex);
        std::string chosen;
        if (code == "pseudo") chosen = code;
        else if (m_tables.count(code)) chosen = code;
        else if (m_tables.count(baseCode(code))) chosen = baseCode(code);
        else return false;
        changed = chosen != m_language;
        m_language = chosen;
    }
    if (changed) {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(m_mutex);
            for (auto& [id, cb] : m_listeners) callbacks.push_back(cb);
        }
        for (auto& cb : callbacks) cb();
    }
    return true;
}

bool Localization::rtl() const {
    std::lock_guard lock(m_mutex);
    const Table* t = table(m_language);
    return t && t->info.rtl;
}

std::vector<Localization::Language> Localization::languages() const {
    std::lock_guard lock(m_mutex);
    std::vector<Language> out;
    for (const auto& [code, t] : m_tables) out.push_back(t.info);
    return out;
}

const Localization::Language* Localization::info(const std::string& code) const {
    std::lock_guard lock(m_mutex);
    const Table* t = table(normalize(code));
    return t ? &t->info : nullptr;
}

std::vector<std::string> Localization::systemLanguages() {
    std::vector<std::string> out;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
        for (int i = 0; i < count; ++i) {
            if (!locales[i] || !locales[i]->language) continue;
            std::string code = locales[i]->language;
            if (locales[i]->country && *locales[i]->country) code += std::string("-") + locales[i]->country;
            out.push_back(normalize(code));
        }
        SDL_free(locales);
    }
    if (out.empty()) {
        for (const char* var : { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" }) {
            const char* value = std::getenv(var);
            if (!value || !*value) continue;
            std::stringstream list(value);
            for (std::string part; std::getline(list, part, ':');) {
                part = part.substr(0, part.find_first_of(".@"));
                if (part.empty() || part == "C" || part == "POSIX") continue;
                out.push_back(normalize(part));
            }
            if (!out.empty()) break;
        }
    }
    return out;
}

std::string Localization::pick(const std::vector<std::string>& preferred, const std::vector<std::string>& available, const std::string& fallback) {
    std::set<std::string> have;
    for (const std::string& a : available) have.insert(normalize(a));
    for (const std::string& p : preferred) {
        const std::string code = normalize(p);
        if (have.count(code)) return code;
        if (have.count(baseCode(code))) return baseCode(code);
    }
    return fallback;
}

bool Localization::useSystemLanguage() {
    std::vector<std::string> available;
    for (const Language& l : languages()) available.push_back(l.code);
    return setLanguage(pick(systemLanguages(), available, m_default));
}

std::vector<const Localization::Table*> Localization::chain() const {
    std::vector<const Table*> out;
    auto add = [&](const std::string& code) {
        const Table* t = table(code);
        if (t && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    };
    const std::string start = m_language == "pseudo" ? m_default : m_language;
    add(start);
    if (const Table* t = table(start)) {
        if (!t->info.fallback.empty()) add(t->info.fallback);
    }
    add(baseCode(start));
    add(m_default);
    return out;
}

bool Localization::has(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    for (const Table* t : chain())
        if (t->entries.count(key)) return true;
    return false;
}

std::string Localization::fill(const std::string& text, const Args& args, const Table*) const {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') { out += '{'; ++i; continue; }
        if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') { out += '}'; ++i; continue; }
        if (c == '{') {
            const size_t end = text.find('}', i);
            if (end != std::string::npos) {
                const std::string name = text.substr(i + 1, end - i - 1);
                bool found = false;
                for (const auto& [k, v] : args)
                    if (k == name) {
                        out += v;
                        found = true;
                        break;
                    }
                if (!found) out += text.substr(i, end - i + 1); // left as it is: a missing argument shows
                i = end;
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::string Localization::formatNumber(double n, int decimals, const Table* t) const {
    const Language fallback;
    const Language& l = t ? t->info : fallback;
    char buf[64];
    if (decimals < 0) {
        std::snprintf(buf, sizeof(buf), "%.3f", std::fabs(n));
        std::string s = buf;
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        std::snprintf(buf, sizeof(buf), "%s", s.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%.*f", std::min(decimals, 12), std::fabs(n));
    }
    std::string s = buf;
    const size_t dot = s.find('.');
    std::string whole = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot + 1);
    std::string grouped;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped += l.group;
        grouped += whole[i];
    }
    std::string out = grouped;
    if (!frac.empty()) out += l.decimal + frac;
    if (n < 0 && out.find_first_not_of("0.,") != std::string::npos) out = "-" + out;
    return digitsAs(out, l.digits);
}

std::string Localization::number(double n, int decimals) const {
    std::lock_guard lock(m_mutex);
    const auto tables = chain();
    return formatNumber(n, decimals, tables.empty() ? nullptr : tables.front());
}

std::string Localization::tr(const std::string& key, const Args& args) const {
    std::lock_guard lock(m_mutex);
    const auto tables = chain();
    const Entry* e = nullptr;
    const Table* from = nullptr;
    for (const Table* t : tables) {
        auto it = t->entries.find(key);
        if (it != t->entries.end()) {
            e = &it->second;
            from = t;
            break;
        }
    }
    if (!e) {
        m_missing.insert(key);
        return key;
    }
    std::string text = e->forms.empty() ? e->text : (e->forms.count("other") ? e->forms.at("other") : e->text);
    if (m_language == "pseudo") text = pseudo(text);
    return fill(text, args, from);
}

std::string Localization::plural(const std::string& key, double n, const Args& args) const {
    std::lock_guard lock(m_mutex);
    const auto tables = chain();
    const Entry* e = nullptr;
    const Table* from = nullptr;
    for (const Table* t : tables) {
        auto it = t->entries.find(key);
        if (it != t->entries.end()) {
            e = &it->second;
            from = t;
            break;
        }
    }
    if (!e) {
        m_missing.insert(key);
        return key;
    }
    std::string text = e->text;
    if (!e->forms.empty()) {
        const std::string category = categoryOf(from->info.code, n);
        auto it = e->forms.find(category);
        if (it == e->forms.end()) it = e->forms.find("other");
        text = it->second;
    }
    if (m_language == "pseudo") text = pseudo(text);
    Args all = args;
    all.emplace_back("n", formatNumber(n, -1, from));
    return fill(text, all, from);
}

std::string Localization::select(const std::string& key, const std::string& choice, const Args& args) const {
    std::lock_guard lock(m_mutex);
    const auto tables = chain();
    for (const Table* t : tables) {
        auto it = t->entries.find(key);
        if (it == t->entries.end()) continue;
        const Entry& e = it->second;
        std::string text = e.text;
        if (!e.forms.empty()) {
            auto f = e.forms.find(choice);
            if (f == e.forms.end()) f = e.forms.find("other");
            text = f->second;
        }
        if (m_language == "pseudo") text = pseudo(text);
        return fill(text, args, t);
    }
    m_missing.insert(key);
    return key;
}

std::string Localization::pluralCategory(const std::string& language, double n) {
    return categoryOf(normalize(language), n);
}

std::string Localization::display(std::string_view logical) const {
    return bidi::display(logical, rtl() ? bidi::Direction::RightToLeft : bidi::Direction::Auto);
}

std::string Localization::pseudo(std::string_view text) {
    static const std::map<char, const char*> accents = {
        { 'a', "\xC3\xA0" }, { 'e', "\xC3\xA9" }, { 'i', "\xC3\xAE" }, { 'o', "\xC3\xB6" }, { 'u', "\xC3\xBC" }, { 'c', "\xC3\xA7" },
        { 'n', "\xC3\xB1" }, { 'y', "\xC3\xBD" }, { 's', "\xC5\xA1" }, { 'A', "\xC3\x85" }, { 'E', "\xC3\x89" }, { 'I', "\xC3\x8C" },
        { 'O', "\xC3\x96" }, { 'U', "\xC3\x9B" }, { 'C', "\xC3\x87" }, { 'N', "\xC3\x91" }, { 'Y', "\xC3\x9D" }, { 'S', "\xC5\xA0" },
    };
    std::string out = "[";
    bool inBrace = false;
    size_t letters = 0;
    for (char c : text) {
        if (c == '{') inBrace = true;
        if (!inBrace) {
            auto it = accents.find(c);
            if (it != accents.end()) out += it->second;
            else out += c;
            if (std::isalpha(static_cast<unsigned char>(c))) ++letters;
        } else {
            out += c;
        }
        if (c == '}') inBrace = false;
    }
    out += std::string(letters / 3 + 1, '~');
    out += "]";
    return out;
}

std::vector<std::string> Localization::missingKeys() const {
    std::lock_guard lock(m_mutex);
    return std::vector<std::string>(m_missing.begin(), m_missing.end());
}

void Localization::forgetMissing() {
    std::lock_guard lock(m_mutex);
    m_missing.clear();
}

std::vector<std::string> Localization::untranslated(const std::string& code) const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out;
    const Table* base = table(m_default);
    const Table* t = table(normalize(code));
    if (!base || !t) return out;
    for (const auto& [key, e] : base->entries)
        if (!t->entries.count(key)) out.push_back(key);
    return out;
}

std::vector<std::string> Localization::placeholderProblems(const std::string& code) const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out;
    const Table* base = table(m_default);
    const Table* t = table(normalize(code));
    if (!base || !t) return out;
    auto textsOf = [](const Entry& e) {
        std::vector<std::string> v;
        if (e.forms.empty()) v.push_back(e.text);
        for (const auto& [k, s] : e.forms) v.push_back(s);
        return v;
    };
    for (const auto& [key, e] : t->entries) {
        auto b = base->entries.find(key);
        if (b == base->entries.end()) continue;
        std::set<std::string> want, got;
        for (const std::string& s : textsOf(b->second))
            for (const std::string& p : placeholdersOf(s)) want.insert(p);
        for (const std::string& s : textsOf(e))
            for (const std::string& p : placeholdersOf(s)) got.insert(p);
        // The count a plural form states may be spelt out ("one coin") instead of {n}.
        want.erase("n");
        got.erase("n");
        for (const std::string& p : want)
            if (!got.count(p)) out.push_back(key + ": lost {" + p + "}");
        for (const std::string& p : got)
            if (!want.count(p)) out.push_back(key + ": invented {" + p + "}");
    }
    return out;
}

int Localization::onChange(std::function<void()> callback) {
    std::lock_guard lock(m_mutex);
    const int id = m_nextListener++;
    m_listeners[id] = std::move(callback);
    return id;
}

void Localization::removeOnChange(int id) {
    std::lock_guard lock(m_mutex);
    m_listeners.erase(id);
}

} // namespace kke
