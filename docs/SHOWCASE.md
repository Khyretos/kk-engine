# kke_demo: the showcase

`kke_demo` (`games/showcase/`) is the one demo that has to prove every
system at once (ACTION_PLAN 1.4, issue #12). You walk a course
where each spot shows one part of the engine. When you stand at one, the
HUD at the bottom says what to try there.

## The course

Positions are in `games/showcase/Layout.h`, so the level, the stress test
and the HUD agree.

| Station | Where | What it shows |
|---|---|---|
| Crate pile | (4, -3) | Jolt rigid bodies: push them (E / Y) or shoot them over |
| Breaking yard | (14, -6) | FEMFX glass, plank and stone wall that break for real, with crates under the glass that the shards push (FEMFX-Jolt bridge, [PHYSICS_BRIDGE.md](PHYSICS_BRIDGE.md)) |
| Parkour lane | x = 20, z 28 to 5 | Vault the fences, climb the blocks, sprint for the 2.1 m ledge, hang from the 3 m wall. Nothing is marked up: `kke::Locomotion` reads the shapes |
| Trick course | x = 26 | A 12 m wall to run along, thin pillars to leap between, and a thin wall under a beam for the leap up ([MOVEMENT.md](MOVEMENT.md)) |
| Lava | (0, 20) | Lava (`ParticleFluid`) pours on a block that melts (`MeltVolume`): ice, wax, then aluminium. Crates and the character push the lava aside. It runs only while a camera is within 18 m |
| Pool | (-18, 20) | Crates, planks and a raft float on Gerstner waves (`kke::boxBuoyancy`) and steel sinks. Vault the wall to wade in |
| Moving platform | (-14, 6) | Stand on it and it carries you |
| Low roof | (10, 6) | Crouch (C) to get under it |
| Stairs, ramp, too-steep slope, pillars | around (-8, -4) and (8, -10) | Steps, slopes the controller walks, a slope it slides off, and the camera's spring arm |

With POLYGON Nature and POLYGON Town installed, Synty art dresses the
course around the stations (`course_art.scene.json`, list in
[SCENES.md](SCENES.md)): a volcanic corner at the lava, a park pond at the
pool, a work yard at the breaking yard, hedges, pines, mountains and a town
beyond the walls. It stays out of the paths the stations need. Without the
packs the course is the plain boxes it is built from.

The **Scenes** panel visits Synty-built levels far from the course
([SCENES.md](SCENES.md)).

The **firing range** (north of the yard, the world map's second zone) has
a rifle, a pistol, rounds and grenades on its bench. Hold right mouse (LT)
to aim and fire at steel plates, FEMFX glass and stone walls that break for
real, and red barrels that blow up and set each other off. How it works is
in [games/showcase/README.md](../games/showcase/README.md#the-firing-range).

## HUD and menu

- The HUD (RmlUi, `ui/showcase_hud.rml`) shows what the character is
  doing (running, vaulting, hanging, wall running, and so on) with the
  measured speed. Near a station it shows the station's hint, plus live
  numbers where there are some, such as how much of the lava block is left.
- **Esc**, or **Start** on a controller (View opens the bag), opens the pause menu
  every KKE game shares ([GAME_SHELL.md](GAME_SHELL.md)). The HUD's corner
  shows the menu button for the device you use. The menu has Resume, Back
  to the start, 1 to 4 players (split screen), engine panels (developer
  builds), Settings, Controls, Main menu and Quit. An offline game stops while the
  menu is open. Online, the others keep playing. B on a controller closes
  it, and the d-pad and A move through it. In split screen any player's
  Start opens it.
- F1 shows the engine's ImGui panels (keyboard only: they are developer
  tools). The Showcase panel has the controls, movement tuning,
  performance, character, lighting, lava and split screen settings.

## Controls

| Action | Keyboard and mouse | Controller |
|---|---|---|
| Move, look | W A S D, mouse | Left stick, right stick |
| Sprint, walk | Left Shift, Left Alt | L3 (toggle), tilt the stick a little |
| Jump, vault, climb | Space | A |
| Crouch, let go of a ledge | C | B |
| Shoot (or throw what you hold), push | Left click, E | RT, Y |
| Aim a gun (crosshair, closer camera) | Right mouse | LT |
| Pick up / put down (items go in your bag) | F | X |
| Bag: inventory and equipment | Tab or I | View |
| World map (travel to a zone) | M | pause menu > World map |
| Spawn menu (crates, barrels, balls, a dummy; clear, reset the world) | G | RB |
| First / third person | V | R3 |
| Camera distance | Mouse wheel | D-pad up / down |
| Reset the world | R | the spawn menu's last row |
| Ping (hear the walls) | Q | D-pad left |
| Push to talk (voice builds) | P | LB |
| Lua toys: tower, ball, clear | H, B, N (or hold B) | the spawn menu |
| Break-the-targets | T | D-pad right |
| Pause menu | Esc | Start |

Split-screen players 2 to 4 move, jump, crouch, shoot, push and
zoom with their own controller. The full list, and why each key is where
it is, is in [games/showcase/README.md](../games/showcase/README.md).

## Scripted runs (screenshots, checks)

| Variable | What it does |
|---|---|
| `KKE_SKIP_INTRO=1` | No logo intro (needed headless) |
| `KKE_START_AT=x,y,z[,yaw]` | Start there, facing yaw (the camera too) |
| `KKE_DEMO_HANG=1` | Jump, hang, shimmy around the end and jump back at the lane's 3 m wall |
| `KKE_DEMO_TRICKS=1` | Wall run and wall jump, then ledge leaps along the pillars and the leap up to the beam |
| `KKE_DEMO_WORLD=1` | Shows the world map, then visits every zone in turn |
| `KKE_DEMO_ITEMS=1` | At the supply table: picks things up, opens the bag, equips an axe, a rifle, a helmet, a canteen and a lantern, then shows them worn |
| `KKE_DEMO_GUNS=1` | At the firing range: rifle at the plates, the glass and the stone walls, then the red barrels, the pistol and a grenade |
| `KKE_DEMO_BRIDGE=1` | Drops an iron ball on the yard's glass, then rolls one into a crate, and logs how far the crates moved |
| `KKE_SPLIT=2..4`, `KKE_OVERHEAD=1` | Local split screen, and an overhead picture-in-picture view |
| `KKE_MAIN_MENU=pause` | Opens the pause menu after 1.5 s (`KKE_MAIN_MENU=0`: no title screen) |
| `KKE_SCENE=town_block` | Starts in a Synty scene |
| `KKE_DEMO_AUTOPILOT=1` | The character (and extra split-screen players) run the parkour lane on their own |
| `KKE_BRIDGE=0` | Turns the FEMFX-Jolt bridge off, to compare |
| `KKE_COURSE_ART=0` | Leaves the Synty art off the course, to compare |
| `KKE_STRESS_TEST=1` | The stress test ([BENCHMARKS.md](BENCHMARKS.md)) |

## Assets

The course itself is built from boxes in code, so it needs no asset pack.
What it loads:

- Optional: the course art (POLYGON Nature, POLYGON Town), listed asset by
  asset in [SCENES.md](SCENES.md).

- The character and its animations: Quaternius' Universal Animation
  Library (CC0), volumes 1 and 2 (`assets/animations/`, fetched, not
  committed). From volume 2 it keeps only SafetyVault, ClimbUp_1m,
  ClimbUp_2m, WallRun_L_Loop and WallRun_R_Loop.
- Optional: any Synty `SK_Character` as the player (Character panel), and
  the Synty scenes. Synty packs are never committed. Each scene lists
  what it uses in [SCENES.md](SCENES.md).
- UI: Noto Sans fonts and the RmlUi theme from `games/rmlui_demo/ui/`.
