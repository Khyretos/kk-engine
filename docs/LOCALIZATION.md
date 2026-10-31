# Localization: any language, left to right or right to left

A game says what it means; `kke::Localization`
([kke/Localization.h](../engine/include/kke/Localization.h)) says it in the
player's language. It is string tables, plural forms by the Unicode CLDR
rules, placeholders, locale numbers, a fallback chain, a language switch, and
right-to-left text ([kke/Bidi.h](../engine/include/kke/Bidi.h)), all working
through the UI the engine already has: nothing to change in a menu to
translate it.

## A language is one JSON file

`assets/locales/nl.json` for the engine's own words, `games/<name>/locales/nl.json`
for a game's. Files of one language add to each other (the game's win), so a
game can add words and override the engine's.

```json
{ "language": "nl", "name": "Nederlands", "fallback": "en",
  "strings": {
    "menu": { "start": "Start", "quit": "Afsluiten" },
    "coins": { "one": "{n} munt", "other": "{n} munten" },
    "pronoun": { "male": "zijn", "female": "haar", "other": "hun" },
    "greeting": "Hallo {name}!",
    "Settings": "Instellingen" } }
```

- Nested objects are dotted keys (`menu.start`).
- An object with an `other` key is a set of **forms**: picked by number
  (`plural`) or by a word (`select`).
- `{name}` is a placeholder; `{{` and `}}` are braces.
- `"rtl": true`, `"fonts": [...]` and `"numbers": {"decimal": ",", "group": "."}`
  are optional; `ar`, `he`, `fa` and `ur` are right to left without saying so.
- A bad file is refused whole, with the key and the reason, and changes nothing.

## Using it

**In a document, nothing.** Every piece of text RmlUi shows (static text in an
`.rml` file, `ui.text`, `innerRML`, data-bound text) passes through the
localizer. Text that has an entry in the current language is replaced by it,
so English is the key (`"Settings": "Instellingen"`). A game that prefers named
keys writes `@menu.start` as the text. Text without an entry is left as it is.
This is how the engine's own menus are translated.

**In Lua:**

```lua
ui.text("status", loc.plural("coins", n))          -- "1 munt" / "1.234 munten"
ui.text("who", loc.tr("greeting", {name = player})) -- "Hallo Kees!"
ui.text("pts", loc.tr("score", {points = 12345}))   -- numbers come out as 12.345 / 12,345 / ١٢٬٣٤٥
loc.set("ar"); loc.languages(); loc.language(); loc.rtl()
```

`loc.tr`, `plural`, `select`, `has`, `number`, `display`, `load`, `missing`:
[SCRIPTING.md](SCRIPTING.md). A language menu is a loop over `loc.languages()`.

**In C++:** `Localization::get().tr("menu.start")`, `.plural(key, n)`, `.number(x)`.

**The player's choice** is `EngineSettings::text.language` (`"auto"` follows the
system), shown on the Settings page, applied by `SettingsModule` and saved.
`KKE_LANGUAGE=nl` forces one for a screenshot.

## The fallback chain

A string is looked for in the current language, its `fallback`, the language
without its region (`pt-BR` finds `pt`), the default language (`en`), and
then the key itself is shown, so nothing is ever blank. Keys asked for and not
found are remembered: `loc.missing()` is what is left to write.

## Plurals

`plural(key, n)` picks `zero`, `one`, `two`, `few`, `many` or `other` by the
CLDR rules of the language that string came from: Russian's 1, 2-4, 5-20 and
21; Polish; Czech; Arabic's six forms; Hebrew's two; Slovenian, Romanian,
Lithuanian, Latvian, Welsh, Irish, Croatian and the rest (about 70 languages;
those that don't pluralise, like Japanese, are always `other`). `{n}` in the
text is the number, written for the language. A form a translation leaves out
falls back to `other`.

## Numbers

`number(1234567.891)` is `1,234,567.891` in English, `1.234.567,891` in
German and Dutch, `1 234 567,891` in French (narrow no-break space), and
with `"digits": "arabic-indic"` `١٬٢٣٤٬٥٦٧٫٨٩١`. A number passed to `loc.tr` as
a placeholder is written this way too.

## Right to left

A screen has no idea that Arabic letters join or that Hebrew runs the other
way: `Localization::display()` / `bidi::display()` does both, from and to
UTF-8:

- **Shaping.** Arabic letters (and Persian and Urdu pe, che, zhe, kaf, gaf,
  yeh) become their initial, medial, final or isolated forms
  (U+FB50-FDFF, U+FE70-FEFF), with the lam-alef ligatures; marks stay put.
- **Order.** The Unicode Bidirectional Algorithm for each line: paragraph
  direction from the first strong letter, numbers stay left to right inside
  right-to-left text, brackets face the other way. Not done: explicit
  embeddings and isolates (dropped) and bracket pairing (UAX #9 N0).

The UI does this for you (a text node with right-to-left letters, or any text
while the language is right to left). Documents also get `rtl` or `ltr` as a
class on their body, so a menu can mirror itself:
`.rtl .row { flex-direction: row-reverse; }`. The engine's menu does: the column
moves to the right, rows and the highlight mirror.

## Fonts

A language file names the fonts it needs; the UI loads them as fallback faces
(any text in any script shows in any UI). Arabic and Hebrew ship
(`NotoSansArabic-Regular.ttf`, `NotoSansHebrew-Regular.ttf`, OFL); Latin,
Cyrillic, Greek and emoji are in the default fonts. **Not shipped:** Chinese,
Japanese, Korean, Thai, Devanagari and the other scripts: their fonts are
megabytes each, so a game that wants them ships the Noto font and lists it in
its language file.

## Testing a translation

- `setLanguage("pseudo")` stretches every string by a third, accents it and
  brackets it (`[Héllö {name}~~]`): the layout that survives it will survive
  German; any string that stays plain was never going through the localizer.
- `untranslated("nl")` lists the keys the default language has that `nl` lacks;
  `placeholderProblems("nl")` lists `{name}`s a translator lost or invented.
- Tested in `tests/test_localization.cpp` and `tests/test_bidi.cpp`.

## What ships

`assets/locales` has English (the identity table the checks compare against),
Dutch, German, French, Spanish, Arabic and Hebrew for the engine's menus and
settings (about 55 strings each). **They were drafted by the assistant, not
by native speakers: have each reviewed before a game ships it.**

## Not done

- A tool to scan a game's Lua and RML for strings that have no key.
- Gender and grammatical case beyond `select`; ICU message formatting.
- Vertical and mixed-direction layouts (Mongolian, Japanese vertical).
- Input methods (IME) for Chinese, Japanese and Korean typing.
