# Touch controls

Every KKE game plays on a phone, a tablet or a touch screen with fingers
alone. A game gets on-screen controls without writing any code:

- a **stick** in the bottom-left corner that moves the player (or
  steers, or flies: whatever the game put on a controller's left stick);
- a **look drag**: anywhere free on the screen, drag to look around, as
  fast as the finger moves;
- round **buttons** in the bottom-right corner for the game's actions
  (jump, fire, throttle...), the thumb's home button biggest;
- a **pause** button in the top-right corner that opens the game's menu.

They appear once a finger touches the screen (from the start on phones)
and hide again when the keyboard, the mouse or a controller is used,
while a menu or the start menu has the screen, and while the game turns
its own controls off.

Players **move, resize and rebind** them: pause, **Controls**,
**Touch controls**. The game stays frozen behind its controls; drag one
to move it, tap one to pick it, then use the bar along the top:

| Bar button | Does |
|---|---|
| Smaller / Bigger | resizes the picked control |
| < Action / Action > | what the picked button does (any of the game's actions); on a stick: left or right controller stick |
| Hold / Toggle | held while touched, or a tap turns it on and the next one off (crouch, sprint) |
| Remove / Add button | takes the picked control away; adds a button for an action that has none |
| Fainter / Clearer | how see-through all the controls are |
| Look slower / faster | how far a drag turns the view |
| Reset | the game's own layout back |
| Done | saves (Esc and B do too) |

The layout is saved in the game's input file (`input.json` or the name
the game gave its `InputModule`) under `"touch"`, next to the bindings.
Controls keep to the nearest corner or edge, so a layout made in
landscape still fits when the phone turns, and they always stay whole
inside the screen's safe area (notches, rounded corners).

## How a game's layout is chosen

`kke::TouchControls::autoLayout` (`kke/TouchControls.h`) reads the
game's actions:

- a **left stick** when any action is bound to a controller's left stick;
- the **look drag** when `look.rate` exists, else an Axis2D action on the
  right stick (never the orbit camera's `camera.*`: orbit-camera demos
  already turn with one finger and pinch with two);
- **buttons** for the actions a controller player presses, A first, then
  X, B, Y, the triggers, the shoulders, the stick clicks and the d-pad,
  up to six. Start, Back, menu (`ui.*`, `shell.*`, `pause.*`) and
  developer actions are never buttons. An action a pad *toggles* becomes
  a toggle button;
- a **pause** button.

The sticks are a controller's sticks: they feed "any controller's" stick
into player 1's map, so the game's own deadzones, inverts and Axis1D
splits (racing's steering on the left stick's X) all apply. Buttons hold
an action: a Button action is pressed (`InputMap::setScreenButton`), an
Axis1D action is held at 1 (`InputMap::setScreenAxis`), e.g. a racing
game's throttle. The look drag sets its axis action to how far the
finger moved this frame per screen short side per second, so a game that
turns `rate * speed * dt` turns by the distance dragged.

Steer the guess in the game's `init()`, before `commitDefaults()`:

```cpp
kke::TouchLayoutOptions touch;
touch.buttons = { "throttle", "brake", "handbrake", "reset.car" }; // these, in this order
touch.look = kke::TouchLayoutOptions::Look::Stick; // a right stick instead of the look drag
m_input->setTouchLayout(touch);
m_input->commitDefaults();
```

Use `Look::Stick` when a tap on the world matters (pointing at a spot to
give an order: `platoon`, `pet_companion`), because the look drag takes
every free touch. `Look::None` drops looking. For a layout of your own,
`m_input->touch().setDefaults({...})` with `kke::TouchControl`s.

Actions a Lua script defines later (`input.define`) are picked up: the
guess is made again whenever player 1's actions change, and a player's
saved layout goes back over it.

## What takes a finger

`InputModule` claims touch events before every other module
(`Application::addEventClaim`):

1. A finger on a menu button or a tappable prompt
   (`<button>`, `data-kke-action`, ...) is the menu's.
2. A finger on a control is the control's.
3. Any other finger is the look drag's, when there is one.
4. Otherwise it goes to the game as usual: SDL's touch-to-mouse turns it
   into a click (picking, orbit camera, sandbox tools).

A claimed finger is never also a click, a camera drag or a gesture in
the game. Code that reads the mouse state directly (not events) should
skip it while `InputModule::touchHasMouse()` is true; the orbit camera
does.

## Menus with a finger

- The shared menus (`GAME_SHELL.md`) are RmlUi: a tap is a click.
- The start menu (`LOBBY.md`): a tap on a row steps its value (the
  left half of the value steps back), presses an action row or Start;
  a tap on a key of the on-screen keyboard types it.
- A game's own menus need the same: the flying demo's pause menu takes a
  tap on a row (`FlyingModule::onEvent`).

## Trying it on a PC

```bash
KKE_PROMPT_STYLE=touch KKE_WINDOW=540x1200 KKE_TARGET=android ./build/bin/climb_race
```

`KKE_PROMPT_STYLE=touch` shows the controls at once; the mouse cannot
press them (it is a mouse, not a finger), but **Touch controls** in the
pause menu's Controls page edits them with the mouse. A touch screen on
Linux or Windows sends real fingers. The log names the guessed layout:
`touch controls: stick left, look look.rate, button jump, ...`.

The logic is tested in `tests/test_touch_controls.cpp` (sticks, buttons,
quick taps, toggles, the look drag, editing, saving) and the start
menu's taps in `tests/test_lobby.cpp`.
