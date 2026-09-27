# Goblin Horde

Hold the old fort on the hill against waves of goblins. You are the king
with his sword; they come through the four gateways, a few at a time go at
you while the rest circle and jeer, and when a wave's nerve breaks the last
of them run for the trees. Each wave is bigger. Between waves you get your
breath (and some health) back.

It shows off crowd AI on the engine AI core ([docs/AI.md](../../docs/AI.md)),
melee on `kke::Combat` ([docs/COMBAT.md](../../docs/COMBAT.md)), Synty
SIDEKICK modular characters (`kke/Sidekick.h`), crowd LODs with
meshoptimizer (`kke/MeshLod.h`), Jolt ragdolls for the fallen, a Recast
navmesh around the ruins, and a procedural flinch on every hit.

## Controls

| | Controller | Mouse and keyboard |
|---|---|---|
| Move | left stick | WASD |
| Look | right stick | mouse (click the view to grab it, Esc lets go) |
| Slash: quick, catches two or three in front | X | left mouse / J |
| Great swing: slow, costly, everything around you flies | Y | right mouse / K |
| Block (just as the claw lands: parry) | RB / LT | Left Shift |
| Roll | A | Space |
| Try again | Start | R |

## How the goblins think

All in data, in [data/goblin.yml](data/goblin.yml) (JSON works too): the
`goblin` species' utility actions are **charge**, **wait_turn** and
**break**. The game sets three inputs every frame: `crowded` (enough
goblins are already on the king and this one isn't one of them),
`morale` (each kill knocks the wave's morale down, it comes back slowly,
the last few have none) and `health`. The AI core does the rest: sensing
the king, paths round the ruins, separation so they don't overlap,
flocking so a mob moves as one, and Attack events when one is in reach.

The waves are data too, in [data/waves.yml](data/waves.yml): how many
goblins, how many alive at once, how fast and how tough, how many go at
the king together (`attackers`) and the breather between waves.

## Crowd cost

- Each goblin look (five by default) is ~30 part files merged into one
  skinned model, then simplified once from ~19k to ~3.8k triangles
  (`simplifyModel`, ratio 0.2). The five load in parallel.
- Instances are pooled and reused; the dead ragdoll for five seconds (at
  most 12 at once, oldest first), lie still, then sink away.
- The AI and animation of 60 goblins take about 1.6 ms a frame (measured
  headless). Skinning is on the CPU, spread over the worker threads
  (`ModelModule`), until GPU skinning lands.

## Environment

| Variable | Effect |
|---|---|
| `KKE_HORDE_BOT=1` | the king fights by himself (attract mode, headless runs) |
| `KKE_HORDE_WAVE=<n>` | start at wave n |
| `KKE_HORDE_MAX=<n>` | goblins alive at once, at most (default 60) |
| `KKE_HORDE_VARIANTS=<n>` | goblin looks loaded (default 5, fewer loads faster) |
| `KKE_HORDE_LOD=<ratio>` | how much of each goblin's triangles to keep (default 0.2) |
| `KKE_HORDE_RAGDOLLS=<n>` | ragdolls at once (default 12, 0 = they just topple) |
| `KKE_HORDE_HERO=<asset>` | another POLYGON Fantasy character as the king |
| `KKE_HORDE_QUIT=<s>` | quit after that long, with a report every 10 s |

## Assets

Synty packs, never committed ([docs/SCENES.md](../../docs/SCENES.md)):
SIDEKICK Goblin Fighters, ANIMATION Goblin Locomotion, POLYGON Fantasy
Characters. Put them in `assets/synty/` or point `KKE_ASSETS_DIR` at them.
The king's clips are Quaternius' Universal Animation Library (CC0). The
fort, trees and rubble are boxes built in code. Without the goblin packs
the waves still come but the goblins can't be seen; without the fantasy
pack the king is the UAL mannequin in gold.
