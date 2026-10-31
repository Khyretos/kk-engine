#include "kke/Bidi.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace kke::bidi {

namespace {

enum class Cls : uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };

bool between(char32_t c, char32_t lo, char32_t hi) { return c >= lo && c <= hi; }

Cls classOf(char32_t c) {
    if (c < 0x80) {
        if (between(c, '0', '9')) return Cls::EN;
        if (between(c, 'A', 'Z') || between(c, 'a', 'z')) return Cls::L;
        switch (c) {
        case '+':
        case '-': return Cls::ES;
        case '#':
        case '$':
        case '%': return Cls::ET;
        case ',':
        case '.':
        case '/':
        case ':': return Cls::CS;
        case ' ':
        case 0x0C: return Cls::WS;
        case '\t':
        case 0x0B:
        case 0x1F: return Cls::S;
        case '\n':
        case '\r':
        case 0x1C:
        case 0x1D:
        case 0x1E: return Cls::B;
        default: return (c < 0x20 || c == 0x7F) ? Cls::BN : Cls::ON;
        }
    }
    if (c < 0xA0) return c == 0x85 ? Cls::B : Cls::BN;
    if (c < 0x100) {
        if (c == 0xA0) return Cls::CS;
        if (c == 0xAD) return Cls::BN;
        if (between(c, 0xA2, 0xA5) || c == 0xB0 || c == 0xB1) return Cls::ET;
        if (c == 0xB2 || c == 0xB3 || c == 0xB9) return Cls::EN;
        if (c == 0xAA || c == 0xB5 || c == 0xBA) return Cls::L;
        if (c < 0xC0 || c == 0xD7 || c == 0xF7) return Cls::ON;
        return Cls::L;
    }
    if (between(c, 0x300, 0x36F) || between(c, 0x483, 0x489)) return Cls::NSM;
    if (between(c, 0x590, 0x5FF)) { // Hebrew
        if (between(c, 0x591, 0x5BD) || c == 0x5BF || between(c, 0x5C1, 0x5C2) || between(c, 0x5C4, 0x5C5) || c == 0x5C7) return Cls::NSM;
        return Cls::R;
    }
    if (between(c, 0x600, 0x6FF)) { // Arabic
        if (between(c, 0x600, 0x605) || c == 0x6DD) return Cls::AN;
        if (c == 0x606 || c == 0x607 || c == 0x6DE || c == 0x6E9 || c == 0x60E || c == 0x60F) return Cls::ON;
        if (c == 0x609 || c == 0x60A || c == 0x66A) return Cls::ET;
        if (c == 0x60C) return Cls::CS;
        if (between(c, 0x610, 0x61A) || between(c, 0x64B, 0x65F) || c == 0x670 || between(c, 0x6D6, 0x6DC) ||
            between(c, 0x6DF, 0x6E4) || c == 0x6E7 || c == 0x6E8 || between(c, 0x6EA, 0x6ED))
            return Cls::NSM;
        if (between(c, 0x660, 0x669) || c == 0x66B || c == 0x66C) return Cls::AN;
        if (between(c, 0x6F0, 0x6F9)) return Cls::EN;
        return Cls::AL;
    }
    if (between(c, 0x700, 0x7BF)) return Cls::AL;      // Syriac, Arabic Supplement, Thaana
    if (between(c, 0x7C0, 0x85F)) return Cls::R;       // N'Ko, Samaritan, Mandaic
    if (between(c, 0x860, 0x8FF)) return (between(c, 0x8CA, 0x8D2) || between(c, 0x8D3, 0x8E1) || between(c, 0x8E3, 0x8FF)) ? Cls::NSM : Cls::AL;
    if (c == 0x200B || c == 0x200C || c == 0x200D) return Cls::BN;
    if (c == 0x200E) return Cls::L;
    if (c == 0x200F) return Cls::R;
    if (c == 0x061C) return Cls::AL;
    if (between(c, 0x2000, 0x200A) || c == 0x2028 || c == 0x3000) return Cls::WS;
    if (c == 0x2029) return Cls::B;
    if (between(c, 0x202A, 0x202E) || between(c, 0x2060, 0x2064) || between(c, 0x2066, 0x2069) || c == 0xFEFF) return Cls::BN;
    if (c == 0x202F) return Cls::CS;
    if (between(c, 0x2030, 0x2034) || between(c, 0x20A0, 0x20CF)) return Cls::ET;
    if (between(c, 0x2010, 0x205E) || between(c, 0x2190, 0x2BFF) || between(c, 0x3001, 0x303F)) return Cls::ON;
    if (between(c, 0xFB1D, 0xFB4F)) return c == 0xFB1E ? Cls::NSM : Cls::R;
    if (between(c, 0xFB50, 0xFDFF)) return (c == 0xFD3E || c == 0xFD3F || c == 0xFDFD) ? Cls::ON : Cls::AL;
    if (between(c, 0xFE00, 0xFE0F) || between(c, 0xFE20, 0xFE2F)) return Cls::NSM;
    if (between(c, 0xFE70, 0xFEFE)) return Cls::AL;
    if (between(c, 0xFF10, 0xFF19)) return Cls::EN;
    if (c == 0xFF0B || c == 0xFF0D) return Cls::ES;
    if (between(c, 0xFF03, 0xFF05)) return Cls::ET;
    if (between(c, 0x10800, 0x10FFF)) return Cls::R;
    if (between(c, 0x1F000, 0x1FAFF)) return Cls::ON;
    return Cls::L;
}

char32_t mirror(char32_t c) {
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    case 0xAB: return 0xBB;
    case 0xBB: return 0xAB;
    case 0x2039: return 0x203A;
    case 0x203A: return 0x2039;
    default: return c;
    }
}

bool isNeutral(Cls c) { return c == Cls::B || c == Cls::S || c == Cls::WS || c == Cls::ON; }
Cls strongDirection(Cls c) { return c == Cls::L ? Cls::L : Cls::R; } // EN and AN count as R for N1

// One line, logical order to visual order.
std::u32string reorderLine(const std::u32string& input, Direction base) {
    std::u32string text;
    text.reserve(input.size());
    for (char32_t c : input)
        if (classOf(c) != Cls::BN) text.push_back(c);
    const size_t n = text.size();
    if (n == 0) return text;

    std::vector<Cls> original(n), types(n);
    for (size_t i = 0; i < n; ++i) original[i] = types[i] = classOf(text[i]);

    int paragraph = 0;
    if (base == Direction::RightToLeft) paragraph = 1;
    else if (base == Direction::Auto)
        for (Cls t : original)
            if (t == Cls::L || t == Cls::R || t == Cls::AL) {
                paragraph = t == Cls::L ? 0 : 1;
                break;
            }
    const Cls sos = paragraph ? Cls::R : Cls::L;

    // W1: a mark takes the type of what it follows.
    Cls previous = sos;
    for (size_t i = 0; i < n; ++i) {
        if (types[i] == Cls::NSM) types[i] = previous;
        else previous = types[i];
    }
    // W2: a European digit after an Arabic letter is an Arabic digit.
    Cls lastStrong = sos;
    for (size_t i = 0; i < n; ++i) {
        if (types[i] == Cls::L || types[i] == Cls::R || types[i] == Cls::AL) lastStrong = types[i];
        else if (types[i] == Cls::EN && lastStrong == Cls::AL) types[i] = Cls::AN;
    }
    // W3: Arabic letters are right-to-left letters from here on.
    for (Cls& t : types)
        if (t == Cls::AL) t = Cls::R;
    // W4: one separator between two numbers of the same kind joins them.
    for (size_t i = 1; i + 1 < n; ++i) {
        if (types[i] == Cls::ES && types[i - 1] == Cls::EN && types[i + 1] == Cls::EN) types[i] = Cls::EN;
        else if (types[i] == Cls::CS && types[i - 1] == Cls::EN && types[i + 1] == Cls::EN) types[i] = Cls::EN;
        else if (types[i] == Cls::CS && types[i - 1] == Cls::AN && types[i + 1] == Cls::AN) types[i] = Cls::AN;
    }
    // W5: terminators next to a European digit are part of the number.
    for (size_t i = 0; i < n;) {
        if (types[i] != Cls::ET) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && types[j] == Cls::ET) ++j;
        if ((i > 0 && types[i - 1] == Cls::EN) || (j < n && types[j] == Cls::EN))
            for (size_t k = i; k < j; ++k) types[k] = Cls::EN;
        i = j;
    }
    // W6: the separators left over are neutral.
    for (Cls& t : types)
        if (t == Cls::ES || t == Cls::ET || t == Cls::CS) t = Cls::ON;
    // W7: a European digit after a left-to-right letter is one.
    lastStrong = sos;
    for (size_t i = 0; i < n; ++i) {
        if (types[i] == Cls::L || types[i] == Cls::R) lastStrong = types[i];
        else if (types[i] == Cls::EN && lastStrong == Cls::L) types[i] = Cls::L;
    }
    // N1, N2: neutrals between two letters of one direction take it; else the paragraph's.
    for (size_t i = 0; i < n;) {
        if (!isNeutral(types[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && isNeutral(types[j])) ++j;
        const Cls before = i == 0 ? sos : strongDirection(types[i - 1]);
        const Cls after = j == n ? sos : strongDirection(types[j]);
        const Cls resolved = before == after ? before : sos;
        for (size_t k = i; k < j; ++k) types[k] = resolved;
        i = j;
    }
    // I1, I2: levels.
    std::vector<int> level(n, paragraph);
    for (size_t i = 0; i < n; ++i) {
        if ((paragraph & 1) == 0) {
            if (types[i] == Cls::R) level[i] = paragraph + 1;
            else if (types[i] == Cls::AN || types[i] == Cls::EN) level[i] = paragraph + 2;
        } else if (types[i] == Cls::L || types[i] == Cls::EN || types[i] == Cls::AN) {
            level[i] = paragraph + 1;
        }
    }
    // L1: white space at the end of the line goes back to the paragraph's level.
    for (size_t i = n; i > 0; --i) {
        const Cls o = original[i - 1];
        if (o == Cls::WS || o == Cls::S || o == Cls::B) level[i - 1] = paragraph;
        else break;
    }
    // L2: from the highest level down to the lowest odd one, reverse each run at that level or above.
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{ 0 });
    int highest = 0, lowestOdd = 1000;
    for (int l : level) {
        highest = std::max(highest, l);
        if (l & 1) lowestOdd = std::min(lowestOdd, l);
    }
    for (int l = highest; l >= lowestOdd; --l) {
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
    // L4: brackets in right-to-left runs face the other way.
    std::u32string out;
    out.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        const size_t i = order[k];
        out.push_back((level[i] & 1) ? mirror(text[i]) : text[i]);
    }
    return out;
}

// ---- Arabic joining

struct Letter {
    char32_t code;
    char32_t isolated; // then final, initial, medial, in the next code points
    int forms;         // 1: isolated only, 2: isolated and final, 4: all four
};

constexpr Letter kLetters[] = {
    { 0x621, 0xFE80, 1 }, { 0x622, 0xFE81, 2 }, { 0x623, 0xFE83, 2 }, { 0x624, 0xFE85, 2 }, { 0x625, 0xFE87, 2 },
    { 0x626, 0xFE89, 4 }, { 0x627, 0xFE8D, 2 }, { 0x628, 0xFE8F, 4 }, { 0x629, 0xFE93, 2 }, { 0x62A, 0xFE95, 4 },
    { 0x62B, 0xFE99, 4 }, { 0x62C, 0xFE9D, 4 }, { 0x62D, 0xFEA1, 4 }, { 0x62E, 0xFEA5, 4 }, { 0x62F, 0xFEA9, 2 },
    { 0x630, 0xFEAB, 2 }, { 0x631, 0xFEAD, 2 }, { 0x632, 0xFEAF, 2 }, { 0x633, 0xFEB1, 4 }, { 0x634, 0xFEB5, 4 },
    { 0x635, 0xFEB9, 4 }, { 0x636, 0xFEBD, 4 }, { 0x637, 0xFEC1, 4 }, { 0x638, 0xFEC5, 4 }, { 0x639, 0xFEC9, 4 },
    { 0x63A, 0xFECD, 4 }, { 0x641, 0xFED1, 4 }, { 0x642, 0xFED5, 4 }, { 0x643, 0xFED9, 4 }, { 0x644, 0xFEDD, 4 },
    { 0x645, 0xFEE1, 4 }, { 0x646, 0xFEE5, 4 }, { 0x647, 0xFEE9, 4 }, { 0x648, 0xFEED, 2 }, { 0x649, 0xFEEF, 2 },
    { 0x64A, 0xFEF1, 4 },
    // Persian, Urdu and friends (Presentation Forms-A)
    { 0x679, 0xFB66, 4 }, { 0x67E, 0xFB56, 4 }, { 0x686, 0xFB7A, 4 }, { 0x688, 0xFB88, 2 }, { 0x691, 0xFB8C, 2 },
    { 0x698, 0xFB8A, 2 }, { 0x6A9, 0xFB8E, 4 }, { 0x6AF, 0xFB92, 4 }, { 0x6BE, 0xFBAA, 4 }, { 0x6C1, 0xFBA6, 4 },
    { 0x6CC, 0xFBFC, 4 }, { 0x6D2, 0xFBAE, 2 },
};

const Letter* letterOf(char32_t c) {
    for (const Letter& l : kLetters)
        if (l.code == c) return &l;
    return nullptr;
}

bool transparent(char32_t c) {
    return between(c, 0x610, 0x61A) || between(c, 0x64B, 0x65F) || c == 0x670 || between(c, 0x6D6, 0x6DC) || between(c, 0x6DF, 0x6E4) ||
           c == 0x6E7 || c == 0x6E8 || between(c, 0x6EA, 0x6ED);
}

// Joins to the letter after it (dual-joining).
bool joinsForward(char32_t c) {
    if (c == 0x640) return true; // tatweel
    const Letter* l = letterOf(c);
    return l && l->forms == 4;
}
// Joins to the letter before it (dual- or right-joining).
bool joinsBackward(char32_t c) {
    if (c == 0x640) return true;
    const Letter* l = letterOf(c);
    return l && l->forms >= 2;
}

char32_t lamAlef(char32_t alef, bool final) {
    switch (alef) {
    case 0x622: return final ? 0xFEF6 : 0xFEF5;
    case 0x623: return final ? 0xFEF8 : 0xFEF7;
    case 0x625: return final ? 0xFEFA : 0xFEF9;
    case 0x627: return final ? 0xFEFC : 0xFEFB;
    default: return 0;
    }
}

} // namespace

std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        int extra = 0;
        char32_t c = 0;
        if (b < 0x80) c = b;
        else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + static_cast<size_t>(extra) >= s.size() && extra > 0) { // cut off
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cont = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cont & 0xC0) != 0x80) { ok = false; break; }
            c = (c << 6) | (cont & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(c);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

std::string encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) {
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

bool hasRtl(std::string_view utf8) {
    // Every character that needs this starts with a byte of 0xD6 or more.
    bool maybe = false;
    for (char ch : utf8)
        if (static_cast<unsigned char>(ch) >= 0xD6) {
            maybe = true;
            break;
        }
    if (!maybe) return false;
    for (char32_t c : decode(utf8)) {
        const Cls k = classOf(c);
        if (k == Cls::R || k == Cls::AL || k == Cls::AN) return true;
    }
    return false;
}

std::string shapeArabic(std::string_view utf8) {
    std::u32string s = decode(utf8);
    std::u32string out;
    out.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        const char32_t c = s[i];
        const Letter* letter = letterOf(c);
        if (!letter) {
            out.push_back(c);
            continue;
        }
        // The letters round it that count: skip marks.
        bool before = false;
        for (size_t j = i; j > 0; --j) {
            if (transparent(s[j - 1])) continue;
            before = joinsForward(s[j - 1]);
            break;
        }
        // Lam followed straight by an alef is one ligature.
        if (c == 0x644 && i + 1 < n) {
            if (const char32_t lig = lamAlef(s[i + 1], before)) {
                out.push_back(lig);
                ++i;
                continue;
            }
        }
        bool after = false;
        for (size_t j = i + 1; j < n; ++j) {
            if (transparent(s[j])) continue;
            after = joinsBackward(s[j]);
            break;
        }
        int form = 0; // isolated
        if (letter->forms == 4) form = before && after ? 3 : before ? 1 : after ? 2 : 0;
        else if (letter->forms == 2) form = before ? 1 : 0;
        out.push_back(letter->isolated + static_cast<char32_t>(form));
    }
    return encode(out);
}

std::string toVisual(std::string_view utf8, Direction base) {
    const std::u32string s = decode(utf8);
    std::u32string out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == U'\n') {
            out += reorderLine(s.substr(start, i - start), base);
            if (i < s.size()) out.push_back(U'\n');
            start = i + 1;
        }
    }
    return encode(out);
}

std::string display(std::string_view utf8, Direction base) {
    if (base == Direction::Auto && !hasRtl(utf8)) return std::string(utf8);
    return toVisual(shapeArabic(utf8), base);
}

} // namespace kke::bidi
