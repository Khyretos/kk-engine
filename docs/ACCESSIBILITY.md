# Accessibility: letting more people play

Switches the player turns on, read by the engine and by games, none of which
changes the rules. They live under **Language and accessibility** on the
Settings page and in `EngineSettings` (`text` and `accessibility`), and apply
the moment they change.

| Switch | What it does |
|---|---|
| Language | [LOCALIZATION.md](LOCALIZATION.md): text in the player's language, right to left where it reads that way. |
| Subtitles, size | Captions for speech and sound (below): bottom of the screen, 0.75x to 2.5x, with a box of the player's choosing behind them. |
| Menu and HUD size | `graphics.uiScale`, 0.5x to 2.5x: every document scales (it already existed). |
| Colour-blind mode, strength | Protanopia, deuteranopia or tritanopia: colours shifted so the ones a person can't tell apart, can be. |
| High contrast | Documents get the class `high-contrast`; a menu's RCSS says what that means (`.high-contrast .item { ... }`). |
| Reduce motion | No screen shake (`camera.shake`), no motion blur. A game reads `accessibility.reduceMotion()` for its own sway, bob and flashes. |
| Screen reader | Menu focus and captions are spoken (below). |

Remapping every action and a controller, glyphs for the device in hand and
the sound visualizer are in INPUT.md and AUDIO.md.

## Captions

```lua
subtitles.say("Over here!", { speaker = "Mira" })              -- shown for as long as it takes to read
subtitles.say("door slams", { kind = "sound" })                -- [door slams], italic
subtitles.say("@dialogue.mira_1", { speaker = "Mira", duration = 4 })
```

`kke::Subtitles` ([kke/Subtitles.h](../engine/include/kke/Subtitles.h)) is the
queue: a caption stays 1.2 s plus 1/17 s a character (1.5 to 12 s), the same
words again renew it instead of stacking, the newest three show. `UiModule`
draws them in its own document, so every game with a UI has them. The text is
localized like any other (English text with an entry, or `@key`) and a speaker
is too. Off in the settings, `say` does nothing.

## Colour correction

The three modes are the daltonisation of Machado, Oliveira and Fernandes
(2009): what a person with that deficiency can't see (the difference between
the picture and their simulated view) is added back into the channels they
can. It is one 3x3 matrix per pixel in the tone-map pass
(`shaders/tonemap_post.frag`), after the look and grade, before the sRGB
encode: spatial, nothing temporal. Greys stay grey. Strength 0.5 is half the
shift. It is a correction, not a cure, and not a simulation: to see what a
colour-blind player sees, test with a simulation tool.
`KKE_COLORBLIND=deuteranopia` forces a mode for a screenshot.

## Screen reader

`Accessibility::announce(text, interrupt)` / `accessibility.announce(text)`
speaks when the switch is on. The engine announces captions as they appear.
Speech is a hook: a platform layer with a real screen reader (NVDA, VoiceOver,
Orca) installs its own with `setAnnouncer`; otherwise the system's speech is
used where there is one (`spd-say` from speech-dispatcher on Linux, `say` on
macOS; the text is an argument, never a command line). **Not done:** Windows
speech, announcing menu focus changes from `GameShellModule`, and exposing the
UI tree to the platform's accessibility API (AT-SPI, UIA, NSAccessibility):
each is a platform backend.

Tested in `tests/test_accessibility.cpp` (the matrices, the hook, the
caption timing) and by screenshot (docs/LOCALIZATION.md shows the menus).
