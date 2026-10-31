# Localization

A game in several languages needs more than a table of strings: "1 item" and
"3 items" are one sentence in English and four in Russian, numbers are written
`1,234.5` or `1.234,5`, Arabic letters change shape and the line runs from the
right, and the default font has no Japanese. `kke::Localizer`
(`kke/Localization.h`, ENG-F84) does all of it in plain C++ with no
dependencies, and Lua gets it as `loc.*`.

## String tables

One JSON file per language, named for it: `lang/en.json`, `lang/de.json`,
`lang/pt-BR.json`. Nested objects are joined with dots.

```json
{
  "menu": { "play": "Play", "quit": "Quit" },
  "greeting": "Hello, {name}!",
  "items": { "zero": "No items", "one": "{n} item", "other": "{n} items" }
}
```

A file may name its own language instead of using its file name:
`{"language": "pt-BR", "strings": {...}}`.

```lua
loc.loadFolder("lang")          -- every .json in the game's folder (not outside it)
loc.language("de")              -- true; false when there is no table for it
print(loc.t("menu.play"))       -- "Spielen"
print(loc.t("greeting", { name = "Ana" }))        -- "Hello, Ana!"
print(loc.plural("items", 3))                      -- "3 items"
```

Text is looked up in the language shown, then in its base language
(`pt-BR`, then `pt`), then in the fallback language (`en`, change it with
`loc.fallback("de")`). A key nobody has comes back as the key itself, so a
missing string is visible on screen and easy to search for; `loc.missing()`
lists every key that was asked for and not found, and
`loc.untranslated("fr")` lists what French still lacks compared with the
fallback. Tables can also be written in Lua with
`loc.add("fr", { menu = { play = "Jouer" } })`.

Placeholders are `{name}` and `{0}`, `{1}` (the items of a list: `loc.t("pair", {"a", "b"})`).
`{{` and `}}` are literal braces. A placeholder nobody filled stays as it is.
Numbers in the arguments are written the way the language writes numbers.

## Plurals

`loc.plural(key, n)` picks `key.<category>` by the CLDR cardinal rule of the
language, then `key.other`, then `key`. `{n}` in the text is the number. A
`key.zero` is used for 0 in every language ("No items"), whatever the
language's own rules say.

| Language | Categories |
|---|---|
| English, German, Dutch, Swedish, Finnish, Italian, Catalan | one (exactly 1), other |
| Spanish, Hungarian, Greek, Bulgarian, Turkish, Norwegian | one (1), other |
| French, Portuguese (Brazil) | one (0 and 1), other; Portuguese (Portugal) one (1) |
| Hindi, Bengali, Persian | one (0 and 1), other |
| Russian, Ukrainian, Belarusian | one (1, 21, 31...), few (2-4, 22-24...), many (0, 5-20, 25...), other (fractions) |
| Polish | one (1), few (2-4, 22-24...), many, other |
| Czech, Slovak | one (1), few (2-4), many (fractions), other |
| Croatian, Serbian, Bosnian | one, few, other |
| Arabic | zero, one, two, few (3-10), many (11-99), other |
| Hebrew | one, two, other |
| Romanian, Slovenian, Lithuanian, Latvian, Welsh, Irish, Danish, Icelandic | their own rules (`kke/Localization.cpp`) |
| Japanese, Chinese, Korean, Vietnamese, Thai, Indonesian | other only |

An unknown language has only `other`. `pluralCategory(language, n)` is
public for C++.

## Numbers

`loc.number(1234567.891, 2)` is `1,234,567.89` in English, `1.234.567,89` in
German, `1 234 567,89` (no-break spaces) in Russian and French (narrow
no-break space). Without the second argument it is the shortest exact form
(`0.1 + 0.2` is `0.3`).

## Right-to-left languages

`loc.rtl()` says whether the language is written right to left (Arabic,
Hebrew, Persian, Urdu and the others). RmlUi's default font engine draws
left to right and knows nothing of joining letters, so text goes through
`loc.display(text)` first (`kke/Bidi.h`):

1. **Shaping.** Arabic letters take their isolated, final, initial or medial
   form from their neighbours (the Presentation Forms-B code points a font
   draws), `lam` + `alef` become one glyph, and vowel marks do not break a
   join. Persian letters (pe, che, zhe, keheh, gaf, farsi yeh) are covered.
2. **Reordering.** The Unicode bidirectional algorithm (UAX #9) as real text
   needs it: weak types, numbers (`123` stays `123`), neutrals, mirrored
   brackets, and the visual order of every line. Explicit embeddings and
   isolates are dropped.

Store text as the plain letters in the tables; call `loc.display` when
putting it on screen. Text without right-to-left letters comes back
unchanged, so it is safe to call on everything.

## Fonts

The bundled Noto Sans covers Latin, Greek and Cyrillic. Other scripts need
their own face. A `lang/fonts.json` says which:

```json
{
  "ar": ["NotoSansArabic-Regular.ttf"],
  "he": ["NotoSansHebrew-Regular.ttf"],
  "ja": ["NotoSansJP-Regular.otf"],
  "zh": ["NotoSansSC-Regular.otf"],
  "ko": ["NotoSansKR-Regular.otf"]
}
```

When a language is chosen the files it lists (then its base language's) that
exist in `assets/fonts/` are loaded into RmlUi as fallback faces, once each.
Noto fonts are free under the SIL Open Font License; the CJK ones are
large, so the engine does not ship them. Download the ones your game's
languages need from <https://fonts.google.com/noto> into `assets/fonts/`.

## Hooks

`hook.Add("LanguageChanged", id, function(e) ... end)` runs when the
language changes (`e.language`): redraw anything built from strings.
`Localizer::version()` changes with every language change and added table
for C++ code that caches text.

## From C++

```cpp
kke::Localizer loc;
loc.loadFolder("assets/lang");
loc.setLanguage("ru");
loc.plural("items", 5);                        // "5 предметов"
loc.text("greeting", {{"name", "Аня"}});
std::string shown = loc.display(loc.text("menu.play"));  // shaped and reordered
```

## What is not here

- Dates, times and currencies by locale.
- Ordinal plurals ("1st", "2nd") and select/gender forms.
- Complex scripts that need a shaping engine (Devanagari, Thai, Khmer):
  they need a font engine with HarfBuzz, not text processing.
- Automatic translation. `loc.untranslated` lists what is left to do.

Tests: `tests/test_localization.cpp`, `tests/test_bidi.cpp`,
`tests/test_script_localization.cpp`.
