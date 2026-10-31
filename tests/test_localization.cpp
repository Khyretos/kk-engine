#include "kke/Localization.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

// Translated text (docs/LOCALIZATION.md): tables, the fallback chain,
// plural rules, numbers, and what a translator gets wrong.

using kke::Localization;

namespace {

const char* kEnglish = R"({
  "language": "en", "name": "English",
  "strings": {
    "menu": { "start": "Start", "quit": "Quit" },
    "greeting": "Hello {name}!",
    "braces": "Use {{curly}} braces, {name}",
    "coins": { "one": "{n} coin", "other": "{n} coins" },
    "pronoun": { "male": "his", "female": "her", "other": "their" },
    "only.english": "Only in English"
  } })";

const char* kDutch = R"({
  "language": "nl", "name": "Nederlands",
  "strings": {
    "menu": { "start": "Start", "quit": "Afsluiten" },
    "greeting": "Hallo {name}!",
    "coins": { "one": "{n} munt", "other": "{n} munten" }
  } })";

void loadEnglish(Localization& l) {
    std::string error;
    EXPECT_TRUE(l.loadJson(kEnglish, &error)) << error;
}

} // namespace

TEST(Localization, LoadsNestedKeysAsDottedOnes) {
    Localization l;
    ASSERT_TRUE(l.loadJson(kEnglish));
    EXPECT_EQ(l.tr("menu.start"), "Start");
    EXPECT_EQ(l.tr("menu.quit"), "Quit");
    EXPECT_TRUE(l.has("menu.start"));
    EXPECT_TRUE(l.has("only.english"));
    EXPECT_FALSE(l.has("menu"));
    ASSERT_EQ(l.languages().size(), 1u);
    EXPECT_EQ(l.languages()[0].code, "en");
    EXPECT_EQ(l.languages()[0].name, "English");
    EXPECT_EQ(l.languages()[0].strings, 7u); // menu.start, menu.quit, greeting, braces, coins, pronoun, only.english (a set of forms is one string)
}

TEST(Localization, BadFilesSayWhatIsWrongAndChangeNothing) {
    Localization l;
    std::string error;
    EXPECT_FALSE(l.loadJson("{ not json", &error));
    EXPECT_NE(error.find("not JSON"), std::string::npos);
    EXPECT_FALSE(l.loadJson(R"({"strings": {}})", &error));
    EXPECT_NE(error.find("language"), std::string::npos);
    EXPECT_FALSE(l.loadJson(R"({"language": "en"})", &error));
    EXPECT_NE(error.find("strings"), std::string::npos);
    EXPECT_FALSE(l.loadJson(R"({"language": "en", "strings": {"a": 5}})", &error));
    EXPECT_NE(error.find("a is not text"), std::string::npos);
    EXPECT_FALSE(l.loadJson(R"({"language": "en", "strings": {"coins": {"one": "{n} coin", "few": "x"}}})", &error));
    EXPECT_NE(error.find("coins"), std::string::npos); // forms without "other"
    EXPECT_NE(error.find("other"), std::string::npos);
    EXPECT_FALSE(l.loadJson(R"({"language": "en", "strings": {"ok": "fine", "bad": 1}})", &error));
    EXPECT_TRUE(l.languages().empty()); // the good "ok" was not kept
    EXPECT_FALSE(l.loadFile("/no/such/file.json", &error));
    EXPECT_NE(error.find("can't read"), std::string::npos);
}

TEST(Localization, PlaceholdersAreFilledAndBracesCanBeEscaped) {
    Localization l;
    loadEnglish(l);
    EXPECT_EQ(l.tr("greeting", { { "name", "Kees" } }), "Hello Kees!");
    EXPECT_EQ(l.tr("greeting"), "Hello {name}!"); // a missing argument shows, so it gets noticed
    EXPECT_EQ(l.tr("braces", { { "name", "x" } }), "Use {curly} braces, x");
}

TEST(Localization, AMissingKeyShowsItselfAndIsRemembered) {
    Localization l;
    loadEnglish(l);
    EXPECT_EQ(l.tr("no.such.key"), "no.such.key");
    EXPECT_EQ(l.plural("also.missing", 3), "also.missing");
    EXPECT_EQ(l.select("and.this", "male"), "and.this");
    EXPECT_EQ(l.missingKeys().size(), 3u);
    l.forgetMissing();
    EXPECT_TRUE(l.missingKeys().empty());
}

TEST(Localization, ALanguageFallsBackToTheDefaultOneWordByWord) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(kDutch));
    ASSERT_TRUE(l.setLanguage("nl"));
    EXPECT_EQ(l.tr("menu.quit"), "Afsluiten");
    EXPECT_EQ(l.tr("only.english"), "Only in English"); // not translated yet
    EXPECT_TRUE(l.missingKeys().empty());               // found in the default language: not missing
    EXPECT_EQ(l.language(), "nl");
}

TEST(Localization, ARegionFindsItsLanguageAndNamesAreNormalised) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "pt", "strings": {"hi": "Olá"}})"));
    ASSERT_TRUE(l.loadJson(R"({"language": "pt_br", "strings": {"bye": "Tchau"}})")); // pt-BR
    ASSERT_TRUE(l.setLanguage("PT-br"));
    EXPECT_EQ(l.language(), "pt-BR");
    EXPECT_EQ(l.tr("bye"), "Tchau");
    EXPECT_EQ(l.tr("hi"), "Olá"); // pt-BR falls back to pt before en
    ASSERT_TRUE(l.setLanguage("pt-PT"));
    EXPECT_EQ(l.language(), "pt"); // no pt-PT: the language itself
    EXPECT_FALSE(l.setLanguage("xx"));
    EXPECT_EQ(l.language(), "pt"); // unchanged
}

TEST(Localization, AnExplicitFallbackComesBeforeTheDefault) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "de", "strings": {"only.german": "Nur Deutsch"}})"));
    ASSERT_TRUE(l.loadJson(R"({"language": "de-AT", "fallback": "de", "strings": {}})"));
    ASSERT_TRUE(l.setLanguage("de-AT"));
    EXPECT_EQ(l.tr("only.german"), "Nur Deutsch");
    EXPECT_EQ(l.tr("menu.start"), "Start");
}

TEST(Localization, FilesOfOneLanguageAddToEachOtherAndTheLaterWins) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "en", "strings": {"menu": {"start": "Play"}, "game.title": "My Game"}})"));
    EXPECT_EQ(l.tr("menu.start"), "Play");     // a game overrides the engine's word
    EXPECT_EQ(l.tr("menu.quit"), "Quit");      // the rest stays
    EXPECT_EQ(l.tr("game.title"), "My Game");
    EXPECT_EQ(l.languages().size(), 1u);
}

TEST(Localization, PluralFormsFollowTheLanguagesRules) {
    Localization l;
    loadEnglish(l);
    EXPECT_EQ(l.plural("coins", 1), "1 coin");
    EXPECT_EQ(l.plural("coins", 0), "0 coins");
    EXPECT_EQ(l.plural("coins", 2), "2 coins");
    EXPECT_EQ(l.plural("coins", 1.5), "1.5 coins");
    EXPECT_EQ(l.plural("coins", 1234567), "1,234,567 coins");
    ASSERT_TRUE(l.loadJson(kDutch));
    ASSERT_TRUE(l.setLanguage("nl"));
    EXPECT_EQ(l.plural("coins", 1), "1 munt");
    EXPECT_EQ(l.plural("coins", 1234), "1.234 munten"); // and the number is written the Dutch way
    EXPECT_EQ(l.plural("greeting", 5, { { "name", "Kees" } }), "Hallo Kees!"); // a plain string needs no forms
}

TEST(Localization, ThePluralCategoriesMatchCldr) {
    auto cat = [](const char* lang, double n) { return Localization::pluralCategory(lang, n); };
    EXPECT_EQ(cat("en", 1), "one");
    EXPECT_EQ(cat("en", 0), "other");
    EXPECT_EQ(cat("en", 2), "other");
    EXPECT_EQ(cat("en", 1.5), "other");
    EXPECT_EQ(cat("fr", 0), "one");
    EXPECT_EQ(cat("fr", 1.5), "one");
    EXPECT_EQ(cat("fr", 2), "other");
    EXPECT_EQ(cat("pt-BR", 0), "one");
    EXPECT_EQ(cat("pt-PT", 0), "other");
    EXPECT_EQ(cat("es", 1), "one");
    EXPECT_EQ(cat("ja", 1), "other");
    EXPECT_EQ(cat("zh", 1), "other");
    EXPECT_EQ(cat("tr", 1), "one");
    EXPECT_EQ(cat("hi", 0), "one");
    // Russian and its relatives
    EXPECT_EQ(cat("ru", 1), "one");
    EXPECT_EQ(cat("ru", 21), "one");
    EXPECT_EQ(cat("ru", 11), "many");
    EXPECT_EQ(cat("ru", 2), "few");
    EXPECT_EQ(cat("ru", 24), "few");
    EXPECT_EQ(cat("ru", 12), "many");
    EXPECT_EQ(cat("ru", 5), "many");
    EXPECT_EQ(cat("ru", 20), "many");
    EXPECT_EQ(cat("ru", 1.5), "other");
    EXPECT_EQ(cat("uk", 22), "few");
    // Polish
    EXPECT_EQ(cat("pl", 1), "one");
    EXPECT_EQ(cat("pl", 2), "few");
    EXPECT_EQ(cat("pl", 12), "many");
    EXPECT_EQ(cat("pl", 22), "few");
    EXPECT_EQ(cat("pl", 5), "many");
    EXPECT_EQ(cat("pl", 0), "many");
    // Czech and Slovak
    EXPECT_EQ(cat("cs", 1), "one");
    EXPECT_EQ(cat("cs", 3), "few");
    EXPECT_EQ(cat("cs", 5), "other");
    EXPECT_EQ(cat("cs", 1.5), "many");
    // Arabic: all six
    EXPECT_EQ(cat("ar", 0), "zero");
    EXPECT_EQ(cat("ar", 1), "one");
    EXPECT_EQ(cat("ar", 2), "two");
    EXPECT_EQ(cat("ar", 3), "few");
    EXPECT_EQ(cat("ar", 10), "few");
    EXPECT_EQ(cat("ar", 11), "many");
    EXPECT_EQ(cat("ar", 99), "many");
    EXPECT_EQ(cat("ar", 100), "other");
    EXPECT_EQ(cat("ar", 103), "few");
    // Hebrew, Romanian, Slovenian, Latvian, Welsh, Irish, Lithuanian, Croatian
    EXPECT_EQ(cat("he", 1), "one");
    EXPECT_EQ(cat("he", 2), "two");
    EXPECT_EQ(cat("he", 3), "other");
    EXPECT_EQ(cat("ro", 1), "one");
    EXPECT_EQ(cat("ro", 0), "few");
    EXPECT_EQ(cat("ro", 119), "few");
    EXPECT_EQ(cat("ro", 20), "other");
    EXPECT_EQ(cat("sl", 101), "one");
    EXPECT_EQ(cat("sl", 102), "two");
    EXPECT_EQ(cat("sl", 4), "few");
    EXPECT_EQ(cat("lv", 0), "zero");
    EXPECT_EQ(cat("lv", 21), "one");
    EXPECT_EQ(cat("lv", 2), "other");
    EXPECT_EQ(cat("cy", 0), "zero");
    EXPECT_EQ(cat("cy", 6), "many");
    EXPECT_EQ(cat("ga", 8), "many");
    EXPECT_EQ(cat("lt", 21), "one");
    EXPECT_EQ(cat("lt", 5), "few");
    EXPECT_EQ(cat("lt", 11), "other");
    EXPECT_EQ(cat("hr", 21), "one");
    EXPECT_EQ(cat("hr", 3), "few");
    EXPECT_EQ(cat("hr", 5), "other");
    EXPECT_EQ(cat("da", 0.5), "one"); // Danish: n = 1, or a fraction below 2
    EXPECT_EQ(cat("xx-unknown", 1), "one");
}

TEST(Localization, AFormMissingFromATranslationFallsBackToOther) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "ru", "strings": {"coins": {"one": "{n} монета", "other": "{n} монет"}}})"));
    ASSERT_TRUE(l.setLanguage("ru"));
    EXPECT_EQ(l.plural("coins", 1), "1 монета");
    EXPECT_EQ(l.plural("coins", 3), "3 монет"); // "few" is not written: "other"
}

TEST(Localization, SelectPicksByWord) {
    Localization l;
    loadEnglish(l);
    EXPECT_EQ(l.select("pronoun", "male"), "his");
    EXPECT_EQ(l.select("pronoun", "female"), "her");
    EXPECT_EQ(l.select("pronoun", "robot"), "their");
}

TEST(Localization, NumbersAreWrittenTheWayTheLanguageDoes) {
    Localization l;
    loadEnglish(l);
    EXPECT_EQ(l.number(1234567.891), "1,234,567.891");
    EXPECT_EQ(l.number(1234567), "1,234,567");
    EXPECT_EQ(l.number(-1234.5), "-1,234.5");
    EXPECT_EQ(l.number(3.14159, 2), "3.14");
    EXPECT_EQ(l.number(5, 2), "5.00");
    EXPECT_EQ(l.number(0.5), "0.5");
    EXPECT_EQ(l.number(-0.0001), "0"); // not "-0"
    ASSERT_TRUE(l.loadJson(R"({"language": "de", "strings": {}})"));
    ASSERT_TRUE(l.loadJson(R"({"language": "fr", "strings": {}})"));
    ASSERT_TRUE(l.loadJson(R"({"language": "ru", "strings": {}})"));
    ASSERT_TRUE(l.setLanguage("de"));
    EXPECT_EQ(l.number(1234567.891), "1.234.567,891");
    ASSERT_TRUE(l.setLanguage("fr"));
    EXPECT_EQ(l.number(1234567.5), "1\xE2\x80\xAF" "234\xE2\x80\xAF" "567,5");
    ASSERT_TRUE(l.setLanguage("ru"));
    EXPECT_EQ(l.number(1234.5), "1\xC2\xA0" "234,5");
    ASSERT_TRUE(l.loadJson(R"({"language": "ar", "digits": "arabic-indic", "strings": {}})"));
    ASSERT_TRUE(l.setLanguage("ar"));
    EXPECT_EQ(l.number(1234.5), "\u0661\u066C\u0662\u0663\u0664\u066B\u0665");
    ASSERT_TRUE(l.loadJson(R"({"language": "xx", "numbers": {"decimal": "'", "group": "_"}, "strings": {}})"));
    ASSERT_TRUE(l.setLanguage("xx"));
    EXPECT_EQ(l.number(1234.5), "1_234'5"); // a file can say its own
}

TEST(Localization, RightToLeftLanguagesAreKnownAndShownReversed) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "he", "name": "עברית", "strings": {"hi": "\u05E9\u05DC\u05D5\u05DD"}})"));
    EXPECT_FALSE(l.rtl());
    EXPECT_EQ(l.display("abc"), "abc");
    ASSERT_TRUE(l.setLanguage("he"));
    EXPECT_TRUE(l.rtl()); // he is right to left without saying so
    EXPECT_EQ(l.display(l.tr("hi")), "\u05DD\u05D5\u05DC\u05E9");
    ASSERT_TRUE(l.setLanguage("en"));
    EXPECT_FALSE(l.rtl());
    EXPECT_EQ(l.display("\u05E9\u05DC\u05D5\u05DD"), "\u05DD\u05D5\u05DC\u05E9"); // right-to-left letters are shown right in any language
}

TEST(Localization, ThePseudoLanguageStretchesAndKeepsPlaceholders) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.setLanguage("pseudo"));
    const std::string s = l.tr("greeting", { { "name", "Kees" } });
    EXPECT_EQ(s.front(), '[');
    EXPECT_EQ(s.back(), ']');
    EXPECT_NE(s.find("Kees"), std::string::npos); // the placeholder was filled after
    EXPECT_NE(s.find("~"), std::string::npos);    // padding
    EXPECT_EQ(s.find("Hello"), std::string::npos); // accented
    EXPECT_GT(s.size(), std::string("Hello Kees!").size());
    EXPECT_EQ(Localization::pseudo("Hi {name}"), "[H\xC3\xAE {name}~]");
    EXPECT_NE(l.plural("coins", 2).find("2"), std::string::npos);
}

TEST(Localization, ListenersHearOnlyRealChanges) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(kDutch));
    int heard = 0;
    const int id = l.onChange([&] { ++heard; });
    ASSERT_TRUE(l.setLanguage("nl"));
    EXPECT_EQ(heard, 1);
    ASSERT_TRUE(l.setLanguage("nl")); // same language: nothing changed
    EXPECT_EQ(heard, 1);
    EXPECT_FALSE(l.setLanguage("xx"));
    EXPECT_EQ(heard, 1);
    l.removeOnChange(id);
    ASSERT_TRUE(l.setLanguage("en"));
    EXPECT_EQ(heard, 1);
}

TEST(Localization, ACallbackMayCallBackIntoTheLocalizer) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(kDutch));
    std::string seen;
    l.onChange([&] { seen = l.tr("menu.quit"); });
    ASSERT_TRUE(l.setLanguage("nl"));
    EXPECT_EQ(seen, "Afsluiten"); // would deadlock if it ran under the lock
}

TEST(Localization, PicksTheBestLanguageFromAPreferenceList) {
    using L = Localization;
    EXPECT_EQ(L::pick({ "nl-NL", "en-US" }, { "en", "nl" }, "en"), "nl");
    EXPECT_EQ(L::pick({ "de-AT" }, { "de", "en" }, "en"), "de");
    EXPECT_EQ(L::pick({ "pt-BR" }, { "pt-BR", "pt" }, "en"), "pt-BR");
    EXPECT_EQ(L::pick({ "ja" }, { "en", "nl" }, "en"), "en");
    EXPECT_EQ(L::pick({ "fr_CA", "nl" }, { "nl" }, "en"), "nl");
    EXPECT_EQ(L::pick({}, { "nl" }, "en"), "en");
    EXPECT_NO_THROW(L::systemLanguages());
}

TEST(Localization, ReportsWhatATranslatorMissedOrBroke) {
    Localization l;
    loadEnglish(l);
    ASSERT_TRUE(l.loadJson(R"({"language": "nl", "strings": {
        "menu": {"start": "Start"},
        "greeting": "Hallo!",
        "braces": "Gebruik {{krullen}}, {naam}",
        "coins": {"one": "{n} munt", "other": "{n} munten"}}})"));
    const auto missing = l.untranslated("nl");
    EXPECT_EQ(missing.size(), 3u); // menu.quit, pronoun, only.english
    EXPECT_NE(std::find(missing.begin(), missing.end(), "menu.quit"), missing.end());
    const auto problems = l.placeholderProblems("nl");
    ASSERT_EQ(problems.size(), 3u);
    EXPECT_NE(std::find(problems.begin(), problems.end(), "greeting: lost {name}"), problems.end());
    EXPECT_NE(std::find(problems.begin(), problems.end(), "braces: lost {name}"), problems.end());
    EXPECT_NE(std::find(problems.begin(), problems.end(), "braces: invented {naam}"), problems.end());
    EXPECT_TRUE(l.untranslated("xx").empty());
}

TEST(Localization, LoadsAFolderAndReportsTheBadFiles) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "kke_locale_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "en.json") << kEnglish;
    std::ofstream(dir / "nl.json") << kDutch;
    std::ofstream(dir / "broken.json") << "{ nope";
    std::ofstream(dir / "readme.txt") << "not a locale";
    Localization l;
    std::string error;
    EXPECT_EQ(l.loadFolder(dir.string(), &error), 2);
    EXPECT_NE(error.find("broken.json"), std::string::npos);
    EXPECT_EQ(l.languages().size(), 2u);
    EXPECT_EQ(l.loadFolder((dir / "nope").string(), &error), 0);
    fs::remove_all(dir);
}

TEST(Localization, ClearForgetsEverything) {
    Localization l;
    loadEnglish(l);
    l.tr("missing");
    l.clear();
    EXPECT_TRUE(l.languages().empty());
    EXPECT_TRUE(l.missingKeys().empty());
    EXPECT_EQ(l.tr("menu.start"), "menu.start");
}
