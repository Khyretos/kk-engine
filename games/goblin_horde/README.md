# Goblin Horde

Hold the old fort on the hill against waves of goblins, alone, with
friends on one screen, online, or both at once. Pick a hero and a main
weapon in the lobby: a sword, a great axe, a bow or a crossbow. Five kinds
of goblin come through the four gateways: grunts with clubs and swords,
archers, battle shamans throwing fire, war shamans who heal and buff the
others, and brutes with two-handed axes. Every fifth wave a giant comes
with them. The nearest few go at each hero while the rest circle and jeer,
waiting their turn. Each kill shakes the wave's nerve, and when it breaks
the last of them run for the trees.

The demo teaches crowd AI on the engine AI core
([docs/AI.md](../../docs/AI.md)), one-against-many melee on `kke::Combat`
([docs/COMBAT.md](../../docs/COMBAT.md)), enemies and weapons as data
(YAML), telegraphed boss attacks, projectiles with charge and aim,
split screen through the lobby ([docs/LOBBY.md](../../docs/LOBBY.md)),
host-run online play on `NetModule`
([docs/NETWORKING.md](../../docs/NETWORKING.md)), clips from one
animation library retargeted onto many Synty skeletons, capped Jolt
ragdolls and a procedural hit flinch. Start here for a hack and slash
game, a co-op wave survival game, or any game with dozens of enemies on
screen at once.

![A hero in the ruined fort surrounded by goblins at sunset](../../website/static/media/goblin-horde.webp)

## Run it

```bash
cmake --build build --target goblin_horde
cd build/bin
./goblin_horde
KKE_SKIP_INTRO=1 ./goblin_horde                     # skip the logo intro
KKE_HORDE_BOT=1 KKE_HORDE_QUIT=60 ./goblin_horde    # a bot fights alone for a minute, with a report every 10 s
```

The root `CMakeLists.txt` adds it only when `KKE_ENABLE_JOLT` is on (the
default).

| Variable | Effect |
|---|---|
| `KKE_HORDE_BOT=1` | a bot plays (attract mode, headless runs); skips the title and lobby |
| `KKE_HORDE_WEAPON=<id>` | the first hero's weapon: `sword`, `greataxe`, `bow`, `crossbow` |
| `KKE_HORDE_WAVE=<n>` | start (and restart) at wave n |
| `KKE_HORDE_BOSS=1` | a giant comes in the first wave |
| `KKE_HORDE_LINEUP=1` | every goblin type and giant stands in a row and shows its moves in turn (for checking the art) |
| `KKE_HORDE_POSE=<clip>` | every goblin plays that UAL clip (checking a clip on every skeleton) |
| `KKE_HORDE_LOBBY=0` | skip the lobby: one player, keyboard and the first pad |
| `KKE_HORDE_MAX=<n>` | goblins alive at once, at most (default 60) |
| `KKE_HORDE_RAGDOLLS=<n>` | ragdolls at once (default 12; 0 = the dead just topple) |
| `KKE_HORDE_QUIT=<s>` | quit after that long |
| `KKE_ASSETS_DIR`, `KKE_SYNTY_DIR` | where the Synty packs are (else `assets/synty`) |

Rebindings are saved to `horde_input.json`, lobby choices to
`horde_lobby.json` (both named in [main.cpp](main.cpp)).

## Controls

Bound in `HordeModule::defineActions` ([HordeModule.cpp](HordeModule.cpp)),
on top of the engine's character actions (move and look). Every seat in
the lobby gets its own copy, so each player can rebind theirs.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Move (relative to the camera) | WASD | left stick |
| Look | mouse, once the view has the mouse | right stick |
| Attack: a three-hit combo; with a bow, hold to draw and let go to shoot (`horde.attack`) | left mouse / J | X (west), or RT |
| Heavy swing; hold to charge a 360° spin; a kick with a bow (`horde.heavy`) | F / K / middle mouse | Y (north) |
| Aim a bow or crossbow (`horde.aim`) | right mouse | LT |
| Block, held; just before a blow lands it parries (`horde.block`) | Left Shift | RB |
| Roll (`horde.roll`) | Space | A (south) |
| Next weapon (`horde.swap`) | Q | d-pad right |
| Inventory: choose a weapon (`horde.inventory`) | Tab / I | d-pad up |
| Pause menu (`shell.pause`, from the game shell) | Esc | Start |
| Try again after being overrun (`horde.again`) | R | Start |

Esc frees the mouse and opens the pause menu (resume, inventory, start
over, settings, quit). Closing the window (Super+Q on Hyprland, the
close button elsewhere) quits like any other game.

## How it plays

- **Lobby.** Each player presses a button on their device to take a seat
  (up to four on one screen) and picks a name, character, skin,
  accessory, main weapon and colour. The `Online` row hosts or joins a
  game; local seats and online players mix freely (up to 16).
- **Waves.** [data/waves.yml](data/waves.yml) lists the waves: how many
  goblins, how many at once, speed, health, and the `mix` of types. After
  the last, it repeats the last wave with more goblins each time. Waves
  grow by `perPlayer` (60%) for every hero after the first.
- **Taking turns.** Only `attackers` goblins (4) go at each hero; the rest
  wait in rings and step in as the ones in front fall.
- **Morale.** Kills lower the wave's morale; below 0.15 they break and
  run. Clearing a wave heals every standing hero 40% and gives a
  `breather`.
- **Bosses.** Every `bossEvery` (5) waves a giant comes with the wave. Its
  health grows by half for each extra hero. The boss bar shows its name
  and health.
- **Down and out.** A hero at zero health is down; when every hero is
  down, the fort is overrun and `horde.again` starts over.

### Weapons ([data/heroes.yml](data/heroes.yml))

| Weapon | Attack | Heavy | Notes |
|---|---|---|---|
| Sword | three-hit combo, the third the strongest | heavy swing; hold for a 360° spin | fast, one hand |
| Great axe | slow three-hit combo | two-handed overhead that can't be blocked; hold to spin | slow, wide, hard |
| Bow | hold to draw (1.1 s to full), let go to shoot: 8 damage at a tap, 46 at full draw, ×2 on the head | a kick to make room | aim with the camera; a pad gets a 4° aim assist |
| Crossbow | one bolt, 34 damage whether aimed long or not, ×1.5 on the head | a kick | 1.7 s reload after every shot |

### Goblins ([data/foes.yml](data/foes.yml))

Each type has three moves, picked by range and situation:

| Type | Moves | Looks |
|---|---|---|
| Grunt | slash, double cut, lunge | War Camp warriors, knights, prisoners, cooks; Dungeon goblins |
| Archer | kick, volley (arrows rain on a marked circle), aimed shot | War Camp archers and rangers, with quivers; keeps 9 m away |
| Battle shaman | firestorm (a circle that bursts), fireball, zap | War Camp wizard, Dungeon shaman |
| War shaman | heal the most hurt goblin, war cry (haste), stone skin (takes half damage) | War Camp shaman and beast tamer |
| Brute | overhead slam, sweep, charge | Dungeon war chief, War Camp king; two-handed axe or hammer |

### Giants

The troll, Grukk the big ork, the barbarian giant and the pig butcher
(POLYGON Fantasy Rivals). Each has three moves, every one marked on the
ground before it lands: a thrown boulder (a circle where it will fall), a
basic combo (a cone in front), and a super slam (a big circle around the
giant).

## How it works

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The application, the game shell (title, pause, settings), lobby and network modules |
| [HordeModule.h](HordeModule.h) | The module and its structs: heroes, foes, shots, marks, HUD |
| [HordeModule.cpp](HordeModule.cpp) | Controls, the lobby, the arena and navmesh, waves and phases, cameras per player, drawing projectiles and marks, the headless report |
| [Roster.h](Roster.h), [Roster.cpp](Roster.cpp) | Reads `foes.yml`, `heroes.yml` and `waves.yml` into weapons, moves and goblin types |
| [Art.h](Art.h), [Art.cpp](Art.cpp) | Loads characters and props from the packs, retargets UAL clips onto each skeleton, places props in hands (grips) |
| [Puppets.cpp](Puppets.cpp) | One animated body: clip states per move, posing, weapon and hat attachments |
| [Heroes.cpp](Heroes.cpp) | Players and bots: input, combos, heavy and spin, bow draw and crossbow reload, aim, soft lock, two-hand IK |
| [Foes.cpp](Foes.cpp) | Spawning and pooling goblins, choosing moves, telegraphs, movement, death, ragdolls, animation |
| [Shots.cpp](Shots.cpp) | Arrows, bolts, fireballs, boulders, ground marks and blasts, and every hit |
| [Hud.cpp](Hud.cpp), [ui/horde_hud.rml](ui/horde_hud.rml) | A HUD panel per player in their part of the screen, the boss bar, the inventory |
| [Net.cpp](Net.cpp) | Online play: the lobby's Online rows, player states, the horde from the host |
| [Flinch.h](Flinch.h) | The procedural hit flinch |
| [data/goblin.yml](data/goblin.yml) | The goblin minds for the AI core (charge, wait, keep distance, break) |

### Minds are data, bodies are code

Every type thinks with the `goblin` species in `goblin.yml` (charge,
wait their turn, break); the game feeds it `crowded`, `morale` and
`health`. What a goblin can *do* is its `moves` list in `foes.yml`: a goblin
picks the first move that is ready and in range. A move's `kind` decides
what happens: `melee`, `dash`, `shot`, `lob`, `blast`, `slam`, `combo`,
`buff`, `heal`. Adding a goblin type is a YAML edit and a restart.

### One clip library, many skeletons

Every character, goblin and giant plays Quaternius's Universal Animation
Library clips (UAL 1 and 2), retargeted by bone name
(`kke::retargetAnimations`). The repository ships the 43-clip
`UAL1_Standard.fbx`; when the full pack is there, `Art.cpp` adds the rest
of `UAL1.fbx` (kicks, spells, strafes). The War Camp goblins use Synty's
older bone names (`Shoulder_L`, `Elbow_L`, `UpperLeg_L`, `Ankle_L`);
`kke::canonicalBoneName` maps them to UAL's (`upperarm_l`, `lowerarm_l`,
`thigh_l`, `foot_l`), and the ragdoll builder looks bones up the same way.
Without that mapping those goblins stood in a T-pose and had no ragdoll.

### Telegraphs

A move with a ground mark (`telegraph`, or every giant move) draws a
glowing outline that fills up until the blow lands: a circle for slams,
lobs and blasts, a cone for combos, a line for charges (`buildMarkMesh`
in [Shots.cpp](Shots.cpp)).

### Split screen and online

The lobby gives every local seat its own input map and a part of the
screen (`kke::splitScreen`); each hero has a camera, a HUD panel and an
inventory in that part. Online, the host runs the horde: it sends wave
state and every goblin (position, facing, move, health) 15 times a second;
clients draw them, send their own hero's state, and report their hits on
goblins to the host, who applies them. Hits on a hero go to the player who
owns it. A client starts when the host's game leaves the lobby.

### Crowd cost

Goblins are not physics bodies: they move kinematically with a push-out
from walls and heroes. Bodies are pooled and reused. Ragdolls are capped
(`KKE_HORDE_RAGDOLLS`), oldest first. The bot report every 10 s prints
the frame time and the time spent on minds and animation.

## Assets

The game runs without any pack: mannequins stand in for every character
and boxes for weapons, and the log says what was missed. With the packs
(`kke_assets needs` prints the required ones):

| Pack | What it gives |
|---|---|
| POLYGON Goblin War Camp | most goblins, their bows, staffs, clubs, swords, quivers and hats; hero accessories |
| POLYGON Dungeon Pack | the knight heroes, goblin chiefs and shamans, the great axe, big hammers |
| POLYGON Fantasy Rivals | the four giants |
| POLYGON Fantasy Characters | the heroes (king, queen, rogue, druid, bard, witch, sorcerer, peasant) and the sword |
| Universal Animation Library (full `UAL1.fbx`, optional) | kicks, spells and the rest beyond the shipped Standard set |
| Universal Animation Library 2 | sword combos, heavy swings, knees |

The repository ships `UAL1_Standard.fbx` (CC0), the fonts and Xelu's CC0
button prompts. The fort, trees and rubble are boxes built in code.

## Make a game like this

1. **Copy the folder.** `cp -r games/goblin_horde games/my_horde`, rename
   the target and namespace, and add it to the root `CMakeLists.txt` inside
   `if(KKE_ENABLE_JOLT)`.
2. **New enemies are data.** Add a type to `data/foes.yml` with models,
   a weapon and three moves; add it to a wave's `mix` in
   `data/waves.yml`. Check it with `KKE_HORDE_LINEUP=1`.
3. **New weapons are data.** Add one to `data/heroes.yml` with `kind`
   (`onehand`, `twohand`, `bow`, `crossbow`), a prop, a `combo`, `heavy`
   and `spin`.
4. **Check the art.** `KKE_HORDE_LINEUP=1` for every type, then
   `KKE_HORDE_POSE=Idle_Loop` if one stands in a T-pose (its bone names
   don't match; see "One clip library").
5. **Measure.** `KKE_HORDE_BOT=1 KKE_HORDE_QUIT=60` and read the report.

Pitfalls:

- Goblins move without physics, so add a box to `m_obstacles` for every
  solid thing you add, or knockback pushes them through it.
- A prop's grip decides its axes (`grip()` in [Art.cpp](Art.cpp)): a bow
  stands up along the arm's forward, a crossbow lies flat across it.
- Online, only the host decides damage to goblins; a client that applies
  its own hits would kill goblins the host still sees alive.
