# Input

Everything a player presses goes through **actions** (`kke::InputMap`),
modelled on Escape from Tarkov's binding screen. Games never ask "is W
down?"; they ask "how much is `move`?", "was `jump` pressed?", "is
`crouch` on?". Players can bind **anything to anything**, any number of
times, and nothing is hardcoded except Esc (so nobody can lock themselves
out of the menu).

| Piece | What it does | Where |
|---|---|---|
| `kke::InputDevices` | Every keyboard, mouse, gamepad and raw joystick SDL sees; stable identities; sensors; rumble/LED; rebinding capture | `engine/include/kke/InputDevices.h` |
| `kke::InputMap` | Actions, bindings, triggers, chords, analog shaping, contexts, JSON | `engine/include/kke/InputMap.h` (pure logic, 13 tests) |
| `kke::InputModule` | Owns both, one map per local player, loads/saves `input.json`, standard character actions, left-handed preset, UI navigation actions, virtual test devices | `engine/include/kke/modules/InputModule.h` |
| Input screen | Live tester for every peripheral + the bindings editor | `rmlui_demo` → **Input** tab (`games/rmlui_demo/InputScreen.*`, `ui/input.rml`) |

## Bindings

A binding = a **source**, optional **modifiers**, a **trigger**, and shaping.

**Sources** (anything SDL reports): keyboard keys (by physical position,
named by your layout), mouse buttons 1-32, wheel up/down/left/right, mouse
motion X/Y, gamepad buttons (incl. paddles R4/L4/R5/L5, QAM/Share/Misc,
touchpad click), gamepad axes (whole or either half), raw joystick buttons,
axes (whole or half) and hat directions on **any** device (HOTAS, wheels,
pedals, button boxes), gyro pitch/yaw/roll, accelerometer, touchpad X/Y.

**Triggers** (per binding, so one key can do several things):

| Trigger | Fires | Example |
|---|---|---|
| Press | on the down edge; held while down | Jump |
| Release | on the up edge | throw on release |
| Hold | once held for `holdTime` (default 0.35 s); held until release | Tarkov hold-to-lean |
| Tap | on release if shorter than `tapTime` | reload |
| DoubleTap | second press within `doubleTapWindow` | quick reload (drop mag) |
| Continuous | the whole time (and analog value) | move, sprint (hold) |
| Toggle | each press flips it | crouch, sprint on L3 |

Rules that make this robust:
- **Tap waits for a double tap** only when that same input also has a
  DoubleTap binding, and fires when the window runs out. A double tap
  never also fires the single tap.
- **Tap + Hold on one key** work together (tap = toggle lean, hold = hold
  lean): the tap fires only if released before `tapTime`, the hold only
  after `holdTime`.
- **Chords**: modifiers are any sources that must be held (Ctrl+E, LB+A,
  a HOSAS "shift" button, a pedal). The most specific chord wins: while
  Alt is held, Alt+T fires and a plain T binding stays quiet.
- **Axes and buttons interchange**: half a stick or a trigger past its
  `threshold` presses a button action; keys drive axes with a `scale`
  (W = +1, S = -1 on `move.y`). Two keys held on one axis sum and clamp.
- **Shaping** on analog sources: `deadzone` (radial for stick pairs), a
  response `curve` (value^curve: >1 = finer near the centre), `invert`, and
  `scale` (sensitivity).
- **Contexts**: every action belongs to one ("game", "ui", "vehicle"...);
  disable a context and its bindings go silent (menus open, rebinding in
  progress, typing in a text field).
- **Conflicts** (same input, same modifiers, same trigger, two actions in
  one context) are shown in red but allowed: sometimes it's what you want.

## Devices and identical twins (HOSAS)

Each device gets a stable key:
`vendor:product` + serial number, or the **USB port** (Linux: the sysfs
topology, e.g. `pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0`, not
`/dev/input/eventN`, which changes between boots; Windows: the HID
instance path), or the path, or the name + an ordinal. Bindings to a raw
joystick name that device, so two identical sticks keep separate bindings.

Identical devices are grouped and numbered **#1/#2 by port**, so the same
stick stays #1 as long as it stays in the same USB port. In the Input
screen, touch a stick and its card lights up; give it a name ("Left
stick"); if you swapped ports, press **Swap twin** and the two trade names
and bindings. Names and swaps live in `input.json` under `devices`.

Gamepads are also raw joysticks, so buttons SDL doesn't map into its
standard layout (extra paddles on some pads) are still bindable as raw
buttons; the capture prefers the standard name when there is one.

**Steam Controller (2026)**: SDL 3.4 maps its R4/L4/R5/L5 as paddles and
QAM as Misc 1, with gyro. KKE turns on SDL's Steam HIDAPI driver
(`SDL_HINT_JOYSTICK_HIDAPI_STEAM`). If Steam Input is running for the game
you'll see "Steam Virtual Gamepad" instead (the real device is hidden by
Steam): turn Steam Input off for the game to use the gyro and paddles
directly.

## Rebinding (capture)

Click **+ Add** (or press A on it). Hold any modifiers first, press the
input, release: the last input pressed becomes the binding and the others
still held become its modifiers. Axis actions also accept moving the mouse,
moving a stick (binds both axes of it) or turning the controller (gyro).
Esc cancels. The button that opened the capture (pad A) is ignored until
released. While listening, the game and UI contexts are off so nothing
else reacts.

## Left-handed

`InputModule::mirrorKeyboard()` (the **Left-handed keys** button) moves
every keyboard binding to its mirror image across the keyboard: WASD ->
O K L ;, Q/E -> P/I, R -> U, Left Shift/Ctrl/Alt -> Right Shift/Ctrl/Alt,
1 -> 0. Left/right move directions are swapped back so left stays left.
Mouse and controller bindings are untouched (swap mouse buttons in the OS
as usual). Press again to mirror back. Or bind every action by hand.

## Controller navigation of menus

The "ui" context has `ui.up/down/left/right` (D-pad and left stick),
`ui.accept` (A), `ui.back` (B), `ui.prev/next` (LB/RB). `UiModule` turns
them into RmlUi focus moves and clicks (spatial navigation: RCSS
`nav: auto`, and `tab-index: auto` so Accept clicks), with key repeat
after 0.4 s. Keys aren't bound to ui.* by default because the keyboard
already reaches RmlUi directly (arrows, Enter, Tab, Esc); add them for a
custom layout. Back and prev/next are the game's to interpret.

## Split screen

`InputModule(path, players)` creates one map per player;
`assignDevices(player, {refs})` gives each player their devices. A binding
to "any gamepad" then means "any of *this player's* gamepads".

Drawing: `Application::views()` takes one camera per player, each drawn
into its part of the window (`kke/Viewports.h`: `splitScreen(1..4)`,
side by side or stacked for two, quarters for three and four, and
`pictureInPicture(corner)` for a small view over the others, like a map
or a rear-view mirror). Empty means the one camera over the whole
window, as before. Every module's `render()` runs once per view with
that view's camera, viewport and `RenderContext::viewIndex`; a module
that writes camera-dependent GPU data in `render()` keeps one copy per
view (ModelModule's culled instance lists, DebugDraw's camera-facing
lines). The UI overlay covers the whole window once. Limits: at most 4
views, and the screen-space liquid surface (`FluidSurface`, melt demo)
draws only without split screen.

In `kke_demo` (Split screen panel, or `KKE_SPLIT=2..4`): players 2-4 each
take the next controller, in the order they were plugged in; player 1
keeps the keyboard, mouse and every controller nobody else took. A
player without a controller runs the parkour lane on their own, so split
screen can be seen without any controllers. Extra players play in third
person without foot/hand IK (like network players). "Overhead view"
(`KKE_OVERHEAD=1`) adds a picture-in-picture of player 1 from above.

## Testing without hardware

`KKE_VIRTUAL_INPUT=hosas,pad` attaches two identical virtual flight sticks
and a virtual gamepad with gyro (SDL virtual joysticks);
`KKE_VIRTUAL_INPUT_ANIMATE=1` moves them (sticks sweep, buttons cycle,
gyro turns; the pad avoids menu buttons). CI runs every demo this way.
`KKE_LEFT_HANDED=1` starts `kke_demo` mirrored.

## Files

`input.json` next to the game:

```json
{ "version": 1,
  "devices": { "aliases": { "231d:0200/port:...1-2:1.0": "Left stick" }, "swapped": {} },
  "players": [ { "version": 1, "bindings": [
      { "action": "jump", "source": { "kind": "key", "code": 44 }, "trigger": "press" },
      { "action": "reload.quick", "source": { "kind": "key", "code": 21 }, "trigger": "doubleTap" },
      { "action": "ammo.check", "source": { "kind": "key", "code": 23 },
        "modifiers": [ { "kind": "key", "code": 226 } ], "trigger": "press" } ] } ] }
```

JSON (not YAML): see ACTION_PLAN.md. Written atomically (temp file +
rename). Unknown actions and malformed entries are skipped, never fatal;
actions a file doesn't mention keep their defaults (so a game update can
add actions without resetting anyone's bindings).
