// loc.* in Lua (kke/ScriptLocalization.h, ENG-F84).
#include "kke/ScriptLocalization.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace kke;

namespace {

std::string globalString(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    std::string v = lua_isstring(vm.state(), -1) ? lua_tostring(vm.state(), -1) : "";
    lua_pop(vm.state(), 2);
    return v;
}
bool globalBool(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const bool v = lua_toboolean(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
double globalNumber(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const double v = lua_tonumber(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string lastError(const ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }

struct Fixture {
    ScriptVM vm;
    ScriptLocalization loc;
    explicit Fixture(const std::string& folder = {}) : loc(folder) { loc.bind(vm); }
    bool run(const std::string& code) { return vm.runString(code, "console"); }
};

} // namespace

TEST(ScriptLocalization, LooksUpTextWithArguments) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        assert(loc.add("en", { menu = { play = "Play" }, hi = "Hi {name}, you have {count} coins", pair = "{0} and {1}", items = { one = "{n} item", other = "{n} items" } }))
        assert(loc.add("de", { menu = { play = "Spielen" }, items = { one = "{n} Ding", other = "{n} Dinge" } }))
        play = loc.t("menu.play")
        hi = loc.t("hi", { name = "Ana", count = 1234 })
        positional = loc.t("pair", { "a", "b" })
        one = loc.plural("items", 1)
        many = loc.plural("items", 2500)
        lang = loc.language()
        ok = loc.language("de")
        german = loc.t("menu.play")
        germanMany = loc.plural("items", 2500)
        bad = loc.language("xx")
        stays = loc.language()
        list = table.concat(loc.languages(), ",")
        has = loc.has("menu.play")
        hasNot = loc.has("nothing")
        fmt = loc.number(1234.5)
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "play"), "Play");
    EXPECT_EQ(globalString(f.vm, "hi"), "Hi Ana, you have 1,234 coins");
    EXPECT_EQ(globalString(f.vm, "positional"), "a and b") << "{0} is the first item of the list";
    EXPECT_EQ(globalString(f.vm, "one"), "1 item");
    EXPECT_EQ(globalString(f.vm, "many"), "2,500 items");
    EXPECT_EQ(globalString(f.vm, "lang"), "en");
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    EXPECT_EQ(globalString(f.vm, "german"), "Spielen");
    EXPECT_EQ(globalString(f.vm, "germanMany"), "2.500 Dinge");
    EXPECT_FALSE(globalBool(f.vm, "bad"));
    EXPECT_EQ(globalString(f.vm, "stays"), "de");
    EXPECT_EQ(globalString(f.vm, "list"), "de,en");
    EXPECT_TRUE(globalBool(f.vm, "has"));
    EXPECT_FALSE(globalBool(f.vm, "hasNot"));
    EXPECT_EQ(globalString(f.vm, "fmt"), "1.234,5");
}

TEST(ScriptLocalization, ListsMissingKeysAndUntranslatedOnes) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        loc.add("en", { a = "A", b = "B" })
        loc.add("fr", { a = "Le A" })
        loc.language("fr")
        loc.t("c.d")
        loc.t("e")
        loc.t("c.d")
        missing = table.concat(loc.missing(), ",")
        todo = table.concat(loc.untranslated("fr"), ",")
        fallback = loc.fallback()
        loc.fallback("fr")
        fallback2 = loc.fallback()
    )")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "missing"), "c.d,e");
    EXPECT_EQ(globalString(f.vm, "todo"), "b");
    EXPECT_EQ(globalString(f.vm, "fallback"), "en");
    EXPECT_EQ(globalString(f.vm, "fallback2"), "fr");
}

TEST(ScriptLocalization, TheLanguageChangedHookFiresOnceAndFontsAreRequested) {
    Fixture f;
    std::vector<std::string> asked;
    f.loc.onFonts = [&](const std::vector<std::string>& files) { asked.insert(asked.end(), files.begin(), files.end()); };
    ASSERT_TRUE(f.run(R"(
        loc.add("en", { a = "A" })
        loc.add("ar", { a = "ا" })
        changes = 0
        hook.Add("LanguageChanged", "t", function(e) changes = changes + 1 lastLanguage = e.language end)
        loc.language("ar")
        loc.language("ar")
        loc.language("en")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "changes"), 2.0);
    EXPECT_EQ(globalString(f.vm, "lastLanguage"), "en");
    EXPECT_EQ(asked.size(), 0u) << "no font map: nothing to load";
    f.loc.localizer().addFonts(nlohmann::json::parse(R"({ "ar": ["NotoSansArabic-Regular.ttf"] })"));
    ASSERT_TRUE(f.run("loc.language('ar') fonts = table.concat(loc.fonts(), ',')")) << lastError(f.vm);
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], "NotoSansArabic-Regular.ttf");
    EXPECT_EQ(globalString(f.vm, "fonts"), "NotoSansArabic-Regular.ttf");
}

TEST(ScriptLocalization, RightToLeftTextIsShapedForDisplay) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        loc.add("en", { a = "A" })
        loc.add("ar", { a = "ا" })
        before = loc.rtl()
        loc.language("ar")
        after = loc.rtl()
        shaped = loc.display("\u{628}\u{627}\u{628}")
        plain = loc.display("hello")
    )")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "before"));
    EXPECT_TRUE(globalBool(f.vm, "after"));
    EXPECT_EQ(globalString(f.vm, "shaped"), "\xEF\xBA\x8F\xEF\xBA\x8E\xEF\xBA\x91");
    EXPECT_EQ(globalString(f.vm, "plain"), "hello");
}

TEST(ScriptLocalization, LoadsFilesInsideTheGamesFolderOnly) {
    const std::string dir = ::testing::TempDir() + "kke_script_loc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/lang");
    std::ofstream(dir + "/lang/en.json") << R"({ "hello": "Hello" })";
    std::ofstream(dir + "/lang/es.json") << R"({ "hello": "Hola" })";
    std::ofstream(dir + "/one.json") << R"({ "x": "y" })";
    std::ofstream(dir + "/../kke_outside.json") << R"({ "x": "y" })";
    Fixture f(dir);
    ASSERT_TRUE(f.run(R"(
        count = loc.loadFolder("lang")
        loc.language("es")
        hola = loc.t("hello")
        okFile = loc.load("one.json")
        outside, whyOutside = loc.load("../kke_outside.json")
        absolute, whyAbsolute = loc.load("/etc/passwd")
        folder, whyFolder = loc.loadFolder("..")
        missing, whyMissing = loc.load("nope.json")
    )")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "count"), 2.0);
    EXPECT_EQ(globalString(f.vm, "hola"), "Hola");
    EXPECT_TRUE(globalBool(f.vm, "okFile"));
    EXPECT_FALSE(globalBool(f.vm, "outside"));
    EXPECT_NE(globalString(f.vm, "whyOutside").find("inside the game's folder"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "absolute"));
    EXPECT_FALSE(globalBool(f.vm, "folder"));
    EXPECT_FALSE(globalBool(f.vm, "missing"));
    EXPECT_NE(globalString(f.vm, "whyMissing").find("can't open"), std::string::npos);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(::testing::TempDir() + "kke_outside.json");
}

TEST(ScriptLocalization, BadTablesAndArgumentsAreReported) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        ok, why = loc.add("en", { a = 5 })
        notTable = pcall(loc.add, "en", "text")
        nan = pcall(loc.plural, "x", 0 / 0)
    )")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "ok"));
    EXPECT_NE(globalString(f.vm, "why").find("'a' must be text"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "notTable"));
    EXPECT_FALSE(globalBool(f.vm, "nan"));
}
