#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke {

// Localization (ENG-F84, docs/LOCALIZATION.md): string tables, plurals,
// interpolation, number formats, fallback languages, right-to-left text and
// fonts per language. Pure logic, unit-tested in tests/test_localization.cpp
// and tests/test_bidi.cpp.
//
//   kke::Localizer loc;
//   loc.loadFolder("assets/lang");          // en.json, de.json, pt-BR.json ...
//   loc.setLanguage("pt-BR");               // pt-BR, then pt, then the fallback (en)
//   loc.text("menu.play");                  // "Jogar"
//   loc.text("greeting", {{"name", "Ana"}}); // "Olá, Ana!" for "Olá, {name}!"
//   loc.plural("items", 3);                 // "3 itens" from items.one / items.other

// CLDR plural categories. Which ones a language uses differs: English has
// one and other, Russian one, few, many and other, Arabic all six, Japanese only other.
enum class PluralCategory : uint8_t { Zero, One, Two, Few, Many, Other };
const char* pluralName(PluralCategory c);
// The category of a number in a language ("en", "ru", "pt-BR", ...); an
// unknown language has just Other. Fractions use the rules for a number
// with visible decimals, as CLDR does.
PluralCategory pluralCategory(const std::string& language, double n);

// "pt-BR" -> "pt"; "zh-Hant-TW" -> "zh". Lower-cases the language part.
std::string baseLanguage(const std::string& language);
// Written right to left: Arabic, Hebrew, Persian, Urdu, ...
bool isRtlLanguage(const std::string& language);

// {name} and {0} placeholders take these; "{{" and "}}" are literal braces.
using LocArgs = std::map<std::string, std::string>;

// The separators a language writes numbers with: 1,234.5 or 1.234,5 or 1 234,5.
struct NumberFormat {
    std::string decimal = ".";
    std::string group = ",";
};
NumberFormat numberFormat(const std::string& language);
// 1234567.891 with 2 decimals -> "1,234,567.89" (en) / "1.234.567,89" (de).
// decimals < 0 keeps the shortest exact form (up to 6 digits).
std::string formatNumber(double value, const NumberFormat& format, int decimals = -1);

class Localizer {
public:
    // A table of a language: {"menu": {"play": "Play"}} gives "menu.play".
    // Plural forms are keys with the category as the last part:
    // "items.one", "items.other". Adding a table for a language again
    // replaces the keys it has. False (with `error`) when it isn't an object
    // of strings and objects.
    bool addTable(const std::string& language, const nlohmann::json& table, std::string* error = nullptr);
    // The same from JSON text.
    bool addTableText(const std::string& language, const std::string& jsonText, std::string* error = nullptr);
    // A file named for its language: lang/de.json is German. Files may
    // hold {"language": "pt-BR", "strings": {...}} to name it themselves.
    bool loadFile(const std::string& path, std::string* error = nullptr);
    // Every *.json in a folder; returns how many loaded.
    size_t loadFolder(const std::string& folder, std::vector<std::string>* errors = nullptr);

    // The language shown. Text is looked up in it, then in its base language
    // ("pt-BR" -> "pt"), then in the fallback language. False for a
    // language with no table (the language is left as it was).
    bool setLanguage(const std::string& language);
    const std::string& language() const { return m_language; }
    void setFallback(const std::string& language);
    const std::string& fallback() const { return m_fallback; }
    // Languages with a table, sorted.
    std::vector<std::string> languages() const;
    bool hasLanguage(const std::string& language) const { return m_tables.count(language) != 0; }
    // How many keys of the fallback language `language` has no text for yet.
    std::vector<std::string> untranslated(const std::string& language) const;

    // The text of a key in the language shown. A key nobody has gives the
    // key itself (so a missing string is visible and searchable) and is
    // noted in missing().
    std::string text(const std::string& key, const LocArgs& args = {}) const;
    // The key's plural form for n: tries key.<category>, then key.other,
    // then key. "{n}" in the text is the number, formatted for the language.
    std::string plural(const std::string& key, double n, const LocArgs& args = {}) const;
    bool has(const std::string& key) const { return find(key) != nullptr; }
    // Fills {placeholders} in any text.
    static std::string format(const std::string& pattern, const LocArgs& args, std::vector<std::string>* unfilled = nullptr);

    // Keys that were asked for and not found, in the order first asked.
    const std::vector<std::string>& missing() const { return m_missing; }
    void clearMissing() const;

    std::string number(double value, int decimals = -1) const { return formatNumber(value, numberFormat(m_language), decimals); }
    bool rtl() const { return isRtlLanguage(m_language); }
    // Text ready for a renderer that draws left to right and knows nothing of
    // joining letters: Arabic shaped, mixed-direction text reordered
    // (kke/Bidi.h). Left-to-right text without right-to-left letters is returned as it is.
    std::string display(const std::string& text) const;

    // Changes with every language change and added table: a UI that
    // caches strings redraws when it sees a new number.
    uint64_t version() const { return m_version; }

    // Fonts a language needs beyond the default ("ar": a Noto Sans Arabic
    // file...). From a JSON map {"ar": ["NotoSansArabic-Regular.ttf"], ...}.
    bool addFonts(const nlohmann::json& map, std::string* error = nullptr);
    // The font files for the language shown (its own list, then its base
    // language's), in the order to try them; empty for languages the
    // default font covers.
    std::vector<std::string> fonts() const;
    std::vector<std::string> fonts(const std::string& language) const;

private:
    const std::string* find(const std::string& key) const;
    const std::string* findIn(const std::string& language, const std::string& key) const;
    void note(const std::string& key) const;

    std::map<std::string, std::map<std::string, std::string>> m_tables; // language -> key -> text
    std::map<std::string, std::vector<std::string>> m_fonts;
    std::string m_language = "en", m_fallback = "en";
    uint64_t m_version = 1;
    mutable std::vector<std::string> m_missing;
    mutable std::set<std::string> m_missingSet;
};

} // namespace kke
