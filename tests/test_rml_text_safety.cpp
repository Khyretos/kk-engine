// Tests for kke::escapeRmlText — the sanctioned way to place untrusted
// text into RML markup (see kke/RmlTextSafety.h). This is the function
// standing between a malicious game.json title and actually breaking out
// of its intended element, so its edge cases matter more than most.

#include "kke/RmlTextSafety.h"
#include <gtest/gtest.h>

using kke::escapeRmlText;

TEST(EscapeRmlText, EmptyStringStaysEmpty) {
    EXPECT_EQ(escapeRmlText(""), "");
}

TEST(EscapeRmlText, PlainTextIsUnchanged) {
    EXPECT_EQ(escapeRmlText("Kreative Kompas Engine"), "Kreative Kompas Engine");
}

TEST(EscapeRmlText, EscapesAmpersandFirst) {
    // & must be escaped, and escaped exactly once — a naive
    // find-and-replace-all-at-once implementation can accidentally
    // double-escape (e.g. produce &amp;amp; from an input containing a
    // literal "&amp;" string). This engine's game.json fields are
    // arbitrary text, not markup, so a literal "&amp;" in a title should
    // become a doubly-escaped, still-inert string, not decode back to "&".
    EXPECT_EQ(escapeRmlText("Cats & Dogs"), "Cats &amp; Dogs");
    EXPECT_EQ(escapeRmlText("&amp;"), "&amp;amp;");
}

TEST(EscapeRmlText, EscapesAngleBrackets) {
    EXPECT_EQ(escapeRmlText("<div>"), "&lt;div&gt;");
}

TEST(EscapeRmlText, EscapesQuotesAsNumericReferences) {
    // Numeric character references, not the named &quot;/&apos; — see
    // RmlTextSafety.h for why: verified empirically (building
    // MarketplaceUiModule) that RmlUi's XML parser renders &apos; as five
    // literal characters instead of decoding it back to a quote mark.
    EXPECT_EQ(escapeRmlText("\""), "&#34;");
    EXPECT_EQ(escapeRmlText("'"), "&#39;");
    EXPECT_EQ(escapeRmlText("It's \"quoted\""), "It&#39;s &#34;quoted&#34;");
}

TEST(EscapeRmlText, NeutralizesElementInjectionAttempt) {
    // The actual threat model: a malicious game.json title trying to
    // close the <p> it's meant to be text inside of and open a new,
    // attacker-controlled element (e.g. a fullscreen overlay).
    std::string malicious = "</p><div style=\"position:absolute;top:0;left:0;width:9999px;height:9999px;\">pwned";
    std::string escaped = escapeRmlText(malicious);

    EXPECT_EQ(escaped.find("</p>"), std::string::npos) << "a literal closing tag must not survive escaping";
    EXPECT_EQ(escaped.find("<div"), std::string::npos) << "a literal opening tag must not survive escaping";
    // The text is still present, just inert — escaping should preserve
    // the content for display, not delete it.
    EXPECT_NE(escaped.find("pwned"), std::string::npos);
}

TEST(EscapeRmlText, NeutralizesAttributeBreakoutAttempt) {
    // If this string were ever placed inside a double-quoted attribute
    // value (not how MarketplaceUiModule uses it today, but the function
    // should be safe for that use too), it must not be able to close the
    // attribute early.
    std::string malicious = "\" onclick=\"alert(1)";
    std::string escaped = escapeRmlText(malicious);
    EXPECT_EQ(escaped.find('"'), std::string::npos) << "raw double-quote must not survive escaping";
}

TEST(EscapeRmlText, HandlesUnicodeTextUnchanged) {
    // Multi-byte UTF-8 (e.g. emoji, non-Latin scripts) must pass through
    // untouched — this function only escapes five specific ASCII
    // characters and must not corrupt anything else, since game titles
    // are exactly where non-English text and emoji are expected to
    // appear (see README "Default fonts").
    EXPECT_EQ(escapeRmlText("Kreative Kompas 🎮"), "Kreative Kompas 🎮");
}
