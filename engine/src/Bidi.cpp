#include "kke/Bidi.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kke::bidi {

std::u32string toUtf32(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        int len = 1;
        char32_t c = b;
        if (b >= 0xF0 && b < 0xF8) {
            len = 4;
            c = b & 0x07;
        } else if (b >= 0xE0) {
            len = 3;
            c = b & 0x0F;
        } else if (b >= 0xC0) {
            len = 2;
            c = b & 0x1F;
        } else if (b >= 0x80) {
            out.push_back(0xFFFD); // a continuation byte with no start
            ++i;
            continue;
        }
        if (i + static_cast<size_t>(len) > s.size()) {
            out.push_back(0xFFFD);
            break;
        }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const auto cb = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cb & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (cb & 0x3F);
        }
        out.push_back(ok ? c : static_cast<char32_t>(0xFFFD));
        i += ok ? static_cast<size_t>(len) : 1;
    }
    return out;
}

std::string toUtf8(const std::u32string& t) {
    std::string out;
    out.reserve(t.size());
    for (char32_t c : t) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

namespace {
bool in(char32_t c, char32_t lo, char32_t hi) { return c >= lo && c <= hi; }
} // namespace

Type typeOf(char32_t c) {
    if (c < 0x80) {
        if (c == '\n' || c == '\r') return Type::B;
        if (c == '\t') return Type::S;
        if (c == ' ') return Type::WS;
        if (c >= '0' && c <= '9') return Type::EN;
        if (c == '+' || c == '-') return Type::ES;
        if (c == '#' || c == '$' || c == '%') return Type::ET;
        if (c == ',' || c == '.' || c == '/' || c == ':') return Type::CS;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return Type::L;
        if (c < 0x20 || c == 0x7F) return Type::BN;
        return Type::ON;
    }
    // Controls and invisible formatting: dropped.
    if (in(c, 0x200B, 0x200D) || in(c, 0x202A, 0x202E) || in(c, 0x2060, 0x2064) || in(c, 0x2066, 0x2069) || c == 0x00AD || c == 0xFEFF ||
        in(c, 0x80, 0x9F))
        return Type::BN;
    if (c == 0x200E) return Type::L;
    if (c == 0x200F) return Type::R;
    if (c == 0x061C) return Type::AL;
    // Marks that take the type of what they follow.
    if (in(c, 0x0300, 0x036F) || in(c, 0x0483, 0x0489) || in(c, 0x0591, 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 || c == 0x05C4 ||
        c == 0x05C5 || c == 0x05C7 || in(c, 0x0610, 0x061A) || in(c, 0x064B, 0x065F) || c == 0x0670 || in(c, 0x06D6, 0x06DC) ||
        in(c, 0x06DF, 0x06E4) || c == 0x06E7 || c == 0x06E8 || in(c, 0x06EA, 0x06ED) || in(c, 0x1AB0, 0x1AFF) || in(c, 0x1DC0, 0x1DFF) ||
        in(c, 0x20D0, 0x20FF) || in(c, 0xFE20, 0xFE2F))
        return Type::NSM;
    // Numbers
    if (in(c, 0x0660, 0x0669) || c == 0x066B || c == 0x066C) return Type::AN;
    if (in(c, 0x06F0, 0x06F9)) return Type::EN;
    if (c == 0x066A || in(c, 0x00A2, 0x00A5) || c == 0x00B0 || c == 0x00B1 || in(c, 0x20A0, 0x20CF) || c == 0x2030 || c == 0x2031) return Type::ET;
    if (c == 0x060C || c == 0x00A0 || c == 0x202F || c == 0x2044) return Type::CS;
    if (in(c, 0x2000, 0x200A) || c == 0x3000 || c == 0x2028 || c == 0x1680 || c == 0x205F) return Type::WS;
    // Right-to-left scripts
    if (in(c, 0x0590, 0x05FF) || in(c, 0x07C0, 0x085F) || in(c, 0xFB1D, 0xFB4F) || in(c, 0x10800, 0x10FFF) || in(c, 0x1E800, 0x1EFFF)) return Type::R;
    if (in(c, 0x0600, 0x07BF) || in(c, 0x0860, 0x08FF) || in(c, 0xFB50, 0xFDFF) || in(c, 0xFE70, 0xFEFF) || in(c, 0x1EE00, 0x1EEFF)) return Type::AL;
    // Punctuation and symbols that belong to no script
    if (in(c, 0x00A1, 0x00BF) && c != 0x00AA && c != 0x00B5 && c != 0x00BA) return Type::ON;
    if (c == 0x00D7 || c == 0x00F7) return Type::ON;
    if (in(c, 0x2010, 0x2027) || in(c, 0x2032, 0x205E) || in(c, 0x2190, 0x2BFF) || in(c, 0x3001, 0x3004) || in(c, 0x3008, 0x3020) ||
        in(c, 0xFF01, 0xFF0F) || in(c, 0xFF1A, 0xFF20) || in(c, 0x1F000, 0x1FAFF))
        return Type::ON;
    return Type::L;
}

bool hasRtl(const std::u32string& text) {
    for (char32_t c : text) {
        const Type t = typeOf(c);
        if (t == Type::R || t == Type::AL) return true;
    }
    return false;
}

// ---- Arabic shaping -----------------------------------------------------------

namespace {

// Presentation Forms-B (and Persian letters of Forms-A): isolated, final,
// initial, medial. 0 = the letter has no such form.
struct Forms {
    char32_t base, iso, fin, ini, med;
};
constexpr Forms kForms[] = {
    { 0x0621, 0xFE80, 0, 0, 0 },           { 0x0622, 0xFE81, 0xFE82, 0, 0 },      { 0x0623, 0xFE83, 0xFE84, 0, 0 },
    { 0x0624, 0xFE85, 0xFE86, 0, 0 },      { 0x0625, 0xFE87, 0xFE88, 0, 0 },      { 0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C },
    { 0x0627, 0xFE8D, 0xFE8E, 0, 0 },      { 0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92 }, { 0x0629, 0xFE93, 0xFE94, 0, 0 },
    { 0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98 }, { 0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C }, { 0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0 },
    { 0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4 }, { 0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8 }, { 0x062F, 0xFEA9, 0xFEAA, 0, 0 },
    { 0x0630, 0xFEAB, 0xFEAC, 0, 0 },      { 0x0631, 0xFEAD, 0xFEAE, 0, 0 },      { 0x0632, 0xFEAF, 0xFEB0, 0, 0 },
    { 0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4 }, { 0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8 }, { 0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC },
    { 0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0 }, { 0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4 }, { 0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8 },
    { 0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC }, { 0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0 }, { 0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4 },
    { 0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8 }, { 0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC }, { 0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0 },
    { 0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4 }, { 0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8 }, { 0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC },
    { 0x0648, 0xFEED, 0xFEEE, 0, 0 },      { 0x0649, 0xFEEF, 0xFEF0, 0, 0 },      { 0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4 },
    // Persian and Urdu letters
    { 0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59 }, { 0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D }, { 0x0698, 0xFB8A, 0xFB8B, 0, 0 },
    { 0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91 }, { 0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95 }, { 0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF },
};

const Forms* formsOf(char32_t c) {
    for (const Forms& f : kForms)
        if (f.base == c) return &f;
    return nullptr;
}

bool transparent(char32_t c) { return typeOf(c) == Type::NSM && (in(c, 0x064B, 0x065F) || c == 0x0670 || in(c, 0x0610, 0x061A) || in(c, 0x06D6, 0x06ED)); }
// A letter that joins to the next one (it has an initial form).
bool joinsNext(char32_t c) {
    const Forms* f = formsOf(c);
    return f && f->ini != 0;
}
// A letter that joins to the previous one (it has a final form).
bool joinsPrev(char32_t c) {
    const Forms* f = formsOf(c);
    return f && f->fin != 0;
}

bool alef(char32_t c) { return c == 0x0622 || c == 0x0623 || c == 0x0625 || c == 0x0627; }

// lam + alef ligatures: isolated, final
void ligature(char32_t alefChar, char32_t& iso, char32_t& fin) {
    switch (alefChar) {
    case 0x0622: iso = 0xFEF5; fin = 0xFEF6; break;
    case 0x0623: iso = 0xFEF7; fin = 0xFEF8; break;
    case 0x0625: iso = 0xFEF9; fin = 0xFEFA; break;
    default: iso = 0xFEFB; fin = 0xFEFC; break;
    }
}

} // namespace

std::u32string shapeArabic(const std::u32string& in_) {
    std::u32string out;
    out.reserve(in_.size());
    const size_t n = in_.size();
    for (size_t i = 0; i < n; ++i) {
        const char32_t c = in_[i];
        const Forms* f = formsOf(c);
        if (!f) {
            out.push_back(c);
            continue;
        }
        // The letter before (skipping marks) joins to this one?
        bool prev = false;
        for (size_t k = i; k-- > 0;) {
            if (transparent(in_[k])) continue;
            prev = joinsNext(in_[k]);
            break;
        }
        // lam + alef is one glyph (marks between them stay after it)
        if (c == 0x0644 && i + 1 < n && alef(in_[i + 1])) {
            char32_t iso, fin;
            ligature(in_[i + 1], iso, fin);
            out.push_back(prev ? fin : iso);
            ++i;
            continue;
        }
        bool next = false;
        for (size_t k = i + 1; k < n; ++k) {
            if (transparent(in_[k])) continue;
            next = joinsPrev(in_[k]);
            break;
        }
        char32_t shaped = f->iso;
        if (f->ini != 0) { // joins both ways
            if (prev && next) shaped = f->med;
            else if (prev) shaped = f->fin;
            else if (next) shaped = f->ini;
        } else if (prev && f->fin != 0) { // joins the letter before only
            shaped = f->fin;
        }
        out.push_back(shaped);
    }
    return out;
}

// ---- The bidirectional algorithm ----------------------------------------------

namespace {

bool isNeutral(Type t) { return t == Type::B || t == Type::S || t == Type::WS || t == Type::ON; }

char32_t mirrored(char32_t c) {
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '<': return '>';
    case '>': return '<';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    case 0x2039: return 0x203A;
    case 0x203A: return 0x2039;
    default: return c;
    }
}

} // namespace

std::u32string reorder(const std::u32string& logical, int baseLevel) {
    // Invisible controls take no part.
    std::u32string text;
    std::vector<Type> types;
    text.reserve(logical.size());
    for (char32_t c : logical) {
        const Type t = typeOf(c);
        if (t == Type::BN) continue;
        text.push_back(c);
        types.push_back(t);
    }
    const size_t n = text.size();
    if (n == 0) return text;

    int base = baseLevel;
    if (base < 0) {
        base = 0;
        for (Type t : types) {
            if (t == Type::L) break;
            if (t == Type::R || t == Type::AL) {
                base = 1;
                break;
            }
        }
    }
    const Type sor = base == 1 ? Type::R : Type::L;
    const Type eor = sor;

    // W1: marks take the type before them
    for (size_t i = 0; i < n; ++i)
        if (types[i] == Type::NSM) types[i] = i == 0 ? sor : (isNeutral(types[i - 1]) || types[i - 1] == Type::NSM ? Type::ON : types[i - 1]);
    // W2: a European number after Arabic letters is an Arabic number
    {
        Type strong = sor;
        for (size_t i = 0; i < n; ++i) {
            if (types[i] == Type::L || types[i] == Type::R || types[i] == Type::AL) strong = types[i];
            else if (types[i] == Type::EN && strong == Type::AL) types[i] = Type::AN;
        }
    }
    // W3
    for (Type& t : types)
        if (t == Type::AL) t = Type::R;
    // W4: one separator between two numbers
    for (size_t i = 1; i + 1 < n; ++i) {
        if (types[i] == Type::ES && types[i - 1] == Type::EN && types[i + 1] == Type::EN) types[i] = Type::EN;
        else if (types[i] == Type::CS && types[i - 1] == types[i + 1] && (types[i - 1] == Type::EN || types[i - 1] == Type::AN)) types[i] = types[i - 1];
    }
    // W5: terminators next to European numbers
    for (size_t i = 0; i < n;) {
        if (types[i] != Type::ET) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && types[j] == Type::ET) ++j;
        const bool touches = (i > 0 && types[i - 1] == Type::EN) || (j < n && types[j] == Type::EN);
        if (touches)
            for (size_t k = i; k < j; ++k) types[k] = Type::EN;
        i = j;
    }
    // W6
    for (Type& t : types)
        if (t == Type::ES || t == Type::ET || t == Type::CS) t = Type::ON;
    // W7: a European number after left-to-right letters is one
    {
        Type strong = sor;
        for (size_t i = 0; i < n; ++i) {
            if (types[i] == Type::L || types[i] == Type::R) strong = types[i];
            else if (types[i] == Type::EN && strong == Type::L) types[i] = Type::L;
        }
    }
    // N1, N2: neutrals between the same direction take it, else the paragraph's
    auto direction = [](Type t) { return t == Type::L ? Type::L : Type::R; }; // R, EN and AN count as R
    for (size_t i = 0; i < n;) {
        if (!isNeutral(types[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && isNeutral(types[j])) ++j;
        const Type before = i == 0 ? sor : direction(types[i - 1]);
        const Type after = j == n ? eor : direction(types[j]);
        const Type resolved = before == after ? before : (base == 1 ? Type::R : Type::L);
        for (size_t k = i; k < j; ++k) types[k] = resolved;
        i = j;
    }
    // I1, I2: levels
    std::vector<int> level(n);
    for (size_t i = 0; i < n; ++i) {
        const Type t = types[i];
        if (base % 2 == 0) level[i] = t == Type::R ? base + 1 : (t == Type::AN || t == Type::EN) ? base + 2 : base;
        else level[i] = (t == Type::L || t == Type::EN || t == Type::AN) ? base + 1 : base;
    }
    // L2: from the highest level down to the lowest odd one, reverse each run at that level or above
    int highest = 0, lowestOdd = 1000;
    for (int l : level) {
        highest = std::max(highest, l);
        if (l % 2 == 1) lowestOdd = std::min(lowestOdd, l);
    }
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    for (int l = highest; l >= lowestOdd && l >= 1; --l) {
        for (size_t i = 0; i < n;) {
            if (level[order[i]] < l) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < n && level[order[j]] >= l) ++j;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }
    std::u32string out;
    out.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        const size_t i = order[k];
        out.push_back(level[i] % 2 == 1 ? mirrored(text[i]) : text[i]);
    }
    return out;
}

std::string display(const std::string& utf8, int baseLevel) {
    const std::u32string all = toUtf32(utf8);
    if (!hasRtl(all)) return utf8;
    std::u32string out;
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find(U'\n', start);
        if (end == std::u32string::npos) end = all.size();
        out += reorder(shapeArabic(all.substr(start, end - start)), baseLevel);
        if (end < all.size()) out.push_back(U'\n');
        start = end + 1;
    }
    return toUtf8(out);
}

} // namespace kke::bidi
