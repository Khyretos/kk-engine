# Menus, pause and settings (kke::GameShellModule)

Every KKE game has the same menus in the same places, so a player who knows
one game knows them all (Kees, 2026-09-28). They look like the kke_demo
pause menu (a column of rows on the left over the dimmed game) and work the
same with a **controller**, the **keyboard** and the **mouse**.

| Page | What's on it | How you get there |
|---|---|---|
| Title | the game's name; Play, friends hosting this game ("Join Kees (2/8)", with a `NetModule`: NETWORKING.md "Finding games"), Settings, Controls, Quit | when the game starts |
| Modes | the game's modes with a line each (only when the game has modes) | Play |
| Pause | Resume, the game's own rows, Settings, Controls, Main menu, Quit | **Start** or **View/Back** on a controller, **Esc** on the keyboard |
| Settings | full screen, VSync, frame cap, menu and HUD size, volumes, quiet in the background, look speed, invert look, the game's own sections | Settings |
| Controls | every button of the game, for the keyboard and mouse or for a controller: pick a row, press the new key or button | Controls |

- **Local play freezes everyone** while the pause menu is open
  (`Application::setPaused`). **Online the game keeps running**: the menu
  says so at the top.
- A game with a lobby (`kke::LobbyModule`) goes from Play to its lobby:
  that is where the game type is picked and **every controller is set up
  before a match** (press A to join). In the lobby, Select (or its
  "Settings and quit" row) opens the pause menu.
- Settings are saved in `settings.json` next to the game, shared by every
  game (the same file `kke::SettingsModule` uses). Remapped buttons are
  saved in the game's input file; "Reset to defaults" puts them back.
- The advanced editor (several bindings per action, hold, double tap,
  chords, axes) is the rmlui_demo's Input screen; a game can link it with
  `setAdvancedControls`.
- Quit (and closing the window, or SUPER+Q on Hyprland) ends the game
  cleanly. If shutting down ever takes longer than 8 s, the engine logs
  which step was stuck ("quitting took longer than 8 s, stuck while ...")
  and ends the process, so a game never hangs on exit.

## Keys

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| Move | d-pad or left stick (any player) | arrows | point |
| Press / change | A, left and right | Enter or Space, left and right | click, drag a slider |
| Back | B | Esc or Backspace | |
| Close the pause menu | Start or View/Back | Esc | Resume |

The pause button shows in hints as `{shell.pause}` (Esc on the keyboard,
the Start glyph on a controller).

## Using it in a game

```cpp
// main.cpp: after InputModule and UiModule
app.addModule<kke::GameShellModule>("Racing", "Three laps, four friends");

// the game's init(), all optional
auto* shell = app.getModule<kke::GameShellModule>();
shell->addMode({ "race", "Race", "Three laps against the CPU" });
shell->onPlay = [this](const std::string& mode) { start(mode); };
shell->onMainMenu = [this] { backToStart(); };          // "Main menu"
shell->addPauseItem("Restart", [this] { restart(); });
shell->settings("Gameplay").toggle("Damage numbers", &m_numbers);
shell->startIsTheGames = [this] { return m_phase == Phase::Results; }; // "Start: race again"
shell->blockPause = [this] { return m_cutscene; };
```

- `onPlay` gets the mode's id ("" without modes). Without `onPlay` Play
  just closes the title.
- `startIsTheGames`: while it's true, Start belongs to the game (a results
  screen's "Start: again") and only View/Back and Esc open the pause menu.
- `blockPause`: while it's true, nothing opens the menu (the Flying demo
  keeps its own per-player pause menu in the air, with a Settings row that
  calls `openPause()`).
- `online`: unset, the shell asks `NetModule` (connected or hosting).
- `pauseRows()` and `settings(title)` take the same rows as the demo
  panel: `heading`, `text`, `button`, `toggle`, `choice`, `slider`,
  `showIf`.
- `setStartOnTitle(false)` starts straight in the game (a tool, or a game
  with its own start screen).
- With a `kke::DemoPanelModule` the pause menu has a "Demo settings" row
  that opens the panel; the panel's View/Back and Esc go to the pause
  menu, F3 still toggles it ([DEMO_PANEL.md](DEMO_PANEL.md)).
- While a menu is open the `game` input context is off for every player
  and a click is never the game's (`Application::uiCapturesMouse`).

## Testing headless

| Variable | What it does |
|---|---|
| `KKE_MAIN_MENU=0` | No title: straight into the game (`tools/check_game` and the recipe runner set it; a benchmark run never shows the title) |
| `KKE_MAIN_MENU=pause` | Opens the pause menu 1.5 s in (screenshots) |
| `KKE_MAIN_MENU=settings` / `controls` | Opens that page 1.5 s in |

The pure parts (simple remapping, frame caps, typed join addresses) are
unit-tested in `tests/test_game_shell.cpp`.
