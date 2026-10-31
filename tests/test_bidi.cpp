// Right-to-left text: Arabic shaping and the bidirectional algorithm (kke/Bidi.h, ENG-F84).
#include "kke/Bidi.h"

#include <gtest/gtest.h>

using namespace kke::bidi;

TEST(Utf, RoundTripsEveryLength) {
    const std::string text = "a \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 \xD8\xA8";
    const std::u32string u = toUtf32(text);
    ASSERT_EQ(u.size(), 9u);
    EXPECT_EQ(u[2], 0xE9u);
    EXPECT_EQ(u[4], 0x20ACu);
    EXPECT_EQ(u[6], 0x1F600u);
    EXPECT_EQ(toUtf8(u), text);
}

TEST(Utf, BrokenBytesBecomeReplacementCharacters) {
    EXPECT_EQ(toUtf32("a\x80z"), (std::u32string{ U'a', 0xFFFD, U'z' }));
    EXPECT_EQ(toUtf32("\xE2\x82"), (std::u32string{ 0xFFFD })) << "cut short";
    EXPECT_EQ(toUtf32("\xC3("), (std::u32string{ 0xFFFD, U'(' })) << "a bad continuation";
}

TEST(BidiTypes, ClassifiesTheCommonCharacters) {
    EXPECT_EQ(typeOf(U'a'), Type::L);
    EXPECT_EQ(typeOf(0xE9), Type::L);
    EXPECT_EQ(typeOf(0x4E2D), Type::L) << "CJK";
    EXPECT_EQ(typeOf(0x05D0), Type::R) << "Hebrew alef";
    EXPECT_EQ(typeOf(0x0627), Type::AL) << "Arabic alef";
    EXPECT_EQ(typeOf(0xFE8E), Type::AL) << "its shaped form";
    EXPECT_EQ(typeOf(U'5'), Type::EN);
    EXPECT_EQ(typeOf(0x0665), Type::AN) << "Arabic-Indic digit";
    EXPECT_EQ(typeOf(U' '), Type::WS);
    EXPECT_EQ(typeOf(U'!'), Type::ON);
    EXPECT_EQ(typeOf(U'-'), Type::ES);
    EXPECT_EQ(typeOf(U'%'), Type::ET);
    EXPECT_EQ(typeOf(U','), Type::CS);
    EXPECT_EQ(typeOf(0x064E), Type::NSM) << "a vowel mark";
    EXPECT_EQ(typeOf(0x200B), Type::BN);
    EXPECT_EQ(typeOf(U'\n'), Type::B);
    EXPECT_TRUE(hasRtl(U"abc \u05D0"));
    EXPECT_FALSE(hasRtl(U"abc 123 \u00E9"));
}

TEST(Arabic, LettersTakeTheFormTheirNeighboursGive) {
    // ba ya ta: initial, medial, final
    EXPECT_EQ(shapeArabic(U"\u0628\u064A\u062A"), (std::u32string{ 0xFE91, 0xFEF4, 0xFE96 }));
    // meem hah meem dal: initial, medial, medial, final
    EXPECT_EQ(shapeArabic(U"\u0645\u062D\u0645\u062F"), (std::u32string{ 0xFEE3, 0xFEA4, 0xFEE4, 0xFEAA }));
    // a lone letter is isolated
    EXPECT_EQ(shapeArabic(U"\u0628"), (std::u32string{ 0xFE8F }));
    // alef joins what is before it but not what is after
    EXPECT_EQ(shapeArabic(U"\u0628\u0627\u0628"), (std::u32string{ 0xFE91, 0xFE8E, 0xFE8F }));
    // a word ends at a space
    EXPECT_EQ(shapeArabic(U"\u0628 \u0628"), (std::u32string{ 0xFE8F, U' ', 0xFE8F }));
}

TEST(Arabic, LamAndAlefMakeOneGlyph) {
    // seen lam alef meem: seen initial, lam-alef final form, meem isolated
    EXPECT_EQ(shapeArabic(U"\u0633\u0644\u0627\u0645"), (std::u32string{ 0xFEB3, 0xFEFC, 0xFEE1 }));
    // lam alef on its own: the isolated ligature
    EXPECT_EQ(shapeArabic(U"\u0644\u0627"), (std::u32string{ 0xFEFB }));
    // with hamza above
    EXPECT_EQ(shapeArabic(U"\u0644\u0623"), (std::u32string{ 0xFEF7 }));
}

TEST(Arabic, VowelMarksDoNotBreakAJoin) {
    // ba + fatha + ya: the mark sits between them and they still join
    const std::u32string shaped = shapeArabic(U"\u0628\u064E\u064A");
    ASSERT_EQ(shaped.size(), 3u);
    EXPECT_EQ(shaped[0], 0xFE91u);
    EXPECT_EQ(shaped[1], 0x064Eu) << "the mark stays";
    EXPECT_EQ(shaped[2], 0xFEF2u);
}

TEST(Arabic, PersianLettersShapeToo) {
    // pe + alef: pe initial, alef final
    EXPECT_EQ(shapeArabic(U"\u067E\u0627"), (std::u32string{ 0xFB58, 0xFE8E }));
    // keheh + ya (Persian forms)
    EXPECT_EQ(shapeArabic(U"\u06A9\u06CC"), (std::u32string{ 0xFB90, 0xFBFD }));
    // other text is left alone
    EXPECT_EQ(shapeArabic(U"abc 123"), U"abc 123");
}

TEST(Reorder, LeftToRightTextStaysPut) {
    EXPECT_EQ(reorder(U"hello world"), U"hello world");
    EXPECT_EQ(reorder(U"abc 123, def!"), U"abc 123, def!");
    EXPECT_EQ(reorder(U""), U"");
}

TEST(Reorder, HebrewRunsBackwards) {
    EXPECT_EQ(reorder(U"\u05E9\u05DC\u05D5\u05DD"), U"\u05DD\u05D5\u05DC\u05E9");
}

TEST(Reorder, ALatinWordInsideARightToLeftSentenceKeepsItsOrder) {
    // Hebrew first: the paragraph is right to left. Drawn left to right: the English word, then the Hebrew reversed.
    EXPECT_EQ(reorder(U"\u05E9\u05DC\u05D5\u05DD abc"), U"abc \u05DD\u05D5\u05DC\u05E9");
    // The other way round, a left-to-right paragraph.
    EXPECT_EQ(reorder(U"abc \u05E9\u05DC\u05D5\u05DD"), U"abc \u05DD\u05D5\u05DC\u05E9");
}

TEST(Reorder, NumbersKeepTheirDigitsInOrder) {
    // alef bet gimel 123: the number stays "123", on the left of the letters
    EXPECT_EQ(reorder(U"\u05D0\u05D1\u05D2 123"), U"123 \u05D2\u05D1\u05D0");
    // a decimal number stays whole
    EXPECT_EQ(reorder(U"\u05D0 3.14 \u05D1"), U"\u05D1 3.14 \u05D0");
    // a percentage stays with its number
    EXPECT_EQ(reorder(U"\u05D0 50% \u05D1"), U"\u05D1 50% \u05D0");
}

TEST(Reorder, BracketsMirrorInRightToLeftRuns) {
    EXPECT_EQ(reorder(U"(\u05E9\u05DC\u05D5\u05DD)"), U"(\u05DD\u05D5\u05DC\u05E9)");
    EXPECT_EQ(reorder(U"[abc]"), U"[abc]") << "left to right: no change";
}

TEST(Reorder, TheParagraphDirectionCanBeForced) {
    // Forced right to left: even a Latin phrase sits on the right, but its own words keep their order.
    EXPECT_EQ(reorder(U"one two", 1), U"one two");
    EXPECT_EQ(reorder(U"\u05D0 b", 0), U"\u05D0 b");
    // Forced right to left with two Latin words round Hebrew
    EXPECT_EQ(reorder(U"ab \u05D0 cd", 1), U"cd \u05D0 ab");
}

TEST(Reorder, InvisibleControlsAreDropped) {
    EXPECT_EQ(reorder(U"a\u200Bb"), U"ab");
}

TEST(Display, ShapesAndReordersArabicForALeftToRightRenderer) {
    // marhaba: meem reh hah ba alef -> FEE3 FEAE FEA3 FE92 FE8E, drawn from the left: reversed
    const std::string arabic = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    EXPECT_EQ(toUtf32(display(arabic)), (std::u32string{ 0xFE8E, 0xFE92, 0xFEA3, 0xFEAE, 0xFEE3 }));
}

TEST(Display, LeavesPlainTextAndHandlesEachLine) {
    EXPECT_EQ(display("just text\nand more"), "just text\nand more");
    const std::string two = "\xD7\x90\xD7\x91\n\xD7\x92\xD7\x93"; // alef bet / gimel dalet
    EXPECT_EQ(toUtf32(display(two)), (std::u32string{ 0x05D1, 0x05D0, U'\n', 0x05D3, 0x05D2 })) << "each line on its own";
}
