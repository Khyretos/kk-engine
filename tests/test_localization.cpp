// Localization (kke/Localization.h, ENG-F84, docs/LOCALIZATION.md).
#include "kke/Localization.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace kke;
using nlohmann::json;
using P = PluralCategory;

namespace {

std::string writeFile(const std::string& dir, const std::string& name, const std::string& text) {
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/" + name;
    std::ofstream(path) << text;
    return path;
}

Localizer sample() {
    Localizer loc;
    loc.addTable("en", json::parse(R"({
        "menu": { "play": "Play", "quit": "Quit" },
        "greeting": "Hello, {name}!",
        "braces": "Use {{x}} for {name}",
        "items": { "zero": "No items", "one": "{n} item", "other": "{n} items" },
        "only.en": "English only" })"));
    loc.addTable("de", json::parse(R"({
        "menu": { "play": "Spielen" },
        "items": { "one": "{n} Ding", "other": "{n} Dinge" } })"));
    loc.addTable("pt", json::parse(R"({ "menu": { "play": "Jogar", "quit": "Sair" }, "items": { "one": "{n} item", "other": "{n} itens" } })"));
    loc.addTable("pt-BR", json::parse(R"({ "menu": { "play": "Jogar [BR]" } })"));
    loc.addTable("ru", json::parse(R"({ "items": { "one": "{n} предмет", "few": "{n} предмета", "many": "{n} предметов", "other": "{n} предмета" } })"));
    return loc;
}

} // namespace

TEST(Plural, EnglishAndTheOthersWithOneAndOther) {
    EXPECT_EQ(pluralCategory("en", 1), P::One);
    EXPECT_EQ(pluralCategory("en", 0), P::Other);
    EXPECT_EQ(pluralCategory("en", 2), P::Other);
    EXPECT_EQ(pluralCategory("en", 1.5), P::Other) << "a fraction is never 'one' in English";
    EXPECT_EQ(pluralCategory("de", 1), P::One);
    EXPECT_EQ(pluralCategory("es", 1), P::One);
    EXPECT_EQ(pluralCategory("it", 21), P::Other);
    EXPECT_EQ(pluralCategory("tr", 1), P::One);
}

TEST(Plural, FrenchAndPortugueseCountZeroAsOne) {
    EXPECT_EQ(pluralCategory("fr", 0), P::One);
    EXPECT_EQ(pluralCategory("fr", 1), P::One);
    EXPECT_EQ(pluralCategory("fr", 1.5), P::One);
    EXPECT_EQ(pluralCategory("fr", 2), P::Other);
    EXPECT_EQ(pluralCategory("pt-BR", 0), P::One);
    EXPECT_EQ(pluralCategory("pt-PT", 0), P::Other);
    EXPECT_EQ(pluralCategory("pt-PT", 1), P::One);
    EXPECT_EQ(pluralCategory("hi", 0), P::One);
}

TEST(Plural, SlavicLanguagesHaveFewAndMany) {
    for (const char* lang : { "ru", "uk" }) {
        EXPECT_EQ(pluralCategory(lang, 1), P::One) << lang;
        EXPECT_EQ(pluralCategory(lang, 21), P::One) << lang;
        EXPECT_EQ(pluralCategory(lang, 2), P::Few) << lang;
        EXPECT_EQ(pluralCategory(lang, 24), P::Few) << lang;
        EXPECT_EQ(pluralCategory(lang, 5), P::Many) << lang;
        EXPECT_EQ(pluralCategory(lang, 11), P::Many) << lang;
        EXPECT_EQ(pluralCategory(lang, 12), P::Many) << lang;
        EXPECT_EQ(pluralCategory(lang, 0), P::Many) << lang;
        EXPECT_EQ(pluralCategory(lang, 1.5), P::Other) << lang;
    }
    EXPECT_EQ(pluralCategory("pl", 1), P::One);
    EXPECT_EQ(pluralCategory("pl", 22), P::Few);
    EXPECT_EQ(pluralCategory("pl", 5), P::Many);
    EXPECT_EQ(pluralCategory("pl", 21), P::Many) << "Polish 21 is 'many', unlike Russian";
    EXPECT_EQ(pluralCategory("cs", 3), P::Few);
    EXPECT_EQ(pluralCategory("cs", 5), P::Other);
    EXPECT_EQ(pluralCategory("cs", 1.5), P::Many);
    EXPECT_EQ(pluralCategory("hr", 21), P::One);
    EXPECT_EQ(pluralCategory("hr", 3), P::Few);
}

TEST(Plural, ArabicHasAllSix) {
    EXPECT_EQ(pluralCategory("ar", 0), P::Zero);
    EXPECT_EQ(pluralCategory("ar", 1), P::One);
    EXPECT_EQ(pluralCategory("ar", 2), P::Two);
    EXPECT_EQ(pluralCategory("ar", 3), P::Few);
    EXPECT_EQ(pluralCategory("ar", 10), P::Few);
    EXPECT_EQ(pluralCategory("ar", 11), P::Many);
    EXPECT_EQ(pluralCategory("ar", 99), P::Many);
    EXPECT_EQ(pluralCategory("ar", 100), P::Other);
    EXPECT_EQ(pluralCategory("ar", 103), P::Few);
}

TEST(Plural, OtherLanguages) {
    EXPECT_EQ(pluralCategory("ja", 1), P::Other) << "no plural at all";
    EXPECT_EQ(pluralCategory("zh-Hans", 1), P::Other);
    EXPECT_EQ(pluralCategory("ko", 5), P::Other);
    EXPECT_EQ(pluralCategory("he", 1), P::One);
    EXPECT_EQ(pluralCategory("he", 2), P::Two);
    EXPECT_EQ(pluralCategory("he", 3), P::Other);
    EXPECT_EQ(pluralCategory("ro", 1), P::One);
    EXPECT_EQ(pluralCategory("ro", 0), P::Few);
    EXPECT_EQ(pluralCategory("ro", 19), P::Few);
    EXPECT_EQ(pluralCategory("ro", 20), P::Other);
    EXPECT_EQ(pluralCategory("sl", 101), P::One);
    EXPECT_EQ(pluralCategory("sl", 102), P::Two);
    EXPECT_EQ(pluralCategory("sl", 103), P::Few);
    EXPECT_EQ(pluralCategory("lt", 1), P::One);
    EXPECT_EQ(pluralCategory("lt", 5), P::Few);
    EXPECT_EQ(pluralCategory("lt", 11), P::Other);
    EXPECT_EQ(pluralCategory("lv", 0), P::Zero);
    EXPECT_EQ(pluralCategory("lv", 21), P::One);
    EXPECT_EQ(pluralCategory("cy", 6), P::Many);
    EXPECT_EQ(pluralCategory("ga", 8), P::Many);
    EXPECT_EQ(pluralCategory("da", 0.5), P::One);
    EXPECT_EQ(pluralCategory("xx", 1), P::Other) << "an unknown language has just 'other'";
    EXPECT_STREQ(pluralName(P::Few), "few");
}

TEST(Language, NamesAreNormalisedAndRtlKnown) {
    EXPECT_EQ(baseLanguage("pt_br"), "pt");
    EXPECT_EQ(baseLanguage("ZH-hant-tw"), "zh");
    EXPECT_EQ(baseLanguage("en"), "en");
    EXPECT_TRUE(isRtlLanguage("ar"));
    EXPECT_TRUE(isRtlLanguage("ar-EG"));
    EXPECT_TRUE(isRtlLanguage("he"));
    EXPECT_TRUE(isRtlLanguage("fa-IR"));
    EXPECT_FALSE(isRtlLanguage("en"));
    EXPECT_FALSE(isRtlLanguage("de"));
}

TEST(Numbers, FormattedTheWayTheLanguageWritesThem) {
    EXPECT_EQ(formatNumber(1234567.891, numberFormat("en"), 2), "1,234,567.89");
    EXPECT_EQ(formatNumber(1234567.891, numberFormat("de"), 2), "1.234.567,89");
    EXPECT_EQ(formatNumber(1234567.891, numberFormat("fr"), 2), std::string("1\xE2\x80\xAF") + "234\xE2\x80\xAF" + "567,89");
    EXPECT_EQ(formatNumber(1234567.891, numberFormat("ru"), 2), std::string("1\xC2\xA0") + "234\xC2\xA0" + "567,89");
    EXPECT_EQ(formatNumber(999, numberFormat("en"), 0), "999");
    EXPECT_EQ(formatNumber(1000, numberFormat("en"), -1), "1,000");
    EXPECT_EQ(formatNumber(0.5, numberFormat("en"), -1), "0.5");
    EXPECT_EQ(formatNumber(2.0, numberFormat("de"), 1), "2,0");
    EXPECT_EQ(formatNumber(-1234.5, numberFormat("en"), 1), "-1,234.5");
    EXPECT_EQ(formatNumber(-0.0001, numberFormat("en"), 2), "0.00") << "no minus sign on a zero";
    EXPECT_EQ(formatNumber(std::nan(""), numberFormat("en")), "NaN");
    EXPECT_EQ(formatNumber(0.1 + 0.2, numberFormat("en"), -1), "0.3") << "shortest exact form, no float noise";
}

TEST(Localizer, FindsTextInTheLanguageThenItsBaseThenTheFallback) {
    Localizer loc = sample();
    EXPECT_TRUE(loc.setLanguage("pt-BR"));
    EXPECT_EQ(loc.text("menu.play"), "Jogar [BR]") << "the regional table first";
    EXPECT_EQ(loc.text("menu.quit"), "Sair") << "then pt";
    EXPECT_EQ(loc.text("only.en"), "English only") << "then the fallback language";
    EXPECT_TRUE(loc.has("menu.quit"));
    EXPECT_FALSE(loc.has("nothing.here"));
    EXPECT_TRUE(loc.setLanguage("de"));
    EXPECT_EQ(loc.text("menu.play"), "Spielen");
    EXPECT_EQ(loc.text("menu.quit"), "Quit") << "German has no 'quit': English fills in";
}

TEST(Localizer, ALanguageWithOnlyABaseTableIsAccepted) {
    Localizer loc = sample();
    EXPECT_TRUE(loc.setLanguage("de-AT")) << "there is a 'de'";
    EXPECT_EQ(loc.language(), "de-AT");
    EXPECT_EQ(loc.text("menu.play"), "Spielen");
    EXPECT_FALSE(loc.setLanguage("xx"));
    EXPECT_EQ(loc.language(), "de-AT") << "an unknown language leaves it as it was";
    EXPECT_TRUE(loc.setLanguage("pt_br")) << "underscores are fine";
    EXPECT_EQ(loc.language(), "pt-BR");
}

TEST(Localizer, AMissingKeyShowsItselfAndIsRememberedOnce) {
    Localizer loc = sample();
    EXPECT_EQ(loc.text("menu.options"), "menu.options");
    EXPECT_EQ(loc.text("menu.options"), "menu.options");
    EXPECT_EQ(loc.plural("things", 2), "things");
    ASSERT_EQ(loc.missing().size(), 2u);
    EXPECT_EQ(loc.missing()[0], "menu.options");
    EXPECT_EQ(loc.missing()[1], "things");
    loc.clearMissing();
    EXPECT_TRUE(loc.missing().empty());
}

TEST(Localizer, PlaceholdersAndBraces) {
    Localizer loc = sample();
    EXPECT_EQ(loc.text("greeting", { { "name", "Ana" } }), "Hello, Ana!");
    EXPECT_EQ(loc.text("greeting"), "Hello, {name}!") << "an argument nobody gave stays visible";
    EXPECT_EQ(loc.text("braces", { { "name", "Bo" } }), "Use {x} for Bo");
    std::vector<std::string> unfilled;
    EXPECT_EQ(Localizer::format("{a} and {b} and {a}", { { "a", "1" } }, &unfilled), "1 and {b} and 1");
    ASSERT_EQ(unfilled.size(), 1u);
    EXPECT_EQ(unfilled[0], "b");
    EXPECT_EQ(Localizer::format("{0}-{1}", { { "0", "x" }, { "1", "y" } }), "x-y");
    EXPECT_EQ(Localizer::format("open {brace", {}), "open {brace");
    EXPECT_EQ(Localizer::format("a }} b", {}), "a } b");
}

TEST(Localizer, PluralPicksTheFormForTheNumber) {
    Localizer loc = sample();
    EXPECT_EQ(loc.plural("items", 1), "1 item");
    EXPECT_EQ(loc.plural("items", 3), "3 items");
    EXPECT_EQ(loc.plural("items", 1234), "1,234 items") << "{n} is written the way the language writes numbers";
    EXPECT_EQ(loc.plural("items", 0), "No items") << "key.zero is used for 0 in any language";
    loc.setLanguage("de");
    EXPECT_EQ(loc.plural("items", 1), "1 Ding");
    EXPECT_EQ(loc.plural("items", 1234), "1.234 Dinge");
    EXPECT_EQ(loc.plural("items", 0), "No items") << "the zero form of the fallback language";
    loc.setLanguage("ru");
    EXPECT_EQ(loc.plural("items", 1), "1 предмет");
    EXPECT_EQ(loc.plural("items", 3), "3 предмета");
    EXPECT_EQ(loc.plural("items", 5), "5 предметов");
    EXPECT_EQ(loc.plural("items", 21), "21 предмет");
    EXPECT_EQ(loc.plural("items", 2, { { "n", "two" } }), "two предмета") << "an argument called n wins";
}

TEST(Localizer, PluralFallsBackToOtherAndToThePlainKey) {
    Localizer loc;
    loc.addTable("en", json::parse(R"({ "apples": { "other": "{n} apples" }, "pears": "{n} pears" })"));
    EXPECT_EQ(loc.plural("apples", 1), "1 apples") << "no 'one' form: 'other'";
    EXPECT_EQ(loc.plural("pears", 4), "4 pears") << "no forms at all: the plain key";
}

TEST(Localizer, TablesAreCheckedAndMerged) {
    Localizer loc;
    std::string error;
    EXPECT_FALSE(loc.addTable("en", json::parse(R"({ "a": 5 })"), &error));
    EXPECT_NE(error.find("'a' must be text"), std::string::npos) << error;
    EXPECT_FALSE(loc.addTable("en", json::parse(R"([1, 2])"), &error));
    EXPECT_NE(error.find("JSON object"), std::string::npos);
    EXPECT_FALSE(loc.addTable("", json::parse(R"({})"), &error));
    EXPECT_FALSE(loc.addTableText("en", "{ not json", &error));
    EXPECT_NE(error.find("not valid JSON"), std::string::npos);
    EXPECT_FALSE(loc.addTable("en", json::parse(R"({ "a": { "b": { "c": 1 } } })"), &error));
    EXPECT_NE(error.find("'a.b.c'"), std::string::npos) << error;
    EXPECT_TRUE(loc.languages().empty());
    EXPECT_TRUE(loc.addTableText("en", R"({ "a": "one", "b": "two" })"));
    EXPECT_TRUE(loc.addTableText("en", R"({ "a": "uno" })"));
    EXPECT_EQ(loc.text("a"), "uno") << "a second table replaces the keys it has";
    EXPECT_EQ(loc.text("b"), "two") << "and keeps the others";
}

TEST(Localizer, VersionChangesWhenWhatIsShownMight) {
    Localizer loc = sample();
    const uint64_t v0 = loc.version();
    loc.setLanguage("de");
    const uint64_t v1 = loc.version();
    EXPECT_GT(v1, v0);
    loc.setLanguage("de");
    EXPECT_EQ(loc.version(), v1) << "choosing the same language again changes nothing";
    loc.addTable("de", json::parse(R"({ "x": "y" })"));
    EXPECT_GT(loc.version(), v1);
}

TEST(Localizer, SaysWhatIsStillUntranslated) {
    Localizer loc = sample();
    const std::vector<std::string> de = loc.untranslated("de");
    EXPECT_NE(std::find(de.begin(), de.end(), "menu.quit"), de.end());
    EXPECT_NE(std::find(de.begin(), de.end(), "greeting"), de.end());
    EXPECT_EQ(std::find(de.begin(), de.end(), "menu.play"), de.end());
    EXPECT_TRUE(loc.untranslated("en").empty());
    const std::vector<std::string> brazil = loc.untranslated("pt-BR");
    EXPECT_EQ(std::find(brazil.begin(), brazil.end(), "menu.quit"), brazil.end()) << "pt covers it";
}

TEST(Localizer, LoadsFilesAndFolders) {
    const std::string dir = ::testing::TempDir() + "kke_loc_test";
    std::filesystem::remove_all(dir);
    writeFile(dir, "en.json", R"({ "hello": "Hello" })");
    writeFile(dir, "de.json", R"({ "hello": "Hallo" })");
    writeFile(dir, "pt_BR.json", R"({ "hello": "Olá" })");
    writeFile(dir, "named.json", R"({ "language": "fr", "strings": { "hello": "Bonjour" } })");
    writeFile(dir, "fonts.json", R"({ "ar": ["NotoSansArabic-Regular.ttf"], "ja": "NotoSansJP-Regular.otf" })");
    writeFile(dir, "broken.json", "{ nope");
    writeFile(dir, "readme.txt", "ignored");
    Localizer loc;
    std::vector<std::string> errors;
    EXPECT_EQ(loc.loadFolder(dir, &errors), 4u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("broken.json"), std::string::npos);
    EXPECT_EQ(loc.languages(), (std::vector<std::string>{ "de", "en", "fr", "pt-BR" }));
    loc.setLanguage("pt-BR");
    EXPECT_EQ(loc.text("hello"), "Olá");
    loc.setLanguage("fr");
    EXPECT_EQ(loc.text("hello"), "Bonjour");
    EXPECT_TRUE(loc.fonts("ar").size() == 1 && loc.fonts("ar")[0] == "NotoSansArabic-Regular.ttf");
    EXPECT_EQ(loc.fonts("ja"), (std::vector<std::string>{ "NotoSansJP-Regular.otf" }));
    EXPECT_TRUE(loc.fonts("de").empty());
    std::string error;
    EXPECT_FALSE(loc.loadFile(dir + "/missing.json", &error));
    EXPECT_NE(error.find("can't open"), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST(Localizer, FontsFollowTheLanguageThenItsBase) {
    Localizer loc;
    EXPECT_TRUE(loc.addFonts(json::parse(R"({ "zh": ["Hans.otf"], "zh-TW": ["Hant.otf"] })")));
    EXPECT_EQ(loc.fonts("zh-TW"), (std::vector<std::string>{ "Hant.otf", "Hans.otf" })) << "its own first, then the base's";
    EXPECT_EQ(loc.fonts("zh-CN"), (std::vector<std::string>{ "Hans.otf" }));
    std::string error;
    EXPECT_FALSE(loc.addFonts(json::parse(R"({ "ar": 5 })"), &error));
    EXPECT_FALSE(loc.addFonts(json::parse(R"([1])"), &error));
}

TEST(Localizer, DisplayShapesAndReordersRightToLeftText) {
    Localizer loc;
    loc.addTable("ar", json::parse(R"({ "x": "y" })"));
    loc.addTable("en", json::parse(R"({ "x": "y" })"));
    EXPECT_EQ(loc.display("plain text"), "plain text");
    loc.setLanguage("ar");
    EXPECT_TRUE(loc.rtl());
    // "bab" (ba, alef, ba): initial, final, isolated, then drawn right to left
    const std::string shaped = loc.display("\xD8\xA8\xD8\xA7\xD8\xA8");
    EXPECT_EQ(shaped, "\xEF\xBA\x8F\xEF\xBA\x8E\xEF\xBA\x91") << shaped; // U+FE8F U+FE8E U+FE91
}

TEST(Localizer, ANumberFollowsTheLanguage) {
    Localizer loc = sample();
    EXPECT_EQ(loc.number(1234.5), "1,234.5");
    loc.setLanguage("de");
    EXPECT_EQ(loc.number(1234.5), "1.234,5");
    EXPECT_EQ(loc.number(2, 2), "2,00");
}
