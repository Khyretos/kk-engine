#include "kke/Localization.h"

#include "kke/DataFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <system_error>

namespace kke {

namespace {

// ---- Plural rules (CLDR, cardinals) --------------------------------------

struct Operands {
    double n;      // absolute value
    uint64_t i;    // integer digits
    bool fraction; // visible fraction digits present (v != 0)
};

Operands operandsOf(double count) {
    const double n = std::fabs(count);
    const double whole = std::floor(n);
    return { n, whole >= 1.8e19 ? UINT64_MAX : static_cast<uint64_t>(whole), n != whole };
}

bool in(uint64_t x, uint64_t lo, uint64_t hi) { return x >= lo && x <= hi; }

PluralCategory slavicLike(const Operands& o, bool polish) {
    const uint64_t m10 = o.i % 10, m100 = o.i % 100;
    if (o.fraction) return PluralCategory::Other;
    if (!polish) {
        if (m10 == 1 && m100 != 11) return PluralCategory::One;
        if (in(m10, 2, 4) && !in(m100, 12, 14)) return PluralCategory::Few;
        return PluralCategory::Many;
    }
    if (o.i == 1) return PluralCategory::One;
    if (in(m10, 2, 4) && !in(m100, 12, 14)) return PluralCategory::Few;
    return PluralCategory::Many;
}

} // namespace

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

PluralCategory pluralCategory(const std::string& language, double count) {
    std::string code = language;
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string base = code.substr(0, code.find_first_of("-_"));
    const Operands o = operandsOf(count);
    using P = PluralCategory;

    static const std::set<std::string> none = { "ja", "zh", "ko", "vi", "th", "id", "ms", "my", "lo", "km", "jv", "yue", "su" };
    if (none.count(base)) return P::Other;

    if (base == "fr" || base == "hy" || base == "kab") return (o.i == 0 || o.i == 1) ? P::One : P::Other;
    if (base == "pt") {
        if (code == "pt-pt") return (o.i == 1 && !o.fraction) ? P::One : P::Other;
        return (o.i == 0 || o.i == 1) ? P::One : P::Other;
    }
    if (base == "hi" || base == "bn" || base == "gu" || base == "kn" || base == "fa" || base == "zu" || base == "am" || base == "as")
        return (o.i == 0 || o.n == 1) ? P::One : P::Other;
    if (base == "ru" || base == "uk" || base == "be") return slavicLike(o, false);
    if (base == "pl") return slavicLike(o, true);
    if (base == "hr" || base == "sr" || base == "bs") {
        if (o.fraction) return P::Other;
        const uint64_t m10 = o.i % 10, m100 = o.i % 100;
        if (m10 == 1 && m100 != 11) return P::One;
        if (in(m10, 2, 4) && !in(m100, 12, 14)) return P::Few;
        return P::Other;
    }
    if (base == "cs" || base == "sk") {
        if (o.fraction) return P::Many;
        if (o.i == 1) return P::One;
        return in(o.i, 2, 4) ? P::Few : P::Other;
    }
    if (base == "ar") {
        if (o.fraction) return P::Other;
        const uint64_t m100 = o.i % 100;
        if (o.i == 0) return P::Zero;
        if (o.i == 1) return P::One;
        if (o.i == 2) return P::Two;
        if (in(m100, 3, 10)) return P::Few;
        if (in(m100, 11, 99)) return P::Many;
        return P::Other;
    }
    if (base == "he" || base == "iw") {
        if (o.fraction) return o.i == 0 ? P::One : P::Other;
        if (o.i == 1) return P::One;
        return o.i == 2 ? P::Two : P::Other;
    }
    if (base == "lt") {
        if (o.fraction) return P::Many;
        const uint64_t m10 = o.i % 10, m100 = o.i % 100;
        if (m10 == 1 && !in(m100, 11, 19)) return P::One;
        if (in(m10, 2, 9) && !in(m100, 11, 19)) return P::Few;
        return P::Other;
    }
    if (base == "lv") {
        const uint64_t m10 = o.i % 10, m100 = o.i % 100;
        if (!o.fraction && (m10 == 0 || in(m100, 11, 19))) return P::Zero;
        if (!o.fraction && m10 == 1 && m100 != 11) return P::One;
        return P::Other;
    }
    if (base == "ro") {
        if (o.i == 1 && !o.fraction) return P::One;
        if (o.fraction || o.n == 0 || in(o.i % 100, 1, 19)) return P::Few;
        return P::Other;
    }
    if (base == "sl") {
        const uint64_t m100 = o.i % 100;
        if (!o.fraction && m100 == 1) return P::One;
        if (!o.fraction && m100 == 2) return P::Two;
        if (o.fraction || in(m100, 3, 4)) return P::Few;
        return P::Other;
    }
    if (base == "ga") {
        if (o.n == 1) return P::One;
        if (o.n == 2) return P::Two;
        if (o.n >= 3 && o.n <= 6 && !o.fraction) return P::Few;
        if (o.n >= 7 && o.n <= 10 && !o.fraction) return P::Many;
        return P::Other;
    }
    if (base == "cy") {
        if (o.fraction) return P::Other;
        switch (o.i) {
        case 0: return P::Zero;
        case 1: return P::One;
        case 2: return P::Two;
        case 3: return P::Few;
        case 6: return P::Many;
        default: return P::Other;
        }
    }
    if (base == "is") return ((o.i % 10 == 1 && o.i % 100 != 11) || o.fraction) ? P::One : P::Other;
    if (base == "da") return (o.n == 1 || (o.fraction && o.i <= 1)) ? P::One : P::Other;
    // English, German, Dutch, Swedish, Italian, Spanish, Turkish, Finnish, Greek...
    return (o.i == 1 && !o.fraction) ? P::One : P::Other;
}

// ---- UTF-8 ----------------------------------------------------------------

std::u32string Localization::decode(const std::string& s) {
    // Invalid bytes become U+FFFD, one for each maximal bad stretch (a cut-off sequence is one), the
    // way the Unicode standard recommends; overlong forms, surrogates and values past U+10FFFF are bad.
    std::u32string out;
    out.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            ++i;
            continue;
        }
        size_t need = 0;
        char32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
            cp = c & 0x1F;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            cp = c & 0x0F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            cp = c & 0x07;
        } else { // a stray continuation byte, 0xC0, 0xC1, 0xF5..0xFF
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        size_t used = 1;
        while (used <= need && i + used < n && (static_cast<unsigned char>(s[i + used]) & 0xC0) == 0x80) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + used]) & 0x3F);
            ++used;
        }
        const bool complete = used == need + 1;
        const bool overlong = (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000);
        const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
        out.push_back(complete && !overlong && !surrogate && cp <= 0x10FFFF ? cp : 0xFFFD);
        i += used;
    }
    return out;
}

std::string Localization::encode(const std::u32string& text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// ---- Arabic shaping and bidi ---------------------------------------------

namespace {

struct ArabicForms {
    char32_t base, isolated, final, initial, medial;
};
struct ArabicLigature {
    char32_t alef, isolated, final;
};

#include "ArabicTable.inc"

const ArabicForms* formsOf(char32_t c) {
    const ArabicForms* end = kArabicForms + sizeof(kArabicForms) / sizeof(kArabicForms[0]);
    const ArabicForms* it = std::lower_bound(kArabicForms, end, c, [](const ArabicForms& f, char32_t x) { return f.base < x; });
    return (it != end && it->base == c) ? it : nullptr;
}

bool transparent(char32_t c) {
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
           c == 0x06E7 || c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED) || c == 0x200C;
}
constexpr char32_t kTatweel = 0x0640;
// Joins to the letter after it (dual-joining).
bool joinsNext(char32_t c) {
    if (c == kTatweel) return true;
    const ArabicForms* f = formsOf(c);
    return f && f->initial != 0;
}
// Joins to the letter before it.
bool joinsPrev(char32_t c) {
    if (c == kTatweel) return true;
    const ArabicForms* f = formsOf(c);
    return f && f->final != 0;
}
const ArabicLigature* lamAlef(char32_t alef) {
    for (const ArabicLigature& l : kLamAlef)
        if (l.alef == alef) return &l;
    return nullptr;
}

std::u32string shape(const std::u32string& in) {
    std::u32string out;
    out.reserve(in.size());
    auto prevLetter = [&](size_t i) -> char32_t {
        while (i > 0) {
            --i;
            if (!transparent(in[i])) return in[i];
        }
        return 0;
    };
    auto nextLetter = [&](size_t i, size_t* at = nullptr) -> char32_t {
        while (++i < in.size())
            if (!transparent(in[i])) {
                if (at) *at = i;
                return in[i];
            }
        return 0;
    };
    for (size_t i = 0; i < in.size(); ++i) {
        const char32_t c = in[i];
        if (transparent(c)) {
            out.push_back(c);
            continue;
        }
        const ArabicForms* f = formsOf(c);
        if (!f) {
            out.push_back(c);
            continue;
        }
        const char32_t before = prevLetter(i);
        const bool afterJoin = before != 0 && joinsNext(before);
        if (c == 0x0644) { // lam + alef is one ligature
            size_t at = 0;
            const char32_t after = nextLetter(i, &at);
            if (const ArabicLigature* lig = lamAlef(after); lig && at == i + 1) {
                out.push_back(afterJoin ? lig->final : lig->isolated);
                ++i;
                continue;
            }
        }
        const char32_t after = nextLetter(i);
        const bool beforeJoin = after != 0 && joinsPrev(after) && f->initial != 0;
        char32_t form = f->isolated;
        if (f->initial != 0) { // dual-joining
            if (afterJoin && beforeJoin) form = f->medial;
            else if (afterJoin) form = f->final;
            else if (beforeJoin) form = f->initial;
        } else if (afterJoin && f->final != 0) { // right-joining
            form = f->final;
        }
        out.push_back(form ? form : c);
    }
    return out;
}

enum class Bidi : uint8_t { L, R, EN, AN, CS, ES, ET, N };

bool isRtlLetter(char32_t c) {
    return (c >= 0x0590 && c <= 0x05FF) || (c >= 0x0600 && c <= 0x07BF && !(c >= 0x0660 && c <= 0x0669) && !(c >= 0x06F0 && c <= 0x06F9)) ||
           (c >= 0x0860 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFB4F) || (c >= 0xFB50 && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFF) || c == 0x200F;
}

bool isLtrLetter(char32_t c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c == 0x200E) return true;
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
    return (c >= 0x370 && c <= 0x58F) || (c >= 0x900 && c <= 0x1FFF && !isRtlLetter(c)) || (c >= 0x3040 && c <= 0xD7AF) ||
           (c >= 0xF900 && c <= 0xFAFF);
}

Bidi classify(char32_t c) {
    if (isRtlLetter(c)) return Bidi::R;
    if ((c >= '0' && c <= '9') || (c >= 0x06F0 && c <= 0x06F9)) return Bidi::EN;
    if (c >= 0x0660 && c <= 0x0669) return Bidi::AN;
    if (isLtrLetter(c)) return Bidi::L;
    switch (c) {
    case ',': case '.': case ':': case '/': case 0x00A0: case 0x202F: case 0x060C: return Bidi::CS;
    case '+': case '-': return Bidi::ES;
    case '#': case '$': case '%': case 0x00A2: case 0x00A3: case 0x00A4: case 0x00A5: case 0x00B0: case 0x00B1: case 0x20AC: return Bidi::ET;
    default: return Bidi::N;
    }
}

char32_t mirrored(char32_t c) {
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    default: return c;
    }
}

std::u32string orderLine(const std::u32string& text, bool rtlBase) {
    const size_t n = text.size();
    std::vector<Bidi> type(n);
    for (size_t i = 0; i < n; ++i) type[i] = classify(text[i]);
    // W4: one separator between two numbers is part of the number; W5: terminators next to one.
    for (size_t i = 1; i + 1 < n; ++i)
        if ((type[i] == Bidi::CS || type[i] == Bidi::ES) && type[i - 1] == Bidi::EN && type[i + 1] == Bidi::EN) type[i] = Bidi::EN;
    for (size_t i = 0; i < n; ++i)
        if (type[i] == Bidi::ET) {
            size_t a = i, b = i;
            while (b < n && type[b] == Bidi::ET) ++b;
            const bool touches = (a > 0 && type[a - 1] == Bidi::EN) || (b < n && type[b] == Bidi::EN);
            for (size_t k = a; k < b; ++k) type[k] = touches ? Bidi::EN : Bidi::N;
            i = b - 1;
        }
    for (Bidi& t : type)
        if (t == Bidi::CS || t == Bidi::ES || t == Bidi::ET) t = Bidi::N;
    // W7: a number after a left-to-right letter is left to right.
    Bidi lastStrong = rtlBase ? Bidi::R : Bidi::L;
    for (size_t i = 0; i < n; ++i) {
        if (type[i] == Bidi::L || type[i] == Bidi::R) lastStrong = type[i];
        else if (type[i] == Bidi::EN && lastStrong == Bidi::L) type[i] = Bidi::L;
    }
    // N1/N2: neutrals between letters of one direction take it; otherwise the paragraph's.
    const Bidi para = rtlBase ? Bidi::R : Bidi::L;
    auto strongOf = [&](Bidi t) { return t == Bidi::L ? Bidi::L : Bidi::R; }; // numbers count as R
    for (size_t i = 0; i < n; ++i)
        if (type[i] == Bidi::N) {
            size_t b = i;
            while (b < n && type[b] == Bidi::N) ++b;
            const Bidi left = i == 0 ? para : strongOf(type[i - 1]);
            const Bidi right = b == n ? para : strongOf(type[b]);
            for (size_t k = i; k < b; ++k) type[k] = left == right ? left : para;
            i = b - 1;
        }
    // I1/I2 levels.
    std::vector<int> level(n);
    const int base = rtlBase ? 1 : 0;
    int top = base, bottomOdd = 99;
    for (size_t i = 0; i < n; ++i) {
        int l = base;
        switch (type[i]) {
        case Bidi::L: l = base % 2 == 0 ? base : base + 1; break;
        case Bidi::R: l = base % 2 == 0 ? base + 1 : base; break;
        default: l = base % 2 == 0 ? base + 2 : base + 1; break; // EN, AN
        }
        level[i] = l;
        top = std::max(top, l);
        if (l % 2 == 1) bottomOdd = std::min(bottomOdd, l);
    }
    std::u32string out = text;
    std::vector<int> lv = level;
    for (int l = top; l >= bottomOdd && l >= 1; --l)
        for (size_t i = 0; i < n;) {
            if (lv[i] < l) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < n && lv[j] >= l) ++j;
            std::reverse(out.begin() + static_cast<std::ptrdiff_t>(i), out.begin() + static_cast<std::ptrdiff_t>(j));
            std::reverse(lv.begin() + static_cast<std::ptrdiff_t>(i), lv.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    // Brackets in right-to-left runs are mirrored (lv is now in visual order).
    for (size_t i = 0; i < n; ++i)
        if (lv[i] % 2 == 1) out[i] = mirrored(out[i]);
    return out;
}

} // namespace

bool Localization::hasRtlLetters(const std::string& utf8) {
    for (char32_t c : decode(utf8))
        if (isRtlLetter(c) || (c >= 0x0660 && c <= 0x0669)) return true;
    return false;
}

std::string Localization::shapeArabic(const std::string& utf8) { return encode(shape(decode(utf8))); }

std::string Localization::visualOrder(const std::string& utf8, bool rtlBase) {
    if (!hasRtlLetters(utf8)) return utf8;
    const std::u32string shaped = shape(decode(utf8));
    std::u32string out;
    size_t start = 0;
    for (size_t i = 0; i <= shaped.size(); ++i)
        if (i == shaped.size() || shaped[i] == U'\n') {
            out += orderLine(shaped.substr(start, i - start), rtlBase);
            if (i < shaped.size()) out.push_back(U'\n');
            start = i + 1;
        }
    return encode(out);
}

// ---- The tables -------------------------------------------------------------

namespace {

const std::set<std::string>& rtlLanguages() {
    static const std::set<std::string> s = { "ar", "he", "iw", "fa", "ur", "ps", "yi", "dv", "sd", "ug", "ckb", "azb" };
    return s;
}

bool isPluralObject(const nlohmann::json& j) {
    if (!j.is_object() || j.empty()) return false;
    static const std::set<std::string> names = { "zero", "one", "two", "few", "many", "other" };
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!names.count(it.key()) || !it.value().is_string()) return false;
    return true;
}

PluralCategory categoryOf(const std::string& name) {
    if (name == "zero") return PluralCategory::Zero;
    if (name == "one") return PluralCategory::One;
    if (name == "two") return PluralCategory::Two;
    if (name == "few") return PluralCategory::Few;
    if (name == "many") return PluralCategory::Many;
    return PluralCategory::Other;
}

} // namespace

Localization::Localization() = default;

std::string Localization::canonical(const std::string& code) {
    std::string out;
    size_t part = 0;
    // Split on - and _ by hand (getline takes one delimiter); ".UTF-8" and "@euro" are not part of the language.
    std::vector<std::string> pieces;
    std::string cur;
    for (char c : code.substr(0, code.find_first_of(".@"))) {
        if (c == '-' || c == '_') {
            pieces.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    pieces.push_back(cur);
    for (std::string& p : pieces) {
        if (p.empty()) continue;
        for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (part > 0 && p.size() == 2) {
            for (char& c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else if (part > 0 && p.size() == 4) {
            p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
        }
        if (!out.empty()) out.push_back('-');
        out += p;
        ++part;
    }
    return out;
}

std::string Localization::baseOf(const std::string& code) { return code.substr(0, code.find('-')); }

void Localization::changed() {
    ++m_generation;
    const auto listeners = m_listeners; // a callback may add or remove one
    for (const auto& [id, fn] : listeners) fn();
}

int Localization::addListener(std::function<void()> callback) {
    m_listeners.emplace_back(m_nextListener, std::move(callback));
    return m_nextListener++;
}

void Localization::removeListener(int id) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [id](const auto& l) { return l.first == id; }),
                      m_listeners.end());
}

namespace {

void flatten(const std::string& prefix, const nlohmann::json& j, std::unordered_map<std::string, std::string>& texts,
             std::map<std::string, std::map<PluralCategory, std::string>>& plurals) {
    if (isPluralObject(j)) {
        for (auto it = j.begin(); it != j.end(); ++it) plurals[prefix][categoryOf(it.key())] = it.value().get<std::string>();
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) flatten(prefix.empty() ? it.key() : prefix + "." + it.key(), it.value(), texts, plurals);
    } else if (j.is_string()) {
        texts[prefix] = j.get<std::string>();
    } else if (j.is_array()) { // lines of one text
        std::string joined;
        for (const auto& line : j) {
            if (!joined.empty()) joined.push_back('\n');
            joined += line.is_string() ? line.get<std::string>() : line.dump();
        }
        texts[prefix] = joined;
    } else if (j.is_number() || j.is_boolean()) {
        texts[prefix] = j.dump();
    }
}

} // namespace

void Localization::addStrings(const std::string& languageCode, const nlohmann::json& strings, const std::string& name) {
    const std::string code = canonical(languageCode);
    if (code.empty()) return;
    Language& lang = m_languages[code];
    if (lang.info.code.empty()) {
        lang.info.code = code;
        lang.info.rtl = rtlLanguages().count(baseOf(code)) > 0;
    }
    if (!name.empty()) lang.info.name = name;
    std::unordered_map<std::string, std::string> texts;
    std::map<std::string, std::map<PluralCategory, std::string>> plurals;
    flatten("", strings, texts, plurals);
    for (auto& [key, text] : texts) {
        Entry& e = lang.entries[key];
        e.text = std::move(text);
        e.plural.clear();
    }
    for (auto& [key, forms] : plurals) {
        Entry& e = lang.entries[key];
        e.text.clear();
        e.plural = std::move(forms);
    }
    lang.info.strings = lang.entries.size();
    changed();
}

bool Localization::loadFile(const std::filesystem::path& file, std::string* error) {
    nlohmann::json doc;
    std::string err;
    if (!datafile::loadFile(file, doc, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!doc.is_object() || !doc.contains("language") || !doc["language"].is_string()) {
        if (error) *error = file.string() + ": a language file needs \"language\": \"nl\"";
        return false;
    }
    const std::string code = canonical(doc["language"].get<std::string>());
    addStrings(code, doc.contains("strings") ? doc["strings"] : nlohmann::json::object(),
               doc.contains("name") && doc["name"].is_string() ? doc["name"].get<std::string>() : std::string());
    Language& lang = m_languages[code];
    if (doc.contains("rtl") && doc["rtl"].is_boolean()) lang.info.rtl = doc["rtl"].get<bool>();
    if (doc.contains("font") && doc["font"].is_string()) lang.info.font = doc["font"].get<std::string>();
    return true;
}

int Localization::loadFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return 0;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.is_regular_file() && datafile::formatOf(entry.path())) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    int loaded = 0;
    for (const auto& f : files)
        if (loadFile(f)) ++loaded;
    return loaded;
}

void Localization::setDefaultLanguage(const std::string& code) { m_default = canonical(code); }

bool Localization::setLanguage(const std::string& code) {
    const std::string want = canonical(code);
    std::string use;
    if (m_languages.count(want)) use = want;
    else if (m_languages.count(baseOf(want))) use = baseOf(want);
    else return false;
    const bool same = use == m_language;
    m_language = use;
    if (!same) changed();
    return true;
}

std::vector<LanguageInfo> Localization::languages() const {
    std::vector<LanguageInfo> out;
    if (auto it = m_languages.find(m_default); it != m_languages.end()) out.push_back(it->second.info);
    for (const auto& [code, lang] : m_languages)
        if (code != m_default) out.push_back(lang.info);
    for (LanguageInfo& l : out)
        if (l.name.empty()) l.name = l.code;
    return out;
}

const LanguageInfo* Localization::info(const std::string& code) const {
    auto it = m_languages.find(canonical(code));
    return it == m_languages.end() ? nullptr : &it->second.info;
}

bool Localization::rtl() const {
    auto it = m_languages.find(m_language);
    return it != m_languages.end() ? it->second.info.rtl : rtlLanguages().count(baseOf(m_language)) > 0;
}

std::string Localization::bestMatch(const std::string& locale) const {
    const std::string want = canonical(locale);
    if (want.empty() || want == "C" || want == "POSIX") return {};
    if (m_languages.count(want)) return want;
    const std::string base = baseOf(want);
    if (m_languages.count(base)) return base;
    for (const auto& [code, lang] : m_languages) // "pt" asked, "pt-BR" loaded
        if (baseOf(code) == base) return code;
    return {};
}

std::string Localization::systemLanguage() const {
    if (const char* list = std::getenv("LANGUAGE")) { // "nl:en"
        std::stringstream ss(list);
        for (std::string item; std::getline(ss, item, ':');)
            if (const std::string hit = bestMatch(item); !hit.empty()) return hit;
    }
    for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" })
        if (const char* v = std::getenv(var); v && *v)
            if (const std::string hit = bestMatch(v); !hit.empty()) return hit;
    return {};
}

std::vector<const Localization::Language*> Localization::chain() const {
    std::vector<const Language*> out;
    auto add = [&](const std::string& code) {
        auto it = m_languages.find(code);
        if (it != m_languages.end() && std::find(out.begin(), out.end(), &it->second) == out.end()) out.push_back(&it->second);
    };
    add(m_language);
    add(baseOf(m_language));
    add(m_default);
    add(baseOf(m_default));
    return out;
}

const Localization::Entry* Localization::find(const std::string& key) const {
    for (const Language* lang : chain())
        if (auto it = lang->entries.find(key); it != lang->entries.end()) return &it->second;
    return nullptr;
}

bool Localization::has(const std::string& key) const { return find(key) != nullptr; }

std::string Localization::fill(const std::string& text, const LocArgs& args) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') {
            out.push_back('{');
            ++i;
        } else if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') {
            out.push_back('}');
            ++i;
        } else if (c == '{') {
            const size_t close = text.find('}', i);
            if (close == std::string::npos) {
                out.push_back(c);
                continue;
            }
            const std::string name = text.substr(i + 1, close - i - 1);
            const auto arg = std::find_if(args.begin(), args.end(), [&](const auto& a) { return a.first == name; });
            if (arg != args.end()) {
                out += arg->second;
            } else {
                out += text.substr(i, close - i + 1); // an unknown placeholder stays visible
            }
            i = close;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string Localization::tr(const std::string& key, const LocArgs& args) const {
    const Entry* e = find(key);
    if (!e) {
        if (std::find(m_missing.begin(), m_missing.end(), key) == m_missing.end()) m_missing.push_back(key);
        return key;
    }
    if (!e->plural.empty() && e->text.empty()) {
        auto other = e->plural.find(PluralCategory::Other);
        return fill(other != e->plural.end() ? other->second : e->plural.begin()->second, args);
    }
    return fill(e->text, args);
}

std::string Localization::trn(const std::string& key, double count, const LocArgs& args) const {
    const Language* owner = nullptr;
    const Entry* e = nullptr;
    for (const Language* lang : chain())
        if (auto it = lang->entries.find(key); it != lang->entries.end()) {
            owner = lang;
            e = &it->second;
            break;
        }
    if (!e) {
        if (std::find(m_missing.begin(), m_missing.end(), key) == m_missing.end()) m_missing.push_back(key);
        return key;
    }
    LocArgs all = args;
    if (std::none_of(all.begin(), all.end(), [](const auto& a) { return a.first == "count"; })) all.emplace_back("count", formatNumber(count));
    if (e->plural.empty()) return fill(e->text, all);
    const PluralCategory want = pluralCategory(owner->info.code, count);
    auto it = e->plural.find(want);
    if (it == e->plural.end()) it = e->plural.find(PluralCategory::Other);
    if (it == e->plural.end()) it = e->plural.begin();
    return fill(it->second, all);
}

std::string Localization::formatNumber(double value, int decimals) const {
    std::string group = ",", point = ".";
    int minGroup = 1; // digits before the point needed for the first group mark
    const std::string base = baseOf(m_language);
    static const std::set<std::string> dotComma = { "de", "nl", "it", "tr", "id", "da", "el", "hr", "sl", "ro", "vi", "ca", "gl", "eu", "is", "es", "sr" };
    static const std::set<std::string> spaceComma = { "ru", "uk", "pl", "cs", "sk", "sv", "fi", "nb", "no", "bg", "hu", "lt", "lv", "et", "be", "kk" };
    if (base == "fr") {
        group = "\xE2\x80\xAF"; // narrow no-break space
        point = ",";
    } else if (base == "pt") { // Brazil writes 1.234,5; Portugal 1 234,5 (and no mark in 1234)
        point = ",";
        if (m_language == "pt-PT") {
            group = "\xC2\xA0";
            minGroup = 2;
        } else {
            group = ".";
        }
    } else if (spaceComma.count(base)) {
        group = "\xC2\xA0";
        point = ",";
        if (base == "pl") minGroup = 2;
    } else if (dotComma.count(base)) {
        group = ".";
        point = ",";
        if (base == "es") minGroup = 2;
    }

    char buf[64];
    if (decimals < 0) {
        std::snprintf(buf, sizeof buf, "%.6f", std::fabs(value));
        std::string s = buf;
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        std::snprintf(buf, sizeof buf, "%s", s.c_str());
    } else {
        std::snprintf(buf, sizeof buf, "%.*f", std::min(decimals, 12), std::fabs(value));
    }
    std::string number = buf;
    const size_t dot = number.find('.');
    std::string whole = number.substr(0, dot), fraction = dot == std::string::npos ? "" : number.substr(dot + 1);
    std::string grouped;
    const bool useGroup = whole.size() >= 3 + static_cast<size_t>(minGroup);
    for (size_t i = 0; i < whole.size(); ++i) {
        if (useGroup && i > 0 && (whole.size() - i) % 3 == 0) grouped += group;
        grouped.push_back(whole[i]);
    }
    const bool negative = value < 0 && number.find_first_not_of("0.") != std::string::npos;
    return std::string(negative ? "-" : "") + grouped + (fraction.empty() ? "" : point + fraction);
}

} // namespace kke
