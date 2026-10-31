#pragma once

#include <string>
#include <string_view>

namespace kke::bidi {

// Right-to-left text for a UI that has no shaping engine of its own
// (docs/LOCALIZATION.md "Right to left"): the text a game stores is in
// logical order, the way it is typed and read aloud; a screen needs it in
// visual order, with Arabic letters in their joined forms. display() does
// both, from and to UTF-8, so it can sit between any string and any
// renderer.
//
// Bidi: the Unicode Bidirectional Algorithm (UAX #9) for one line at a time:
// paragraph direction, weak types (W1-W7), neutrals (N1-N2), implicit levels
// (I1-I2), trailing white space (L1), reordering (L2) and mirrored brackets
// (L4). Explicit embeddings, overrides and isolates (U+202A-202E,
// U+2066-2069) are not handled and are dropped; the marks U+200E, U+200F and
// U+061C work. Bracket pairing (N0) is not done: brackets take the
// direction of their neighbours.
//
// Shaping: Arabic letters (and Persian and Urdu pe, che, zhe, kaf, gaf, yeh)
// to Presentation Forms-A/B (U+FB50-FDFF, U+FE70-FEFF) as initial, medial,
// final or isolated, with the lam-alef ligatures. Marks (tashkeel) stay
// where they are. Fonts that carry these forms (Noto Sans Arabic does)
// draw the text joined.

enum class Direction { Auto, LeftToRight, RightToLeft };

// True when the text holds any character that needs this (Hebrew, Arabic and
// their relatives): the fast check before doing any work.
bool hasRtl(std::string_view utf8);

// Arabic letters to their joined forms, still in logical order.
std::string shapeArabic(std::string_view utf8);

// Logical order to visual order, line by line. `base` is the paragraph
// direction (Auto: the first strong letter decides, per line).
std::string toVisual(std::string_view utf8, Direction base = Direction::Auto);

// shapeArabic then toVisual: what to hand to the renderer. Text without any
// right-to-left letter comes back unchanged (with `base` Auto); a
// left-to-right sentence in a right-to-left interface keeps its order too,
// and sits at the right-hand side by the renderer's text-align, not by this.
std::string display(std::string_view utf8, Direction base = Direction::Auto);

// UTF-8 <-> code points (invalid bytes become U+FFFD).
std::u32string decode(std::string_view utf8);
std::string encode(std::u32string_view text);

} // namespace kke::bidi
