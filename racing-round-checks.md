# Racing round: checks to run on soucouyant

Patch: `racing-round.patch`. It is one commit on top of Forgejo `main` f44c551e0c17abbe8fc7b10fccace857bec10078.

- Apply it with `git am racing-round.patch`.
- It changes 13 files: `games/racing/*` and `engine/src/modules/GameShellModule.cpp`.
- Racing's README sections "Controls" and "Split screen and cameras" describe everything below.

Already checked in the cloud (lavapipe, 4 cores):

- All seven cameras were screenshotted.
- The bonnet camera was fixed.
- In pile-ups, dents reach 0.26 to 0.46 m.
- The pause menu shows the new item.
- The racing game log had zero warnings.
- Clang `-fsyntax-only -Werror` passed on the 7 changed C++ files.
- **Not run:** the full GCC build and `kke_tests`, because they were stopped when the work moved to soucouyant.

Before each `check_game` run, set `KKE_ASSETS_DIR` to the Synty folder. Without it you get block cars, and the cockpit has no dashboard.

## 1. Build and tests (zero warnings)

```
tools/runner/kkrun build        # last line must be PASS, 0 warnings
tools/runner/kkrun tests        # PASS
```

## 2. Every camera, one screenshot each

`KKE_RACE_CAMERA` picks the camera, so no key presses are needed. `KKE_RACE_AUTOPILOT=1` drives the car.

```
for cam in chase far cockpit first bonnet wheel tv; do
  KKE_RACE_AUTOPILOT=1 KKE_RACE_CARS=6 KKE_RACE_CAMERA=$cam \
    tools/check_game racing --seconds 40 --shot racing-$cam.jpg
done
```

Look at each shot:

| Shot | Must show |
|---|---|
| chase | the car from behind, road ahead |
| far | the car smaller, higher, the pack around |
| cockpit | the dashboard and gauges at the bottom, the steering wheel in the bottom middle, the A-pillar at the side; no dark glass blocking the view |
| first | the same, a little narrower; in a banked turn it looks into the corner (on the speedway, turns are left) |
| bonnet | the front half of the bonnet at the bottom, the road ahead; NOT a black shape filling the screen |
| wheel | the front tyre from the side sill |
| tv | the car from beside the track, far away |

Every `check_game` run must end `OK` with no warnings.

## 3. Dents, up close

```
KKE_RACE_BENCH=pileup KKE_RACE_CARS=8 KKE_RACE_CAMERA=chase KKE_RACE_QUIT=80 \
  tools/check_game racing --seconds 80 --shot racing-dents.jpg
grep "deepest dent" <the game log>
```

- The deepest dent should reach about 0.3 m or more.
- In the shot, the fronts of wrecked cars are visibly folded.
- On the real GPU, also play one derby by hand (`KKE_RACE_TRACK=scrapyard_bowl`). Hits should leave dents you can see from the chase camera.
- The cost line `FEMFX x ms a step` should stay under about 4 ms.

## 4. Looking round and switching cameras (by hand, or xdotool)

- **Keyboard.** Press `C` (hold it about 0.4 s when using xdotool at low fps). It goes Chase, Far chase, Cockpit, First person, Bonnet, Wheel, TV, then back to Chase.
  - Each press logs `<name>: <camera> camera`.
  - "Camera: X" shows in the player panel.
- **Mouse.** Hold the right mouse button and move the mouse sideways. The chase camera swings round the car. Let go, and it swings back after about 0.6 s.
- **Gamepad right stick.**
  - Right: look right.
  - Down: look behind.
  - Let go: it comes back.
  - Inside the car it turns the head, up to about 125 degrees.
- **Split screen with 2 players.** Each player has their own camera. A cockpit for player 1 does not change player 2's view.

## 5. Shared menus

- Esc or Start opens the pause menu. It has a "Set up a wheel or flight stick" row.
- Settings has a "Driving" section with:
  - "Camera" (sets everyone's camera);
  - "Set up a wheel or flight stick".

## 6. Flight stick and wheel (needs the hardware, or `KKE_VIRTUAL_INPUT=hosas`)

**Flight stick, no set-up:**
- the trigger joins in the start menu;
- the stick steers;
- push forward for gas, pull back to brake;
- button 2: camera;
- the hat looks round;
- button 7: pause menu;
- in the menus, the hat moves, the trigger picks and button 2 goes back.

**Wheel and pedals:** choose "Set up a wheel or flight stick", then:
1. turn the wheel left and hold;
2. turn it right and hold;
3. press the gas;
4. press the brake.

- Each step shows a green bar that fills. B or Backspace skips a step.
- It ends with "All set".
- `racing_wheel.json` appears next to the game.
- Then race. Steering, gas and brake should follow the wheel, and the brake must not be stuck on at rest.

## 7. Open the PR

Once checks 1 to 5 pass, open a PR on Forgejo (it gets the AI review and the lint report).
Don't push straight to `main`, and never push to GitHub.
