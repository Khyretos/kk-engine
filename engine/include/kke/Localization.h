#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kke {

// Translated text (docs/LOCALIZATION.md): string tables per language,
// plural forms by the Unicode CLDR rules, placeholders, locale numbers,
// a fallback chain, and right-to-left display (kke/Bidi.h).
//
// A language is one JSON file (assets/locales/nl.json, or a game's own
// games/<name>/locales/nl.json; files of one language add to each other,
// the later one winning, so a game can override the engine's words):
//
//   { "language": "nl", "name": "Nederlands", "fallback": "en",
//     "strings": {
//       "menu": { "start": "Start", "quit": "Afsluiten" },
//       "coins": { "one": "{n} munt", "other": "{n} munten" },
//       "greeting": "Hallo {name}!" } }
//
// Nested objects become dotted keys ("menu.start"). An object with an
// "other" key is a set of forms, picked by number (plural) or by a word
// (select: "male", "female", ...), not a namespace.
class Localization {
public:
    using Args = std::vector<std::pair<std::string, std::string>>;

    struct Language {
        std::string code;     // "nl", "pt-BR", "ar"
        std::string name;     // in its own words: "Nederlands"
        std::string fallback; // asked next for a missing string (default: the language without its region, then the default language)
        bool rtl = false;
        std::vector<std::string> fonts; // font files this language needs (the UI loads them as fallback faces)
        std::string decimal = ".", group = ",";
        std::string digits = "latin"; // "latin", "arabic-indic" (٠١٢), "persian" (۰۱۲)
        size_t strings = 0;
    };

    // The process-wide instance the engine and Lua use. Tests make their own.
    static Localization& get();

    Localization();

    // ---- loading
    // One language. A file for a language already loaded adds to it.
    bool loadJson(std::string_view json, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    // Every *.json in the folder; returns how many loaded (errors, one line each, in `error`).
    int loadFolder(const std::string& folder, std::string* error = nullptr);
    void clear();

    // ---- the language
    // The language every missing string falls back to before showing its key.
    void setDefaultLanguage(const std::string& code);
    const std::string& defaultLanguage() const { return m_default; }
    // "pt-BR" finds "pt-BR", else "pt". False (and no change) when neither is loaded.
    // "pseudo" is always there: every string stretched and accented, to see what a
    // translation would do to a layout before there is one.
    bool setLanguage(const std::string& code);
    const std::string& language() const { return m_language; }
    bool rtl() const;
    std::vector<Language> languages() const;
    const Language* info(const std::string& code) const;
    // The system's languages, most preferred first ("nl-NL", "en-US"): SDL's
    // list, or the LC_ALL / LANG environment where SDL has none.
    static std::vector<std::string> systemLanguages();
    // The first of `preferred` that `available` has (exactly, then without its region), else `fallback`.
    static std::string pick(const std::vector<std::string>& preferred, const std::vector<std::string>& available, const std::string& fallback);
    // setLanguage(pick(systemLanguages(), ...)); what a game does at first start.
    bool useSystemLanguage();

    // ---- text
    // The string, `{name}` filled from args ({{ and }} are braces). Missing:
    // through the fallback chain, then the key itself (and it is remembered, see missingKeys()).
    std::string tr(const std::string& key, const Args& args = {}) const;
    // The plural form for n ("coins": one / other ...); {n} is n, written for the language.
    std::string plural(const std::string& key, double n, const Args& args = {}) const;
    // The form named by `choice`, else "other".
    std::string select(const std::string& key, const std::string& choice, const Args& args = {}) const;
    bool has(const std::string& key) const;
    // n written for the language: 1,234.5 / 1.234,5 / 1 234,5 / ١٬٢٣٤٫٥. decimals < 0: as many as it needs (up to 3).
    std::string number(double n, int decimals = -1) const;
    // Text ready for a screen with no bidi of its own (kke/Bidi.h): joined Arabic, visual order.
    std::string display(std::string_view logical) const;
    // The CLDR plural category of n: "zero", "one", "two", "few", "many" or "other".
    static std::string pluralCategory(const std::string& language, double n);
    // text stretched by about a third, letters accented, [brackets] round it, {placeholders} intact.
    static std::string pseudo(std::string_view text);

    // ---- checking a translation
    // Keys asked for that no language had, since clear() or the last forgetMissing().
    std::vector<std::string> missingKeys() const;
    void forgetMissing();
    // Keys of the default language that `code` does not have.
    std::vector<std::string> untranslated(const std::string& code) const;
    // Placeholders of a string ({name}) that a translation of it lost or invented; one line each.
    std::vector<std::string> placeholderProblems(const std::string& code) const;

    // ---- change
    // Called after setLanguage() changes the language (the UI re-translates). Returns an id.
    int onChange(std::function<void()> callback);
    void removeOnChange(int id);

private:
    struct Entry {
        std::string text;                         // a plain string
        std::map<std::string, std::string> forms; // plural or select forms ("other" always there)
    };
    struct Table {
        Language info;
        std::map<std::string, Entry> entries;
    };
    std::vector<const Table*> chain() const;
    static std::string baseCode(const std::string& code);
    std::string fill(const std::string& text, const Args& args, const Table* language) const;
    std::string formatNumber(double n, int decimals, const Table* language) const;
    const Table* table(const std::string& code) const;

    mutable std::mutex m_mutex;
    std::map<std::string, Table> m_tables;
    std::string m_language = "en", m_default = "en";
    mutable std::set<std::string> m_missing;
    std::map<int, std::function<void()>> m_listeners;
    int m_nextListener = 1;
};

} // namespace kke
