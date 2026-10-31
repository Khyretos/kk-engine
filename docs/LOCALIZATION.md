# Localization: text in the player's language

ENG-F84. One module, `kke::LocalizationModule`, gives a game string tables,
plurals, a language switch that changes every open screen, and
right-to-left text. Add it next to the other modules (the starter game
already has it):

```cpp
app.addModule<kke::LocalizationModule>();   // reads assets/lang and the game's lang/ folder
```

## A language file

One file per language, JSON or YAML ([DATA_FILES.md](DATA_FILES.md)), in
`games/<game>/lang/nl.json`:

```json
{
  "language": "nl",
  "name": "Nederlands",
  "strings": {
    "menu": { "start": "Start", "quit": "Stoppen" },
    "hello": "Hallo {name}!",
    "coins": { "one": "{count} munt", "other": "{count} munten" }
  }
}
```

- Nested objects make dotted keys: `menu.start`.
- An object whose keys are all plural categories (`zero`, `one`, `two`,
  `few`, `many`, `other`) is a plural, read with `i18n.trn`.
- `{name}` is filled from the table you pass; `{{` and `}}` are literal braces.
- A list of strings is one text with line breaks.
- `"rtl": true` and `"font": "NotoSansArabic-Regular.ttf"` are optional: right-to-left
  is known for Arabic, Hebrew, Persian, Urdu and a few more, and `font` names a file in
  `assets/fonts` to use for scripts the bundled Noto Sans lacks.

Files are read from the engine's `assets/lang` (`KKE_LANG_DIR`), then the game's
`lang/` folder, so a game adds strings and replaces the engine's. A second file for the
same language is merged, the later one winning: a mod adds a translation without touching yours.

The engine ships its own menu strings in English, Dutch, German, French, Spanish, Arabic and
Hebrew (`assets/lang`). English is also written in the code, so a missing file changes nothing.

## Lookup

`nl-BE` is looked up as `nl-BE`, then `nl`, then the default language (`en`). A key nobody
has comes back as itself (`menu.start`), so a forgotten string shows up as itself and not as
an empty box, and `i18n.missing()` lists it once for a tool or a log.

## Choosing the language

`gameplay.language` in `settings.json`: `"auto"` (the default) takes the machine's language from
`$LANGUAGE`, `$LC_ALL`, `$LC_MESSAGES` or `$LANG`, and the default language when none of the loaded
ones fits; or a code like `"pt-BR"`. The settings menu (Accessibility) lists every loaded language by
its own name, and changing it is immediate: open screens are translated again.

## In documents (RML)

```html
<h1 i18n="menu.title"></h1>
<button i18n="menu.start"></button>
<input type="text" i18n-placeholder="menu.name_hint"/>
<span i18n="coins" i18n-count="3"></span>                       <!-- "3 munten" -->
<span i18n="hello" i18n-args="name=Kees"></span>                 <!-- "Hallo Kees!" -->
```

`i18n` sets the element's text; `i18n-title`, `i18n-placeholder` and `i18n-value` set those
attributes; `i18n-count` picks a plural form; `i18n-args="a=1;b=2"` fills placeholders. This is
done when a document opens and again whenever the language changes, and for the markup of
`ui.rml`. Text is escaped: a translation can never inject markup. Every document's body gets the
class `kke-rtl` in a right-to-left language, so RCSS can mirror a layout
(`body.kke-rtl .row { flex-direction: row-reverse; }`).

## In Lua

```lua
i18n.tr("menu.start")                        -- "Begin"
i18n.tr("hello", {name = "Kees"})            -- "Hallo Kees!"
i18n.trn("coins", 3)                         -- "3 munten": the plural form for a count
i18n.number(1234.5)                          -- "1.234,5" (Dutch), "1,234.5" (English), "1 234,5" (French)
i18n.language()                              -- "nl"
i18n.setLanguage("de")                       -- true, or false when nothing is loaded for it
i18n.languages()                             -- { {code="en", name="English", rtl=false}, ... }
i18n.has("menu.start"); i18n.rtl(); i18n.missing()
i18n.visual(text)                            -- shaped and ordered for display, for text you build yourself
hook.Add("LanguageChanged", "my.id", function(code) ... end)
```

Text you give to `ui.text` yourself is shown as typed: pass it through `i18n.tr` (and `i18n.visual` for
a right-to-left language if it is not from a table) first.

## Plurals and numbers

Plural categories follow CLDR's cardinal rules for English, German, Dutch, Scandinavian, Romance and
Turkish languages (one/other, French and Brazilian Portuguese count 0 as one), Russian, Ukrainian,
Polish, Czech, Slovak, Croatian and Serbian (one/few/many), Arabic (all six), Hebrew, Lithuanian,
Latvian, Romanian, Slovenian, Irish, Welsh, Hindi and Persian, and none for Japanese, Chinese, Korean,
Vietnamese, Thai and Indonesian. An unlisted language is treated like English. The plural form comes from the
language that owns the text, so a string that falls back to English uses English's rules.

Numbers use the language's separators: `1,234.5` in English, `1.234,5` in German, Dutch, Italian and
Brazilian Portuguese, `1 234,5` (no-break space) in Russian, Polish and Swedish, a narrow space in French,
and no mark in four-digit numbers in Spanish, Polish and European Portuguese.

## Right to left

RmlUi's text engine has no shaper and no bidirectional algorithm, so the engine does that work
(`Localization::visualOrder`, applied to every `i18n` text, the shell's menus, subtitles and
`i18n.visual`):

- Arabic letters are joined into their presentation forms (initial, medial, final, isolated),
  lam-alef ligatures included, with vowel marks passing through. The table is generated from Unicode's own
  data by `tools/i18n/gen_arabic_table.py`.
- The line is put in visual order by the Unicode bidirectional algorithm's rules for one embedding
  level: right-to-left letters, left-to-right words, numbers (with their separators and currency
  signs) and neutral punctuation, and brackets that turn round.
- Each `\n` line is ordered on its own. Long text should be wrapped to lines before it is ordered
  (the engine wraps visual text, so very long Arabic or Hebrew paragraphs may wrap at the wrong end); labels,
  buttons and subtitles of a line or two are the intended use. No isolates or explicit overrides.

## Fonts

Noto Sans (Latin, Greek, Cyrillic) is always loaded, with Noto Color Emoji as a fallback; Noto Sans Arabic and Hebrew
ship in `assets/fonts` and load when a language asks for them with `"font"`. Any font file (`.ttf`, `.otf`, `.ttc`) you put in
`assets/fonts/fallback/` joins the fallback chain at start, which is how to add Chinese, Japanese, Korean, Thai or
Devanagari: Noto Sans CJK is 16 to 20 MB a language, so the engine does not ship it.

## What is not here

- Translating the engine's other tools (the sandbox, the character creator, the ImGui developer panels) is not
  done: only the game shell (title, pause, settings, controls) is.
- Gendered and contextual forms (`select`) and ordinals are not in the string syntax: use separate keys.
- Complex scripts that need a shaper (Devanagari, Thai, Tamil...) draw their letters but not their conjuncts:
  RmlUi's FreeType text has no HarfBuzz. Arabic and Hebrew are handled by the engine as above.
- Right-to-left layout of whole screens is the document's RCSS (the class `kke-rtl` is there for it).
