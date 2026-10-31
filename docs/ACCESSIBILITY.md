# Accessibility: playing comfortably

ENG-F85. What a player can change so the game works for them, in the settings menu (Accessibility) and in
`settings.json`. Sound made visible (the ring and captions for deaf and hard-of-hearing players) is
[AUDIO.md](AUDIO.md) "Accessibility"; rebinding every key and button is [INPUT.md](INPUT.md).

| Setting (`settings.json`) | What it does |
|---|---|
| `graphics.uiScale` | Menu and HUD size, 0.5x to 2.5x (everything is authored in `dp`) |
| `gameplay.language` | The language of the game's text: [LOCALIZATION.md](LOCALIZATION.md) |
| `accessibility.colourBlind` | `off`, `protanopia`, `deuteranopia`, `tritanopia`, `achromatopsia` |
| `accessibility.colourBlindStrength` | 0 to 1: how much of the correction is applied |
| `accessibility.subtitles` | Dialogue subtitles on or off |
| `accessibility.subtitleScale` | 0.5x to 3x the subtitle size |
| `accessibility.reduceMotion` | No scripted camera shake, no motion blur |
| `accessibility.highContrast` | Menus and subtitles on solid black with white outlines |

## Colour-blind modes

The correction is one 3x3 matrix on the tone-mapped colour, applied by the tone-map pass that already
touches every pixel (`shaders/tonemap_post.frag`), so it costs nothing and is skipped when off. It is the usual
daltonization: the colour minus how the colour looks to a person with the condition (Machado, Oliveira and
Fernandes 2009's matrices for full severity, on linear colour) is the information they lose, and that
is moved into the channels they do see (red's and green's loss into blue and the other, blue's into red and
green). `achromatopsia` removes the colour and keeps the lightness. Strength blends from no change to the full correction.
`kke/ColourBlind.h` has the matrices and is tested: for each condition, two colours the simulated viewer
cannot tell apart end up clearly different after the correction.

It corrects the 3D picture, not the UI drawn over it: a game's HUD should not rely on colour alone (an icon or
a shape as well as red and green).

To check your game, run it as a person with the condition sees it, whatever the player's setting:

```bash
KKE_COLOURBLIND_SIM=protanopia ./my_game     # or deuteranopia, tritanopia, achromatopsia
```

## Subtitles

`kke::SubtitleModule` (the starter game has it) draws dialogue lines at the bottom of the screen, on a dark band,
with the speaker's name in yellow, in the size the player chose, and not at all when they turned subtitles off
(nothing is even queued, so nothing appears later). A line stays as long as it takes to read (17 characters a second, between
1.5 and 10 seconds) unless you give a time, fades in and out, and waits when three are already on screen.

```lua
subtitle.say("Welcome, {name}.", 0, "Mira", {name = "Kees"})   -- text, seconds (0 = reading time), speaker, placeholders
subtitle.say("intro.line1", 3.0, "npc.guard")                  -- a language key (LOCALIZATION.md) is shown in the player's language
subtitle.clear()
subtitle.active()                                              -- how many are on screen or waiting
```

Text and speaker may each be a language key; right-to-left languages are shaped and ordered. In C++:
`app.getModule<kke::SubtitleModule>()->say(...)`. Sound captions ("Metal impact") are the sound ring's.

## Reduced motion and high contrast

`reduceMotion` turns motion blur off whatever the graphics setting says, and `camera.shake` does nothing
(`Application::reduceMotion()` is there for a game that moves its own camera or shakes its own UI: read it). `highContrast`
adds the class `kke-high-contrast` to every document's body; the shell's own menus and the subtitles use it, and your RCSS can too
(`body.kke-high-contrast .panel { background-color: #000; border: 2dp #fff; }`). `reduceMotion` adds `kke-reduce-motion` the same way,
for transitions and animations in your RCSS.

## Not done

- A bridge to the operating system's screen reader (AT-SPI, UI Automation, VoiceOver): the engine draws its own UI,
  so that needs each platform's accessibility tree. Nothing speaks the menus aloud yet.
- Colour-blind correction of the UI layer, and a mode that shifts the engine's own HUD colours.
- Dyslexia-friendly fonts and text spacing options.
