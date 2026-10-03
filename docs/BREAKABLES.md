# Breakables: any prop bends and breaks

`kke::Breakables` (`kke/Breakables.h`) turns a prop you placed from a
model pack (a Synty crate, a barrel, a lamp post, a bottle) into a
physics object that bends and breaks the way its material would: wood
splinters, stone breaks into chunks, glass shatters from where it was
hit, ceramic breaks into shards, metal dents and bends and stays bent.
The prop keeps its own mesh, UVs and texture; what you see is the model
itself, cut and bent.

It needs FEMFX in the build (`KKE_ENABLE_FEMFX`, on in the `everything`
presets). Without it `make()` returns 0 and the prop stays as it was, so
the same game code runs on Android.

## How it works

1. The prop's triangles are voxelized into tetrahedra
   (`kke::voxelizeToTets`), within a budget (`maxCells`, 80 by default:
   480 tets, a good prop on min-spec hardware).
2. The material's fracture pattern cuts the tets into pieces
   (`kke/VoronoiFracture.h`, `bakeFracture`): splinters along the grain
   for wood, clusters of cells for stone, radial cracks from the hit for
   glass. Metal isn't cut; it bends (FEMFX plasticity).
3. The mesh is glued to the tets (`embedTrianglesInPieces`), so each
   piece carries its own part of the model; where a piece breaks off,
   the new faces are filled from the texture (`interiorFillFromTexture`).
4. FEMFX simulates it (`PhysicsModule`). Every frame `update()` redraws
   the props that moved from their tets (`deformEmbedded`); props asleep
   cost nothing. Jolt bodies (ragdolls, cars, balls) reach it through
   `PhysicsBridgeModule`.

It settles under its own weight for `armAfterSeconds` (2 s) before it
can break, so a prop never cracks just from being placed.

## Use it

```cpp
#include "kke/Breakables.h"

// init():
m_breakables = std::make_unique<kke::Breakables>(app);

// after placing a prop:
kke::BreakKind kind;
if (kke::guessBreakKind("SM_Prop_Crate_01", kind)) {   // -> Wood
    kke::Breakables::Options o;
    o.kind = kind;
    o.seed = instance * 2654435761u;  // same seed, same pieces
    o.maxSize = 3.0f;                 // trees and houses stay as they are
    std::string stats;
    m_breakables->make(instance, m_models->transform(instance), o, &stats);
}

// update(), every frame:
m_breakables->update();
for (auto lost : m_breakables->lost()) { /* physics dropped it: give it its collider back */ }

// shutdown(), while PhysicsModule is still there:
m_breakables->clear();
```

`restore(instance)` makes it a plain prop again (Build mode in the
sandbox), `remove(instance)` forgets it when the prop is deleted.

## Guessing the material from a name

`guessBreakKind(name, kind)` reads a file or mesh name word by word
(`SM_Prop_Crate_01`, `woodenCrate`): material words first (glass, wood,
metal, steel, stone, rock, ceramic, pottery...), then objects (crate,
barrel, chest, fence: wood; bottle, window: glass; car, barrier, sign,
lamp, pole: metal; rock, statue, wall piece: stone; vase, plate, pot:
ceramic). It returns false for what shouldn't break and for names that
say nothing: ground, floor, road, terrain, water, grass, plants,
buildings and roofs, interiors, cliffs, characters, effects and cloth.

`breakPreset(kind)` gives each kind's material, fracture pattern, piece
size and whether it bends; `breakKindFromId("wood")` reads the id a level
file stores.

## Options

| Option | Default | What |
|---|---|---|
| `kind` | Wood | what it's made of |
| `overridePattern`, `pattern` | off | another fracture pattern than the kind's own |
| `chunkScale` | 1 | x the kind's piece size |
| `toughness` | 1 | x the stress it takes to break |
| `maxCells` | 80 | voxel budget (6 tets each) |
| `seed` | 1 | the pieces (same seed, same cracks: networked games agree) |
| `texture` | | the instance's texture override |
| `armAfterSeconds` | 2 | settling time before it can break |
| `maxSize` | 0 (any) | refuse props whose longest side is more than this, metres |
| `anchorTall` | true | tall, thin props (taller than 2.5x their footprint) are held at their foot so they bend there |
| `allowToppling` | false | let a tall prop that breaks into pieces (a tree as splinters) stand free and topple |

## What it refuses, and why

It leaves a prop as it was (and logs why) rather than make something
that looks wrong:

- a skinned or empty model;
- bigger than `maxSize`;
- a tall prop that would break into pieces: pieces can't be pinned, so
  it would fall over the moment it's armed (unless `allowToppling`);
- one model that is several separate things (a scatter of small rocks):
  the tets would join them into one solid and stretch triangles between
  them;
- a mesh the tets can't hold: a triangle more than 6 cells outside the
  voxel shell (thin decorations far from the body) would stretch into
  streaks when it moves.

A prop sunk into the ground is lifted to stand on it first.

Tall solid metal (a lamp post, a flag pole) is anchored at its bottom
layer of tets and made 10 times stiffer, with a 20 times higher yield,
so it stands up straight and only bends when something hits it hard.

## Where it's used

- `games/synty_demo`: crates, barrels, chest (wood), barrier and flag
  pole (metal). "Drop onto a crate" (C, pad B) drops a ragdoll onto them.
- `games/sandbox`: X makes the selected prop breakable; in Play mode,
  pack props break by themselves ("Props break in Play").

Cost: making one takes 10-45 ms (voxelizing and gluing), so make a few
a frame rather than a whole level at once (the sandbox does 3 a frame).
FEMFX then costs ~0.1 ms a step while everything sleeps.
