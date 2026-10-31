#include "kke/Localization.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using kke::Localization;
using kke::PluralCategory;
using P = kke::PluralCategory;

namespace {

Localization sample() {
    Localization loc;
    loc.addStrings("en", nlohmann::json::parse(R"({
        "menu": { "start": "Start", "quit": "Quit" },
        "hello": "Hello {name}!",
        "coins": { "one": "{count} coin", "other": "{count} coins" },
        "braces": "use {{name}} to name it",
        "only_en": "English only"
    })"), "English");
    loc.addStrings("nl", nlohmann::json::parse(R"({
        "menu": { "start": "Begin" },
        "hello": "Hallo {name}!",
        "coins": { "one": "{count} munt", "other": "{count} munten" }
    })"), "Nederlands");
    loc.addStrings("pt", nlohmann::json::parse(R"({ "menu": { "quit": "Sair" } })"), "Portugues");
    return loc;
}

} // namespace

TEST(Plurals, EnglishLikeLanguagesHaveOneAndOther) {
    for (const char* lang : { "en", "de", "nl", "sv", "it", "es", "tr" }) {
        EXPECT_EQ(kke::pluralCategory(lang, 1), P::One) << lang;
        EXPECT_EQ(kke::pluralCategory(lang, 0), P::Other) << lang;
        EXPECT_EQ(kke::pluralCategory(lang, 2), P::Other) << lang;
        EXPECT_EQ(kke::pluralCategory(lang, 1.5), P::Other) << lang;
    }
}

TEST(Plurals, FrenchAndBrazilianPortugueseCountZeroAndFractionsOfOneAsOne) {
    EXPECT_EQ(kke::pluralCategory("fr", 0), P::One);
    EXPECT_EQ(kke::pluralCategory("fr", 1), P::One);
    EXPECT_EQ(kke::pluralCategory("fr", 1.5), P::One);
    EXPECT_EQ(kke::pluralCategory("fr", 2), P::Other);
    EXPECT_EQ(kke::pluralCategory("pt-BR", 0), P::One);
    EXPECT_EQ(kke::pluralCategory("pt-PT", 0), P::Other);
    EXPECT_EQ(kke::pluralCategory("pt-PT", 1), P::One);
}

TEST(Plurals, RussianHasOneFewManyAndOther) {
    EXPECT_EQ(kke::pluralCategory("ru", 1), P::One);
    EXPECT_EQ(kke::pluralCategory("ru", 21), P::One);
    EXPECT_EQ(kke::pluralCategory("ru", 2), P::Few);
    EXPECT_EQ(kke::pluralCategory("ru", 24), P::Few);
    EXPECT_EQ(kke::pluralCategory("ru", 5), P::Many);
    EXPECT_EQ(kke::pluralCategory("ru", 11), P::Many);
    EXPECT_EQ(kke::pluralCategory("ru", 12), P::Many);
    EXPECT_EQ(kke::pluralCategory("ru", 100), P::Many);
    EXPECT_EQ(kke::pluralCategory("ru", 1.5), P::Other);
}

TEST(Plurals, PolishCzechArabicHebrewAndTheLanguagesWithNone) {
    EXPECT_EQ(kke::pluralCategory("pl", 1), P::One);
    EXPECT_EQ(kke::pluralCategory("pl", 22), P::Few);
    EXPECT_EQ(kke::pluralCategory("pl", 12), P::Many);
    EXPECT_EQ(kke::pluralCategory("pl", 5), P::Many);
    EXPECT_EQ(kke::pluralCategory("cs", 3), P::Few);
    EXPECT_EQ(kke::pluralCategory("cs", 5), P::Other);
    EXPECT_EQ(kke::pluralCategory("cs", 1.5), P::Many);
    EXPECT_EQ(kke::pluralCategory("ar", 0), P::Zero);
    EXPECT_EQ(kke::pluralCategory("ar", 1), P::One);
    EXPECT_EQ(kke::pluralCategory("ar", 2), P::Two);
    EXPECT_EQ(kke::pluralCategory("ar", 7), P::Few);
    EXPECT_EQ(kke::pluralCategory("ar", 11), P::Many);
    EXPECT_EQ(kke::pluralCategory("ar", 100), P::Other);
    EXPECT_EQ(kke::pluralCategory("he", 2), P::Two);
    for (const char* lang : { "ja", "zh", "ko", "vi", "th", "id" }) EXPECT_EQ(kke::pluralCategory(lang, 1), P::Other) << lang;
    EXPECT_STREQ(kke::pluralName(P::Few), "few");
}

TEST(Localization, LooksKeysUpAndFillsPlaceholders) {
    Localization loc = sample();
    EXPECT_EQ(loc.tr("menu.start"), "Start");
    EXPECT_EQ(loc.tr("hello", { { "name", "Kees" } }), "Hello Kees!");
    EXPECT_EQ(loc.tr("hello"), "Hello {name}!") << "a placeholder nobody filled stays visible";
    EXPECT_EQ(loc.tr("braces"), "use {name} to name it");
    EXPECT_TRUE(loc.has("menu.quit"));
    EXPECT_FALSE(loc.has("menu.nothing"));
}

TEST(Localization, FallsBackFromRegionToLanguageToDefault) {
    Localization loc = sample();
    ASSERT_TRUE(loc.setLanguage("nl"));
    EXPECT_EQ(loc.tr("menu.start"), "Begin");
    EXPECT_EQ(loc.tr("menu.quit"), "Quit") << "Dutch has no 'quit': English fills in";
    ASSERT_TRUE(loc.setLanguage("pt-BR")) << "no pt-BR, but pt is there";
    EXPECT_EQ(loc.language(), "pt");
    EXPECT_EQ(loc.tr("menu.quit"), "Sair");
    EXPECT_EQ(loc.tr("only_en"), "English only");
    EXPECT_FALSE(loc.setLanguage("ja")) << "nothing for Japanese";
    EXPECT_EQ(loc.language(), "pt") << "a refused switch changes nothing";
}

TEST(Localization, UnknownKeysComeBackAsThemselvesAndAreListedOnce) {
    Localization loc = sample();
    EXPECT_EQ(loc.tr("menu.nope"), "menu.nope");
    EXPECT_EQ(loc.tr("menu.nope"), "menu.nope");
    EXPECT_EQ(loc.trn("items.nope", 3), "items.nope");
    ASSERT_EQ(loc.missing().size(), 2u);
    EXPECT_EQ(loc.missing()[0], "menu.nope");
    loc.clearMissing();
    EXPECT_TRUE(loc.missing().empty());
}

TEST(Localization, PluralsPickTheFormOfTheLanguageThatOwnsTheText) {
    Localization loc = sample();
    EXPECT_EQ(loc.trn("coins", 1), "1 coin");
    EXPECT_EQ(loc.trn("coins", 3), "3 coins");
    EXPECT_EQ(loc.trn("coins", 1234), "1,234 coins");
    loc.setLanguage("nl");
    EXPECT_EQ(loc.trn("coins", 1), "1 munt");
    EXPECT_EQ(loc.trn("coins", 0), "0 munten");
    EXPECT_EQ(loc.trn("coins", 1234), "1.234 munten") << "Dutch writes the number its own way";
    loc.addStrings("ru", nlohmann::json::parse(R"({ "apples": { "one": "{count} яблоко", "few": "{count} яблока", "many": "{count} яблок", "other": "{count} яблока" } })"));
    loc.setLanguage("ru");
    EXPECT_EQ(loc.trn("apples", 1), "1 яблоко");
    EXPECT_EQ(loc.trn("apples", 3), "3 яблока");
    EXPECT_EQ(loc.trn("apples", 11), "11 яблок");
    EXPECT_EQ(loc.trn("apples", 21), "21 яблоко");
    // A key Russian lacks falls back to English text, with English's rules.
    EXPECT_EQ(loc.trn("coins", 1), "1 coin");
    EXPECT_EQ(loc.trn("coins", 21), "21 coins");
}

TEST(Localization, ASwitchBumpsTheGenerationAndTellsListeners) {
    Localization loc = sample();
    const uint32_t before = loc.generation();
    int calls = 0;
    const int id = loc.addListener([&] { ++calls; });
    loc.setLanguage("nl");
    EXPECT_GT(loc.generation(), before);
    EXPECT_EQ(calls, 1);
    loc.setLanguage("nl"); // already there
    EXPECT_EQ(calls, 1);
    loc.removeListener(id);
    loc.setLanguage("en");
    EXPECT_EQ(calls, 1);
}

TEST(Localization, ListsTheDefaultFirstAndMatchesSystemLocales) {
    Localization loc = sample();
    const auto langs = loc.languages();
    ASSERT_EQ(langs.size(), 3u);
    EXPECT_EQ(langs[0].code, "en");
    EXPECT_EQ(langs[1].code, "nl");
    EXPECT_EQ(langs[1].name, "Nederlands");
    EXPECT_EQ(loc.bestMatch("nl_NL.UTF-8"), "nl");
    EXPECT_EQ(loc.bestMatch("nl-BE"), "nl");
    EXPECT_EQ(loc.bestMatch("pt_BR@euro"), "pt");
    EXPECT_EQ(loc.bestMatch("ja_JP.UTF-8"), "");
    EXPECT_EQ(loc.bestMatch("C"), "");
    loc.addStrings("pt-BR", nlohmann::json::object(), "Brasil");
    EXPECT_EQ(loc.bestMatch("pt_PT"), "pt") << "the exact base wins over a sibling region";
}

TEST(Localization, ReadsLanguageFilesInJsonAndYamlAndMergesMods) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "kke_loc_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "en.json") << R"({ "language": "en", "name": "English", "strings": { "a": "one", "b": "two" } })";
    std::ofstream(dir / "de.yml") << "language: de\nname: Deutsch\nstrings:\n  a: eins\n  coins:\n    one: '{count} Münze'\n    other: '{count} Münzen'\n";
    std::ofstream(dir / "ar.json") << R"({ "language": "ar", "name": "العربية", "font": "NotoSansArabic-Regular.ttf", "strings": { "a": "مرحبا" } })";
    std::ofstream(dir / "readme.txt") << "not a language";
    std::ofstream(dir / "broken.json") << R"({ "strings": {} })";
    Localization loc;
    EXPECT_EQ(loc.loadFolder(dir), 3);
    EXPECT_EQ(loc.languages().size(), 3u);
    loc.setLanguage("de");
    EXPECT_EQ(loc.tr("a"), "eins");
    EXPECT_EQ(loc.trn("coins", 5), "5 Münzen");
    EXPECT_FALSE(loc.rtl());
    loc.setLanguage("ar");
    EXPECT_TRUE(loc.rtl());
    EXPECT_EQ(loc.info("ar")->font, "NotoSansArabic-Regular.ttf");
    // A mod's file for English adds a string and replaces one.
    std::ofstream(dir / "mod.json") << R"({ "language": "en", "strings": { "b": "TWO", "c": "three" } })";
    std::string error;
    EXPECT_TRUE(loc.loadFile(dir / "mod.json", &error)) << error;
    loc.setLanguage("en");
    EXPECT_EQ(loc.tr("a"), "one");
    EXPECT_EQ(loc.tr("b"), "TWO");
    EXPECT_EQ(loc.tr("c"), "three");
    EXPECT_FALSE(loc.loadFile(dir / "broken.json", &error));
    EXPECT_NE(error.find("language"), std::string::npos);
    fs::remove_all(dir);
}

TEST(Localization, WritesNumbersTheWayEachLanguageDoes) {
    Localization loc;
    for (const char* code : { "en", "de", "fr", "ru", "pl", "es", "pt-BR", "pt-PT" }) loc.addStrings(code, nlohmann::json::object());
    auto fmt = [&](const char* lang, double v, int d = -1) {
        loc.setLanguage(lang);
        return loc.formatNumber(v, d);
    };
    EXPECT_EQ(fmt("en", 1234567.5), "1,234,567.5");
    EXPECT_EQ(fmt("en", -42), "-42");
    EXPECT_EQ(fmt("en", 3.14159, 2), "3.14");
    EXPECT_EQ(fmt("en", 12, 2), "12.00");
    EXPECT_EQ(fmt("de", 1234567.5), "1.234.567,5");
    EXPECT_EQ(fmt("fr", 1234.5), "1\xE2\x80\xAF" "234,5");
    EXPECT_EQ(fmt("ru", 1234567), "1\xC2\xA0" "234\xC2\xA0" "567");
    EXPECT_EQ(fmt("pl", 1234), "1234") << "Polish does not group four digits";
    EXPECT_EQ(fmt("pl", 12345), "12\xC2\xA0" "345");
    EXPECT_EQ(fmt("es", 1234), "1234");
    EXPECT_EQ(fmt("es", 12345.5), "12.345,5");
    EXPECT_EQ(fmt("pt-BR", 1234.5), "1.234,5");
    EXPECT_EQ(fmt("pt-PT", 1234.5), "1234,5");
    EXPECT_EQ(fmt("en", 0.1 + 0.2), "0.3") << "no float noise";
}

TEST(Arabic, JoinsLettersIntoTheirPresentationForms) {
    auto cps = [](const std::string& s) { return Localization::decode(s); };
    // beh beh beh: initial, medial, final.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8\xD8\xA8\xD8\xA8")), (std::u32string{ 0xFE91, 0xFE92, 0xFE90 }));
    // One letter alone is isolated.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8")), (std::u32string{ 0xFE8F }));
    // Alef joins only to what is before it: beh alef = initial beh, final alef.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8\xD8\xA7")), (std::u32string{ 0xFE91, 0xFE8E }));
    // Lam and alef make one ligature.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD9\x84\xD8\xA7")), (std::u32string{ 0xFEFB }));
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8\xD9\x84\xD8\xA7")), (std::u32string{ 0xFE91, 0xFEFC }));
    // A space breaks the joining; Latin text is untouched.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8 \xD8\xA8")), (std::u32string{ 0xFE8F, U' ', 0xFE8F }));
    EXPECT_EQ(Localization::shapeArabic("Hello"), "Hello");
    // A vowel mark does not break the joining of the letters round it.
    EXPECT_EQ(cps(Localization::shapeArabic("\xD8\xA8\xD9\x8E\xD8\xA8")), (std::u32string{ 0xFE91, 0x064E, 0xFE90 }));
}

TEST(Bidi, PutsRightToLeftTextInVisualOrder) {
    const std::string abg = "\xD7\x90\xD7\x91\xD7\x92"; // alef bet gimel
    const std::string gba = "\xD7\x92\xD7\x91\xD7\x90";
    EXPECT_EQ(Localization::visualOrder(abg, true), gba);
    EXPECT_EQ(Localization::visualOrder(abg, false), gba) << "a Hebrew word reads right to left in any paragraph";
    EXPECT_EQ(Localization::visualOrder("plain text 123", true), "plain text 123") << "no RTL letters, nothing to do";
}

TEST(Bidi, KeepsLatinWordsAndNumbersReadable) {
    const std::string abg = "\xD7\x90\xD7\x91\xD7\x92";
    const std::string gba = "\xD7\x92\xD7\x91\xD7\x90";
    EXPECT_EQ(Localization::visualOrder("abc " + abg, false), "abc " + gba);
    EXPECT_EQ(Localization::visualOrder(abg + " abc", true), "abc " + gba);
    // A number in a right-to-left line stays left to right.
    const std::string ab = "\xD7\x90\xD7\x91", ba = "\xD7\x91\xD7\x90";
    EXPECT_EQ(Localization::visualOrder(ab + " 12", true), "12 " + ba);
    EXPECT_EQ(Localization::visualOrder(ab + " 1,234.5", true), "1,234.5 " + ba) << "separators inside a number belong to it";
    // Brackets turn round in a right-to-left run.
    EXPECT_EQ(Localization::visualOrder("(" + ab + ")", true), "(" + ba + ")");
}

TEST(Bidi, EveryLineIsOrderedOnItsOwn) {
    const std::string ab = "\xD7\x90\xD7\x91", ba = "\xD7\x91\xD7\x90";
    const std::string gd = "\xD7\x92\xD7\x93", dg = "\xD7\x93\xD7\x92";
    EXPECT_EQ(Localization::visualOrder(ab + "\n" + gd, true), ba + "\n" + dg);
}

TEST(Bidi, ShapesArabicBeforeReordering) {
    // beh alef (initial beh + final alef) read right to left: alef first on screen.
    const auto out = Localization::decode(Localization::visualOrder("\xD8\xA8\xD8\xA7", true));
    EXPECT_EQ(out, (std::u32string{ 0xFE8E, 0xFE91 }));
}

TEST(Utf8, RoundTripsAndSurvivesBrokenBytes) {
    const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    EXPECT_EQ(Localization::encode(Localization::decode(text)), text);
    EXPECT_EQ(Localization::decode(std::string("a\xFF" "b")).size(), 3u);
    EXPECT_EQ(Localization::decode(std::string("\xE2\x82")).size(), 1u) << "a cut-off sequence is one bad character, not a crash";
    EXPECT_EQ(Localization::decode(std::string("\xC0\x80")).size(), 2u) << "an overlong NUL is two bad bytes";
    EXPECT_EQ(Localization::decode(std::string("\xED\xA0\x80")).size(), 1u) << "a UTF-16 surrogate is not a character";
    EXPECT_EQ(Localization::decode(std::string("\xF4\x90\x80\x80")).size(), 1u) << "past U+10FFFF";
    EXPECT_EQ(Localization::decode(std::string("\xE2\x82" "a")), (std::u32string{ 0xFFFD, U'a' })) << "the next good byte survives";
}
