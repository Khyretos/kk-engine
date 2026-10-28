#include "kke/Bidi.h"

#include <gtest/gtest.h>

// Right-to-left text (docs/LOCALIZATION.md): Arabic letters joined, lines
// reordered by the Unicode Bidirectional Algorithm. The expected strings are
// worked out by hand from UAX #9 and the Arabic joining tables, written as
// code points so the editor's own bidi never gets a say.

using namespace kke::bidi;

namespace {
const std::string kShalom = "\u05E9\u05DC\u05D5\u05DD";      // shin lamed vav mem (logical order)
const std::string kShalomVisual = "\u05DD\u05D5\u05DC\u05E9"; // the same, as a screen draws it left to right
} // namespace

TEST(Bidi, LeftToRightTextIsLeftAlone) {
    EXPECT_EQ(toVisual("Hello, world 123!"), "Hello, world 123!");
    EXPECT_EQ(display("Hello, world 123!"), "Hello, world 123!");
    EXPECT_EQ(display("Привет, 日本語"), "Привет, 日本語");
    EXPECT_EQ(display(""), "");
    EXPECT_FALSE(hasRtl("Hello, Привет, 日本語, 😀"));
}

TEST(Bidi, AHebrewWordIsReversed) {
    EXPECT_TRUE(hasRtl(kShalom));
    EXPECT_EQ(toVisual(kShalom), kShalomVisual);
}

TEST(Bidi, AHebrewWordInsideEnglishStaysWhereItIs) {
    EXPECT_EQ(toVisual("abc " + kShalom + " def"), "abc " + kShalomVisual + " def");
}

TEST(Bidi, NumbersInRightToLeftTextKeepTheirOrderAndMoveLeft) {
    // Hebrew paragraph: the Hebrew reads first (right), the number after it (left).
    EXPECT_EQ(toVisual(kShalom + " 123"), "123 " + kShalomVisual);
    // And English words in it keep their order.
    EXPECT_EQ(toVisual(kShalom + " abc"), "abc " + kShalomVisual);
}

TEST(Bidi, BracketsFaceTheOtherWayInRightToLeftText) {
    EXPECT_EQ(toVisual("(" + kShalom + ")"), "(" + kShalomVisual + ")");
    // The characters are swapped and the order reversed: the closing one is at the left.
    EXPECT_EQ(toVisual("[" + kShalom), kShalomVisual + "]");
}

TEST(Bidi, TheParagraphDirectionCanBeForced) {
    EXPECT_EQ(toVisual("abc", Direction::RightToLeft), "abc"); // a left-to-right run keeps its order
    EXPECT_EQ(toVisual(kShalom, Direction::LeftToRight), kShalomVisual);
    // An English sentence ending in a full stop, in a right-to-left paragraph: the stop goes to the left.
    EXPECT_EQ(toVisual("abc.", Direction::RightToLeft), ".abc");
}

TEST(Bidi, EveryLineIsItsOwnParagraph) {
    EXPECT_EQ(toVisual(kShalom + "\nabc\n" + kShalom), kShalomVisual + "\nabc\n" + kShalomVisual);
}

TEST(Bidi, InvisibleControlsAreDropped) {
    EXPECT_EQ(toVisual("a\u200Bb"), "ab");
}

TEST(Bidi, ArabicLettersTakeTheirJoinedForms) {
    EXPECT_EQ(shapeArabic("\u0628"), "\uFE8F");                // beh alone: isolated
    EXPECT_EQ(shapeArabic("\u0628\u0628"), "\uFE91\uFE90");    // initial, final
    EXPECT_EQ(shapeArabic("\u0628\u0628\u0628"), "\uFE91\uFE92\uFE90"); // initial, medial, final
    // kaf teh alef beh: alef does not join forward, so beh starts afresh.
    EXPECT_EQ(shapeArabic("\u0643\u062A\u0627\u0628"), "\uFEDB\uFE98\uFE8E\uFE8F");
    EXPECT_EQ(shapeArabic("abc"), "abc");
    EXPECT_EQ(shapeArabic("a \u0628 b"), "a \uFE8F b"); // spaces and Latin break a joining run
}

TEST(Bidi, LamAlefIsOneLigature) {
    EXPECT_EQ(shapeArabic("\u0644\u0627"), "\uFEFB");          // isolated
    EXPECT_EQ(shapeArabic("\u0628\u0644\u0627"), "\uFE91\uFEFC"); // after a joining letter: final form
    EXPECT_EQ(shapeArabic("\u0644\u0623"), "\uFEF7");          // lam + alef with hamza above
}

TEST(Bidi, MarksStayAndDoNotBreakAJoin) {
    EXPECT_EQ(shapeArabic("\u0628\u064E\u0628"), "\uFE91\u064E\uFE90"); // beh fatha beh
}

TEST(Bidi, PersianAndUrduLettersJoinToo) {
    EXPECT_EQ(shapeArabic("\u067E"), "\uFB56");               // peh
    EXPECT_EQ(shapeArabic("\u067E\u067E"), "\uFB58\uFB57");
    EXPECT_EQ(shapeArabic("\u06CC\u06CC"), "\uFBFE\uFBFD");   // farsi yeh
}

TEST(Bidi, ArabicTextIsJoinedAndReversedForTheScreen) {
    // marhaba: meem reh hah beh alef
    EXPECT_EQ(display("\u0645\u0631\u062D\u0628\u0627"), "\uFE8E\uFE92\uFEA3\uFEAE\uFEE3");
    EXPECT_TRUE(hasRtl("\u0645\u0631\u062D\u0628\u0627"));
}

TEST(Bidi, DigitsAfterArabicAreArabicDigitsAndStayInOrder) {
    // "year 2024": ain alef meem, space, 2024
    EXPECT_EQ(display("\u0639\u0627\u0645 2024"), "2024 \uFEE1\uFE8E\uFECB");
}

TEST(Bidi, Utf8RoundTripsAndBadBytesBecomeReplacementCharacters) {
    const std::string text = "a\u00E9\u05D0\u20AC\U0001F600";
    EXPECT_EQ(encode(decode(text)), text);
    EXPECT_EQ(decode(text).size(), 5u);
    const std::u32string bad = decode(std::string("a\xFF" "b\xC3"));
    ASSERT_EQ(bad.size(), 4u); // a, bad byte, b, a lead byte with nothing after it
    EXPECT_EQ(bad[1], 0xFFFDu);
    EXPECT_EQ(bad[2], static_cast<char32_t>('b'));
    EXPECT_EQ(bad[3], 0xFFFDu);
}
