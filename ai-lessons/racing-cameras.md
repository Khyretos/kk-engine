# Lesson: racing cameras, any controller, visible dents (2026-10-04)

Role: worker (C++ game code in `games/racing/`). Shared notes at the end.

## The task

Kees's feedback on the racing demo:

- He couldn't adjust the camera. He wanted a first-person view, a cockpit view, and the right stick to move the camera.
- Any controller should be able to race, even a flight stick.
- He saw no deformation when cars hit each other.

## What was done (files)

- `games/racing/Cameras.cpp` (new): seven cameras, one per player (`Car::cam`, kept in `m_playerCam[4]`).
  - Chase, far chase, cockpit, first person, bonnet, wheel and TV.
  - Right stick or mouse look (`readLook`). In first person the head moves with g-forces (`updateHead`).
- `games/racing/Controllers.cpp` (new):
  - Default flight-stick bindings (`JoyAxis` / `JoyButton` / `JoyHat`).
  - A guided wheel-and-pedals set-up, saved in `racing_wheel.json`.
- `Cars.cpp`:
  - The Synty car's `SteeringW` mesh becomes its own model, so it can turn.
  - Glass parts, the eye point and the bonnet point are found from the mesh.
- `Driving.cpp` `placeInstances`: the steering wheel turns, and the glass is hidden while you sit inside.
- `engine/src/modules/GameShellModule.cpp`: a `shell.open` action, so non-gamepad devices can open the pause menu.
- `RacingModule.h`: `m_crumpleShove` raised from 1000 to 2500, so dents are deep enough to see.

## Lessons (what happened, why, what to do)

1. **Look inside the asset before designing.** Run `strings FILE.fbx | grep -o "SK_Veh_[A-Za-z0-9_]*" | sort -u`.
   - This showed that every Synty Street Racer car has a full interior: `SteeringW`, `Seats`, `Speedometer_Needle`, `Gear_Stick`.
   - That turned "draw a fake dashboard" into "put the camera in the real seat".
   - `kke_model_info FILE` only gives counts, not part names.
2. **Glass is drawn dark and solid, so it hides the view from inside.** The renderer has no transparency for models, and there is no per-part hide.
   - Trick: collapse the glass parts' vertices to one point with `ModelModule::setDeformedVertices`.
   - Degenerate triangles draw nothing.
   - Do it only when the mode changes (it is a vertex upload).
3. **The axis of a part from its vertices.** The steering column is the direction in which the wheel's vertices spread least.
   - Find it by power iteration on the inverse of the covariance matrix (see `Cars.cpp`).
   - Then flip it so it points at the driver (`axis.z < 0`).
4. **`InputModule::setPlayers(1)` deletes maps 1 to 3.** New players copy map 0's bindings later.
   - So bind on map 0 before `commitDefaults()`. Binding the other maps in a loop is wasted.
5. **Pedals rest at one end of their axis.** At rest the value is +1 or -1, not 0.
   - A plain `Binding` has no offset, so it would read a resting pedal as a full brake.
   - The game reads them itself: `AxisSetup::amount = (v - rest) / (full - rest)`.
   - It also removes the default `JoyAxis` bindings for the actions a wheel set-up covers.
6. **GameShellModule opens on gamepad and keyboard SDL events only.** A flight stick needed a new `shell.open` action, checked in `frameStart`.
   - Menus are driven through the `ui.*` actions, so bind the joystick hat and buttons to those.
7. **Sizes, measured.** The game logs `deepest dent` every 5 s when `KKE_RACE_QUIT` is set.
   - With shove 1000, after many hits it was 0.10 m (not visible from the chase camera).
   - With 2500 it was 0.26 to 0.46 m.
   - The FEMFX cost with 3 to 5 bodies moving was 2 to 3.5 ms a step.
8. **The bonnet camera was broken before this change and nobody noticed.** It sat at 82% of the car's height, which is inside the windscreen frame of a convertible.
   - Fix: find the bonnet's top from the vertices near the middle of the front half (`CarGarage::finish`).
   - Lesson: screenshot every mode, not just the new ones.

## How to test headless (verified commands, cloud container)

```
Xvfb :99 -screen 0 1280x720x24 &       # restart it if SDL says "x11 not available"
cd build-release/bin
DISPLAY=:99 SDL_VIDEODRIVER=x11 KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_WINDOW=960x540 \
  KKE_ASSETS_DIR=/path/to/synty KKE_RACE_AUTOPILOT=1 KKE_RACE_CARS=6 \
  KKE_RACE_CAMERA=cockpit KKE_RACE_QUIT=60 ./racing > race.log 2>&1 &
import -window root -crop 960x540+160+90 shot.png   # the window sits at +160+90 on a 1280x720 screen
```

- `KKE_RACE_CAMERA` takes `chase far cockpit first bonnet wheel tv` (or 0..6).
- A dent test:
  - `KKE_RACE_CRASH=1` or `KKE_RACE_BENCH=pileup` with `KKE_RACE_CARS=8`.
  - Then `grep deepest race.log`.
- Pressing keys with xdotool: a quick `xdotool key c` is often missed at about 10 fps.
  - Hold the key instead: `xdotool keydown c; sleep 0.4; xdotool keyup c`.
  - Focus the window first: `xdotool windowfocus --sync $(xdotool search --name Racing | head -1)`.
- Mouse look: `xdotool mousedown 3`, then `mousemove_relative -- 30 0` in a loop, then `mouseup 3`.
- `KKE_VIRTUAL_PAD_SCRIPT` drives a virtual pad. But with `KKE_RACE_AUTOPILOT` the player's map only listens to the keyboard, so the pad is ignored. That is correct behaviour, not a bug.
- The whole race runs at about 1/3 real time on llvmpipe (4 to 15 fps), so allow 35 s before the race starts.
- Zero warnings:
  - `cmake --build build-release` with GCC, and grep for `warning:`.
  - Clang as well, because the Android CI uses clang with `-Werror` and clang warns where GCC doesn't.
  - For clang, take each changed file's command from `build-release/compile_commands.json`, change the compiler to `clang++`, drop `-c` and `-o X`, and add `-fsyntax-only -Werror -Wno-unknown-warning-option`.
- Tests: `build-release/bin/kke_tests`.

## Could a 9B local model do this alone?

Partly.

- **It could do:** the camera maths for chase, bonnet and TV, and the README.
  - Give it `Cameras.cpp` as the example and the car-space convention: +Z forward, +X left, yaw + to the right.
- **It would struggle with:**
  - the glass trick (it needs to know there is no per-part hide and that degenerate triangles draw nothing);
  - the pedal rest offset;
  - the `setPlayers` map copying;
  - the shell's event-only pause.
  - These are facts about this engine that aren't in any public doc.
  - Give it lessons 2, 4, 5 and 6 word for word, and one step at a time: cameras first, then the controllers, then the set-up flow.
- **It would need a person or a bigger model for:** judging the screenshots (is the eye too low, is the bonnet camera inside the glass?).
  - Qwen3.5-9B is vision-capable. Give it the screenshot and ask one yes/no question at a time ("Is the steering wheel visible at the bottom middle?").
