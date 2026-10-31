#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kke {

// Text in the player's language (ENG-F84): string tables with keys, {name}
// placeholders, plurals by CLDR's rules, a fallback chain, a language
// switch that changes every open screen, and right-to-left text.
//
// A language is one file, JSON or YAML (docs/DATA_FILES.md), `lang/nl.json`:
//
//   { "language": "nl", "name": "Nederlands",
//     "strings": {
//       "menu": { "start": "Start", "quit": "Stoppen" },          // "menu.start"
//       "coins": { "one": "{count} munt", "other": "{count} munten" },  // a plural
//       "hello": "Hallo {name}!" } }
//
// Nested objects make dotted keys. An object whose keys are all plural
// categories (zero, one, two, few, many, other) is a plural, chosen with
// trn(). `{{` and `}}` are literal braces.
//
// Lookup goes: the chosen language ("pt-BR"), its base language ("pt"), the
// default language ("en"). A key nobody has comes back as itself, so a
// forgotten string shows as "menu.start" and not as nothing, and is listed
// in missing() for a tool to report.
//
// Pure logic: no window, no GPU. The UI (UiModule: `i18n="key"` on any
// element) and Lua (`i18n.*`) sit on top of it.

enum class PluralCategory { Zero, One, Two, Few, Many, Other };
const char* pluralName(PluralCategory category);

// CLDR cardinal plural rules for a language code ("en", "pt-BR", "ru", "ar"...)
// and a count. Covers the languages games ship in; an unknown one is "other"
// for everything but 1, like English. Fractions are "other" except where the
// language says otherwise (French counts 1.5 as "one").
PluralCategory pluralCategory(const std::string& language, double count);

struct LanguageInfo {
    std::string code;        // "nl", "pt-BR"
    std::string name;        // "Nederlands": what the language menu shows
    bool rtl = false;        // written right to left
    std::string font;        // a font file in assets/fonts for scripts the default font lacks ("" = none)
    size_t strings = 0;
};

using LocArgs = std::vector<std::pair<std::string, std::string>>;

class Localization {
public:
    Localization();

    // ---- Loading ------------------------------------------------------
    // One language file. A language loaded twice is merged, the later file
    // winning (a mod adds or replaces strings). False with `error` for a file
    // that is not a language.
    bool loadFile(const std::filesystem::path& file, std::string* error = nullptr);
    // Every language file in a folder; how many were loaded.
    int loadFolder(const std::filesystem::path& folder);
    // Strings from memory (a game that builds them, tests).
    void addStrings(const std::string& language, const nlohmann::json& strings, const std::string& name = {});

    // ---- The language -------------------------------------------------
    // The language every key falls back to last. Default "en".
    void setDefaultLanguage(const std::string& code);
    const std::string& defaultLanguage() const { return m_default; }
    // Switches to `code` ("pt-BR" uses pt-BR, then pt). False (and nothing
    // changes) when there are no strings for it or for its base language.
    bool setLanguage(const std::string& code);
    const std::string& language() const { return m_language; }
    // Every loaded language, the default first, then by code.
    std::vector<LanguageInfo> languages() const;
    const LanguageInfo* info(const std::string& code) const;
    bool rtl() const;
    // The best loaded language for a locale string like "nl_NL.UTF-8" or
    // "pt-BR" ("" when none fits): what to start in.
    std::string bestMatch(const std::string& locale) const;
    // bestMatch of the machine's language ($LANGUAGE, $LC_ALL, $LC_MESSAGES,
    // $LANG, in that order; the OS's list through SDL where the engine has
    // a window, see UiModule).
    std::string systemLanguage() const;
    // Bumps on every language switch and every load: a screen that cached
    // text compares it to know when to rebuild.
    uint32_t generation() const { return m_generation; }
    // Called after every switch. Returns an id for removeListener().
    int addListener(std::function<void()> callback);
    void removeListener(int id);

    // ---- Text ---------------------------------------------------------
    std::string tr(const std::string& key, const LocArgs& args = {}) const;
    // A plural: the form for `count` in the current language. {count} is
    // filled in (as formatNumber()) next to the given args.
    std::string trn(const std::string& key, double count, const LocArgs& args = {}) const;
    bool has(const std::string& key) const; // in the current language, its base or the default
    // Keys asked for and found nowhere, once each, in the order asked.
    const std::vector<std::string>& missing() const { return m_missing; }
    void clearMissing() { m_missing.clear(); }

    // `{name}` placeholders filled in from `args`, `{{` and `}}` as braces: what tr() does to a
    // string, for text that did not come from a table.
    static std::string format(const std::string& text, const LocArgs& args) { return fill(text, args); }

    // A number the way the language writes it: "1,234.5" in English, "1.234,5"
    // in German and Dutch, "1 234,5" in French (a narrow no-break space) and
    // Russian. decimals < 0 keeps as many as the value needs, up to 6.
    std::string formatNumber(double value, int decimals = -1) const;

    // ---- Right to left ------------------------------------------------
    // RmlUi's text has no shaper, so Hebrew and Arabic need the work done
    // before it: Arabic letters joined into their presentation forms (lam-alef
    // ligatures included), then the line put in visual order (the Unicode
    // bidi algorithm's rules for letters, digits, neutrals and mirrored
    // brackets, one embedding level; no isolates or overrides).
    // `rtlBase`: the paragraph is right to left. A string with no RTL letters
    // comes back as it was.
    static std::string visualOrder(const std::string& utf8, bool rtlBase);
    // Joins Arabic letters only (logical order stays).
    static std::string shapeArabic(const std::string& utf8);
    static bool hasRtlLetters(const std::string& utf8);

    // ---- UTF-8 helpers (tests and callers that need them) ---------------
    static std::u32string decode(const std::string& utf8);
    static std::string encode(const std::u32string& text);

private:
    struct Entry {
        std::string text;
        std::map<PluralCategory, std::string> plural; // non-empty: a plural
    };
    struct Language {
        LanguageInfo info;
        std::unordered_map<std::string, Entry> entries;
    };
    const Entry* find(const std::string& key) const;
    std::vector<const Language*> chain() const;
    void changed();
    static std::string baseOf(const std::string& code);
    static std::string canonical(const std::string& code);
    static std::string fill(const std::string& text, const LocArgs& args);

    std::map<std::string, Language> m_languages;
    std::string m_default = "en";
    std::string m_language = "en";
    uint32_t m_generation = 1;
    mutable std::vector<std::string> m_missing;
    std::vector<std::pair<int, std::function<void()>>> m_listeners;
    int m_nextListener = 1;
};

} // namespace kke
