# Sea demo

Pirate ships on an open sea. Pick a ship from Synty's POLYGON Pirate Pack,
from a rowing boat up to a three-masted man-o'-war, each with its own
speed, turning and guns. Set the sails, steer with the wind, hold the aim
button to see where a broadside will fall, and fire. Cannonballs fly in a
real arc, splash, punch dents and splinters into hulls, snap masts and
sink ships; wrecks, masts and splinters float on the same waves you see.
An island fort's wooden palisade splinters for real with FEMFX in builds
that have it. Up to four people share one screen (split screen), and a
friend can sail along online.

It is a showcase, not a whole game: three modes (battle, target practice,
free sail), a settings panel for wind, waves, guns and enemies, and
everything a naval game needs underneath. Start here for a pirate game, a
naval shooter, a sailing game or anything that floats.

![A brig under sail on the swell](../../website/static/media/sea.webp)

## Run it

```bash
cmake --build build --target sea_demo
cd build/bin
./sea_demo
KKE_SKIP_INTRO=1 ./sea_demo         # skip the logo intro
KKE_MOOD=stormy ./sea_demo          # the same sea under another mood
```

The ships need the POLYGON Pirate Pack (see [Assets](#assets)); without it
they are built from boxes and everything else works the same.

Developer switches (ignored in shipping builds):

| Variable | Effect |
|---|---|
| `KKE_SEA_START=1` | Sail at once with the start menu's saved choices (headless checks) |
| `KKE_SEA_MODE=0/1/2` | Battle, target practice or free sail |
| `KKE_SEA_ENEMIES=n` | n enemy ships (0-6) |
| `KKE_SEA_SHIP=0-4` | Player 1's ship: rowing boat, longboat, schooner, brig, man-o'-war |
| `KKE_NET=host`, `KKE_NET=join:ADDRESS` | Host or join online at startup |
| `KKE_LOBBY_JOIN=n` | n more controllers join the start menu ([LOBBY.md](../../docs/LOBBY.md)) |

```bash
# A battle with three enemies, headless, with a screenshot:
KKE_SEA_START=1 KKE_SEA_ENEMIES=3 tools/check_game sea_demo --seconds 30 --shot sea.jpg
```

## Controls

All actions are rebindable (Settings, Controls); `SeaDemoModule::defineInput`
in [SeaDemoModule.cpp](SeaDemoModule.cpp) makes them.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Steer | A / D or Left / Right | left stick, sideways |
| Sails: up a step / down a step (oars: row) | W / S or Up / Down | left stick, up / down |
| Aim the guns (shows the arc and where it lands) | hold right mouse button | hold LT |
| Fire a broadside | left click or Space | RT |
| Turn the camera | Q / E, or drag with the right or middle button | right stick |
| Camera closer / further | mouse wheel | d-pad up / down |
| Next ship | Tab | Y (north) |
| Throw something overboard | T | d-pad right |
| Next thing to throw | G (or keys 1-5) | d-pad left |
| Pause menu (Change ship, Restart, Settings) | Esc | Start |

The guns fire to the side the camera looks at: look left of the bow and
the port guns fire, right and the starboard guns fire. Look up to aim
further. With **Aim help** on, the guns also pick up the range to the
nearest enemy on that side and lead it.

## How it plays

The start menu (kke::LobbyModule) picks each player's ship, the mode,
the enemy ship type, how many enemies (the CPU row) and how good they are,
and Online (Off, Join, Host). Another controller joins with A for split
screen.

| Ship | Speed | Turning | Guns | Feel |
|---|---|---|---|---|
| Rowing boat | 3.6 m/s, rowed | 45°/s | 1 swivel gun a side | darts around, sinks in a few hits |
| Longboat | 4.6 m/s, rowed | 32°/s | 2 a side | steady, no wind needed |
| Schooner | 9.5 m/s | 20°/s | 4 a side | quickest under sail, light guns |
| Brig | 8.5 m/s | 13°/s | 8 a side | the all-rounder (default) |
| Man-o'-war | 7 m/s | 8.5°/s | 7 a side on two decks, 24-pounders | slow to start and turn, hits hardest |

Sailing ships carry way: they speed up and slow down slowly, turn faster
at speed, and go fastest with the wind from behind (turn **Wind fills the
sails** off for the same speed in every direction). Rowing boats ignore the
wind.

- **Battle**: enemy ships approach, turn broadside-on and fire back.
  Sunk enemies come back unless that row is off; a sunk player gets a new
  ship after a few seconds.
- **Target practice**: enemies lie at anchor closer in, waiting to be sunk.
- **Free sail**: no enemies; throw things overboard and watch the sea.

## How it works

### Ships (`Ships.h`, `Ships.cpp`)

`ShipClass` is one row per ship: the Synty file names of its hull, masts,
sails and rigging, its size, handling, guns and health. `ShipArtLibrary`
loads each class's parts once. The parts of one ship share an origin, so
they load raw (`fixUnitMismatch = false`) and the whole set is scaled
by 100 when the hull is in centimetres (raw length under 2). Each sail is
matched to the nearest mast by position, so a snapped mast takes its sails
with it. The hull's vertices are kept for dents.

### Sailing (`SeaDemoModule::sailShip`)

A ship is one `kke::FloatingBody` box (470 kg/m³, low centre of mass), so
buoyancy, pitching and rolling come from the waves. On top: thrust from
the sails (scaled by the wind's angle) or the oars against quadratic drag,
a keel force that stops it sliding sideways, and a rudder that sets a yaw
rate, stronger at speed. Water taken on through damage adds mass, so a
holed ship sits lower and slower before it sinks. Ships push each other
apart with capsule contacts along the hull (`FloatingBodies`), so two
long hulls can sail side by side.

### Guns (`Battle.cpp`)

A broadside is a rolling volley: each gun fires a few hundredths of a
second after the last, from its own muzzle along the side, with a little
spread and recoil. Balls are point masses under gravity with no drag, so
the predicted arc (`predictArc`) is exactly the path they fly, and the
ring shows where the first ball lands. Muzzle speed (80 m/s default) and
elevation set the range; the camera's pitch sets the elevation, or **Aim
help** solves it for the nearest enemy.

### Damage

Hits are cheap on purpose (the ships never use FEMFX): a ball that
crosses the hull dents the Synty mesh around the impact point (vertices
pushed in, uploaded with `ModelModule::setDeformedVertices`), throws
splinters (small floating planks), sparks and smoke, takes health and can
start a fire. A ball through a mast snaps it: the mast and its sails fall
into the sea as a floating body. At zero health the ship lists, settles
and sinks; its masts and splinters keep floating for a while.

### Effects (`Effects.cpp`)

`kke::ParticleEffects` (smoke and sparks, 9000 at most): wake foam along
the sides and spray at the bow (more with speed), muzzle flashes and white
powder smoke, water columns and foam rings where balls land, dust on
islands, fire and black smoke on burning ships. Flags stream downwind and
sails swell with the wind.

### The sea and the world (`World.cpp`)

`kke::OceanWaves` (Gerstner swell) is drawn by `kke::OceanRenderer` out to
the horizon: a graded grid (fine near the camera, coarse far away) whose
waves fade out where the cells get too big to show them, so the far sea
never flickers. Islands are Synty background islands and rocks (low mounds
without the pack); ships scrape and stop on them. The fort island has a
tower, palms and, with FEMFX, a palisade of five wooden panels.

### The FEMFX fort

Each palisade panel is a `PhysicsModule::spawnPatternedBox` with the
`Splinters` fracture pattern: wood that breaks along the planks. A
cannonball that reaches the wall is handed to FEMFX as an iron sphere at
30 m/s (made heavier to keep its momentum, because at 80 m/s it would jump
through a plank between steps). The panels spawn unbreakable and arm
once they have settled, so they never fall apart under their own weight.
**Rebuild the fort** in the panel puts it back. Builds without FEMFX
(`KKE_ENABLE_FEMFX=OFF`, the default) have no palisade; the panel says so.

What it costs (the panel's fort row shows it live): standing, the panel
pieces sleep and FEMFX takes about 0.1 ms a step. While a wall breaks, each
moving splinter costs about 0.15-0.2 ms ([SCALING.md](../../docs/SCALING.md)),
so the debris budget keeps 60 splinters at most; six balls into the wall
peaked at 8-12 ms a step on a 4-core cloud machine and settled back to
0.1 ms within ten seconds.

### Online (`SeaNet.cpp`)

Every ship is a network player: each screen sends its captains' ships,
and the host also sends its enemy ships as extra local players. A ship's
state carries its position, orientation, class, team, health, sails and
which masts stand. Shots go to everyone as events (drawn, not simulated,
on the other screens). A hit is applied by the ship's owner: whoever sees
their ball hit a ship they don't own sends a hit event, so a friend's
damage, sinking and score stay consistent. A client's own enemies make
way for the host's. The friend gets the same options as the host.

## Notes from Black Flag

Assassin's Creed IV: Black Flag is the reference for the feel:

- Sails in steps (furled, half, full) instead of a throttle, and a ship
  that carries way.
- The camera chooses the guns: look to a side and hold aim, and that
  side's broadside shows where it will land; look up for range.
- A rolling broadside reads better than every gun at once.
- Readable impacts: water columns, splinters, smoke and fire tell you how
  a fight is going without a health bar.

Here the arc is the real ballistic path (no drag), so a long shot is
aimed high and a close one flat, the way a gun crew would.

## Tuning

| What | Where |
|---|---|
| Ship speed, turning, guns, health | `kClasses` in [Ships.cpp](Ships.cpp) |
| Wind, waves, ball speed, damage, aim help | the settings panel (F3, or the pause menu) |
| How enemies fight | `thinkEnemy` in [Battle.cpp](Battle.cpp) |
| Fort wood strength, size, debris budget | `buildFort` in [World.cpp](World.cpp) |
| Particle budget | `ParticleEffects(..., 9000)` in `SeaDemoModule::init` |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| Gerstner ocean, CPU and GPU | `kke::OceanWaves`, `kke::OceanRenderer` | this page |
| Buoyant bodies with capsule contacts | `kke::FloatingBodies` (`tests/test_floating_bodies.cpp`) | [cookbook: physics](../../docs/cookbook/physics.md) |
| Synty models, dents | `kke::ModelModule`, `kke::AssetCatalog` | [ASSETS.md](../../docs/ASSETS.md) |
| Breakable wood | `kke::PhysicsModule` (FEMFX) | [SCALING.md](../../docs/SCALING.md) |
| Particles | `kke::ParticleEffects` | |
| Start menu, split screen | `kke::LobbyModule`, `kke::splitScreen` | [LOBBY.md](../../docs/LOBBY.md) |
| Title, pause, settings | `kke::GameShellModule` | [GAME_SHELL.md](../../docs/GAME_SHELL.md) |
| Online | `kke::NetModule` | [NETWORKING.md](../../docs/NETWORKING.md) |
| Settings panel | `kke::DemoPanelModule` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Third-person camera | `kke::CameraRig` | |

Nothing of the ocean or ships is exposed to Lua yet, so a game of this
kind is C++ for now.

## Assets

Synty **POLYGON Pirate Pack** (`POLYGON_Pirate_Pack`), found at runtime in
`assets/synty/` or `KKE_ASSETS_DIR`; never committed. Used: the hulls,
masts, sails and rigging of `SM_Veh_Boat_Rowing_01`, `SM_Veh_Boat_Small_01`,
`SM_Veh_Boat_Medium_01`, `SM_Veh_Veh_Boat_Large_01` / `SM_Veh_Boat_Large_*`
and `SM_Veh_Boat_Warship_01`; `SM_Flag_Pirate_01`, the British and Spanish
flags; `SM_Env_Background_Island_01/02/03`, `SM_Env_Rock_Huge_01/03`,
`SM_Env_PalmTree_01/02/03` and `SM_Bld_Fort_Tower_01`. Things thrown
overboard are boxes built in code. The log lists every file it loaded ("pack assets used").

## Make a game like this

1. **Copy the folder** (`cp -r games/sea_demo games/my_ships`), rename the
   target in CMakeLists.txt, the namespace `kke_sea` and add the folder to
   the root CMakeLists.txt.
2. **Add or change ships** as rows in `kClasses`: Synty names, size,
   handling and guns.
3. **Make the rules**: a convoy to protect, a treasure to carry home, a
   fort to take. `m_ships`, `damageShip` and `startSinking` are the hooks.
4. Pitfalls: physics goes in `fixedUpdate`, drawing interpolates with
   `ctx.alpha`; FEMFX objects must never overlap when spawned (they push
   apart hard enough to break); set `NetModule::playerCharacter` before
   hosting or joining, because it is sent once.

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The application and its modules |
| [SeaDemoModule.h](SeaDemoModule.h) | The module and all its state |
| [SeaDemoModule.cpp](SeaDemoModule.cpp) | Start, sailing, controls, camera, split screen, drawing, the panel |
| [Ships.h](Ships.h) / [Ships.cpp](Ships.cpp) | Ship classes and their Synty art |
| [Battle.cpp](Battle.cpp) | Aiming, broadsides, cannonballs, damage, masts, sinking, enemy captains |
| [Effects.cpp](Effects.cpp) | Splashes, wakes, smoke, fire, splinters |
| [World.cpp](World.cpp) | Islands, the fort, the FEMFX palisade |
| [SeaNet.cpp](SeaNet.cpp) | Online |
| [game.json](game.json) | Marketplace manifest |
