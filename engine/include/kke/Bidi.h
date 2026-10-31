#pragma once

#include <string>

namespace kke::bidi {

// Right-to-left text for a renderer that draws left to right and does no
// shaping (RmlUi's default font engine, ENG-F84). Two steps:
//
//   shapeArabic  Arabic letters change shape by what they sit next to
//                (isolated, final, initial, medial) and lam + alef join
//                into one glyph. Text is stored as the plain letters; this
//                picks the Presentation Forms-B code points a font draws.
//   reorder      The Unicode bidirectional algorithm (UAX #9) as far as
//                real text needs it: weak and neutral types, numbers, mirrored
//                brackets and the visual order of a line. Explicit embeddings
//                and isolates (LRE, RLO, ...) are ignored (dropped).
//
// display() does both on UTF-8, line by line. Unit-tested in tests/test_bidi.cpp.

std::u32string toUtf32(const std::string& utf8);
std::string toUtf8(const std::u32string& text);

enum class Type { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };
Type typeOf(char32_t c);
// Any letter written right to left.
bool hasRtl(const std::u32string& text);

std::u32string shapeArabic(const std::u32string& logical);

// The visual order of one line of `logical`. baseLevel 0 is a left-to-right
// paragraph, 1 right-to-left, -1 takes the first strong letter's direction.
std::u32string reorder(const std::u32string& logical, int baseLevel = -1);

// Shape, reorder and encode; every line of the text on its own. Text with no
// right-to-left letters comes back unchanged.
std::string display(const std::string& utf8, int baseLevel = -1);

} // namespace kke::bidi
