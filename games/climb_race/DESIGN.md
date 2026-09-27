# Climb Race: the flagship game

Climb Race is the engine's first full game: the one that shows what KKE
can do (generated rock, hand-by-hand climbing with IK on every limb, Jolt
rockfall, split screen, online play) and plays well enough that people
come back to it. This file is the plan; [README.md](README.md) is how to
play what exists today.

## The game in one line

Race your friends up a mountain, choosing every hold yourself, on a tour
of mountains that get bigger and meaner, with party modes for the couch.

## Pillars

1. **Every hold is a choice.** The climbing is the game. Everything else
   (mountains, modes, medals) exists to give those choices a reason.
2. **Couch first, online too.** Four on a sofa is the main case. Online
   is the same game with more screens.
3. **Readable at a glance.** Hold colours, one stamina bar, one hint line.
   A child can play it; a good climber finds a faster line.
4. **Made from data.** A mountain is a small YAML file. Anyone can copy
   one, change a few numbers and climb it (the first step toward building
   mountains in the sandbox; see "Later").

## 1. Mountains

A mountain is a data file in `mountains/` (`kke::datafile`: JSON or YAML):
its name, a line about it, the generator's knobs
(`kke::ClimbWallDesc`: seed, height, lean, hold density and mix, ledges,
loose rock) and its mood (sky and light). Picked in the start menu with a
new **Mountain** row; "Random" keeps today's endless seeds.

| # | Mountain | Height | What it teaches |
|---|---|---|---|
| 1 | Pebble Hill | 12 m | The basics: a leaning-back slab covered in jugs, one ledge |
| 2 | Meadow Crag | 20 m | Resting: two ledges, a first vertical band |
| 3 | Granite Tower | 30 m | The classic face (today's mountain): slab, wall, overhang |
| 4 | Overhang Cove | 26 m | Steep rock: lunges and jugs, stamina matters |
| 5 | Crumble Peak | 30 m | Loose rock everywhere: a lunge can break the hold |
| 6 | Cloud Spire | 46 m | The long one: crimps, four ledges, pacing |

Each has three medal times (bronze, silver, gold) set from ClimbBot runs
(Normal, Hard, Expert).

**Engine change:** `ClimbWallDesc` gets `jugBias` and `crimpBias` (extra
weight for jugs or crimps in the hold mix; 0 = today's mix, so every
existing mountain and test stays the same).

## 2. Progression: the Tour

- Your best time and medal on every mountain, saved between runs
  (`climb_race_progress.json` next to the game, JSON or YAML).
- The mountains open one after another: finish one (any medal) and the
  next opens. Pebble Hill and Random are always open. Locked mountains
  show in the menu with what opens them.
- The menu row shows the medal you have ("Granite Tower  · silver 1:12").
- Medals are for players only (a CPU winning earns nothing). In split
  screen each player earns their own, on the shared progress file.
- Later, gold medals unlock looks (chalk colours, hats) once there are
  hats to unlock.

## 3. Party modes

A **Mode** row in the start menu (the host's, online):

| Mode | Rules |
|---|---|
| Race | Today's game: first over the summit wins. |
| Time trial | Race your best run: its ghost climbs next to you (recorded poses, played back on the next face). Medals count here too. |
| Rockfall | Rocks tumble down every face from the summit (Jolt bodies). A hit costs a chunk of stamina (`kke::Climber::knock`); the higher the leader, the more come. First to the top wins. |
| Elimination | Every 30 s the lowest climber is out (they watch the leaders). Last one climbing, or the first to top, wins. |

## Milestones

Each lands on main when it builds with zero warnings, the tests pass and a
headless run is clean.

1. **Mountains** (done): `mountains/*.yaml`, the Mountain row, per-mountain mood,
   Y goes to the next mountain, online setup carries the mountain.
2. **Tour** (done): saved best times and medals, unlocking, medals on the finish
   screen and in the menu.
3. **Party modes** (done): Mode row; Rockfall and Elimination; online carries the
   mode.
4. **Time trial ghost** (done): record and replay the best run.
5. **Polish and show**: a results screen with every climber's time and
   falls, sounds for grabs, falls and medals, screenshots, docs, website.

## Later (parked, not in these milestones)

- Build and edit a mountain in the sandbox (Play to Make): place ledges
  and holds by hand, save as a mountain file.
- Lua hooks for modes, so a new party mode is a script.
- Climbing on scene meshes (a real cliff model, not only generated rock).
