# Settings panel (kke::DemoPanelModule)

A panel of sliders, choices, toggles, buttons and live text for a demo or a
tool, drawn with RmlUi, that works the same with a **controller**, the
**keyboard** and the **mouse**. Every demo's settings use it, so someone
on a sofa with a pad can change everything a mouse user can.

The rule it follows (Kees, 2026-09-27): the engine's F1 panels are ImGui,
for developers; everything a player sees, and every tool for making a game
inside the game, is RmlUi.

## Using it

```cpp
// main.cpp
app.addModule<kke::InputModule>("my_demo_input.json");
app.addModule<kke::UiModule>();
app.addModule<MyDemoModule>();
app.addModule<kke::DemoPanelModule>("My demo");   // Side::Right for the right edge
```

```cpp
// MyDemoModule::init()
auto& s = app.getModule<kke::DemoPanelModule>()->section("Waves");
s.hint("{mouse:left} throw", "{sea.throw} throw");      // keyboard+mouse / controller
s.slider("Wind", &m_wind, 0.0f, 14.0f, "%.1f m/s", [this] { applyWind(); }, 0.5f);
s.choice("Throw", &m_kind, { "Foam", "Wood", "Iron" });
s.toggle("Camera follows the boat", &m_follow);
s.button("Reset", [this] { reset(); });
s.text([this] { return "Speed " + std::to_string(speed()); });
s.note("Water is 1025 kg/m3: lighter things float.");
```

The values stay in the game. The panel reads them every frame (a key that
changes one shows up in the panel too), writes them when the player changes
a row and then calls the row's `onChange`.

- `{action}` in any text becomes that action's button on the device the
  player holds (kke::ButtonPrompts); `hint()` picks a different line for
  keyboard and controller, since an action bound only to a pad shows
  nothing on the keyboard.
- A row that belongs to one scene: `.showIf([this] { return m_scene == 1; })`,
  or a whole section: `sectionIf(...)`.
- A value that moves (a setting of whichever character is selected): pass a
  `Ref` (a function returning the pointer, or null to hide the row).

## How the player drives it

| State | What shows | Controls |
|---|---|---|
| Open (start) | the panel; the game has the controls | the mouse clicks and drags anything |
| Active | a highlighted row | up/down picks a row, left/right changes it (hold to sweep), A presses, B or Esc goes back |
| Collapsed | only the title | the title or `panel.toggle` opens it |

`panel.toggle` is the View/Back button on a pad and F3 on the keyboard
(rebindable like any action). While the panel is Active, player 1's
`game` input context is off, so the stick that picks rows doesn't also
drive the boat. The last row, "Hide panel", collapses it.

Esc works like a pause menu: it opens the panel with the keyboard on it,
and Esc again goes back to the game. Because of that the panel turns the
window's quit-on-Esc off and adds a "Quit" row above "Hide panel", so
nobody is stuck in a game with no way out. A game that wants Esc for
itself calls `setEscapeMenu(false)` before `init`.

With a DemoPanelModule the engine's ImGui windows (Performance, Camera,
Physics...) start hidden: F1 shows them, in developer builds only
(`setDeveloperPanelsKey(false)` for a game that handles F1 itself).

## A camera for the controller

`OrbitCameraModule::setPadControls(true)` turns the view with the right
stick (`camera.orbit`) and zooms with the d-pad (`camera.zoom`). Both are
`game` actions, so they rest while the panel has the controller.

## Checking it headless

```bash
KKE_VIRTUAL_INPUT=pad KKE_VIRTUAL_PAD_SCRIPT="8:back,9:dpad_down,9.6:dpad_right" ./sea_demo
```

opens the panel with the virtual pad's View button 8 s in, moves down a
row and turns it up one step (docs/INPUT.md "Testing without hardware").
