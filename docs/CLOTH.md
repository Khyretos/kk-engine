# Cloth

Capes, flags, curtains, blankets, sheets, tablecloths and nets. Cloth lives
in a `kke::RigidWorld` next to the rigid bodies and characters, so it drapes
over boxes, catches balls and gets pushed aside by people walking through it.
The API is in [`kke/Cloth.h`](../engine/include/kke/Cloth.h) and
`RigidWorld::addCloth`. The demo is [games/cloth_demo](../games/cloth_demo/README.md).

## Which solver, and why

Cloth is simulated by **Jolt Physics' soft bodies**, the same MIT library
that already runs the rigid bodies. Its cloth solver is position-based
(XPBD) with stretch, shear and bend constraints, long-range tethers,
skinned back-stops and vertex-against-shape collision with CCD.

**FEMFX was looked at and not used for cloth.** FEMFX solves volumes made
of tetrahedra (jelly, rubber, breakable hero objects). A sheet of cloth has
no volume. Built from tetrahedra it would need paper-thin elements, which
are slow and unstable. FEMFX stays the engine's tool for deformable solids
([PHYSICS_BRIDGE.md](PHYSICS_BRIDGE.md)).

What Jolt doesn't have, the engine adds: collisions of cloth with itself
and with other cloth (the part that stops clipping), air drag and wind,
friction between fabrics, and fabric presets that look and move like the
real thing.

## Making cloth

```cpp
#include "kke/RigidWorld.h"

kke::ClothDesc curtain;
curtain.mesh = kke::clothGrid(glm::vec3(0, 2.4f, 0), 1.6f, 2.2f, 24, 32);   // hangs down
curtain.fabric = kke::clothFabric("linen");
for (int c = 0; c < curtain.mesh.columns; ++c)
    curtain.pinned.push_back(kke::clothGridIndex(curtain.mesh, c, 0));      // on the rod
kke::RigidWorld::ClothId id = world.addCloth(curtain);

// every frame, after world.step(dt):
world.clothPositions(id, positions);          // one per mesh vertex, world space
```

- `clothGrid(center, width, height, columns, rows, right, down)` makes a
  rectangle. `down = (0,-1,0)` hangs it like a curtain, `down = (0,0,1)`
  lays it flat like a sheet. UVs are in metres.
- `clothNet(...)` is the same grid as threads only (a hammock, a tennis
  net). `ClothDesc::contactMass` makes a net feel as heavy as its frame
  holds it (a tennis net: about 5 kg), so a ball stops instead of
  punching through.
- `pinned` vertices don't move. With `setClothJoints(id, {matrix})` they
  follow a moving joint (a flag on a moving pole).
- A cape or skirt: give `skin` (up to four joints and weights per vertex)
  and `bindPose`, then call `setClothJoints` with the joints' world
  matrices every frame. `maxDistance` says how far a vertex may swing from
  its skinned place; back-stops keep it from going behind the body.
- `setWind(velocity)` blows on every cloth. `ClothDesc::wind` scales it
  per cloth (0 for cloth indoors).
- `RigidWorld::BodyDesc::clothOnly` makes a body only cloth sees: the arms
  and legs of a mannequin, a cape's body proxy.
- Characters (`addCharacter`) carry a collider only cloth sees, so walking
  into a curtain pushes it aside. Rays and character queries ignore cloth.

Drawing: `DynamicMeshRenderer::drawCloth(ctx, fabric)` (in
`kke/SphereImpostors.h`) draws the mesh two-sided with the fabric's look
(`shaders/cloth.frag`). The cloth demo shows how it rebuilds the mesh each
frame from `clothPositions` and `clothNormals`.

## Fabrics

`clothFabric(name)` gives a tuned preset. Copy it and change what you like.

| Preset | Feels like | Looks like |
| --- | --- | --- |
| `silk` | very light (60 g/m²), floats down slowly, drapes in soft folds | satin weave, gloss along the threads |
| `satin` | light, slippery, flowing | satin weave, strong thread gloss |
| `cotton` | the default: 150 g/m², ordinary folds | plain weave, matte |
| `linen` | a little stiffer than cotton, crisp folds | plain weave |
| `denim` | heavy (450 g/m²), stiff, few big folds | twill (the diagonal ribs, white weft) |
| `wool` | heavy, soft, thick (16 mm), grips | knitted loops, fuzzy sheen at the edges |
| `fleece` | thick and soft | fuzzy, strong sheen |
| `leather` | heavy and stiff, barely stretches | smooth, no weave |
| `canvas` | stiff, heavy | coarse plain weave |
| `net` | threads, light, springy | drawn as threads |
| `rubber` | stretchy, bouncy | smooth, no weave |

How the numbers work: `density` is real (kg/m²). `stretch`, `shear` and
`bend` are **softness**: 0 is as stiff as the solver can make it, 1 means
each solver sub-step fixes about two thirds of the error, 100 about 2%
(very floppy). Softness is scaled by each vertex's mass and the sub-step,
so a fabric feels the same at any mesh resolution. `airDrag` scales air
resistance (it is what makes silk float and a flag fly), `thickness` is
the collision radius of every vertex, `maxStretch` caps how far the
tethers let it stretch. The look fields (`color`, `roughness`, `sheen`,
`weave`, `weaveScale`, `fuzz`, `specular`) only change how it's drawn.

The weave fades to its average colour where a thread is smaller than a
pixel, so fabric never shimmers in the distance (no dithering, no
temporal tricks).

## No clipping: protection levels

Clipping (cloth through a body, through other cloth or through itself) is
off by default. `ClothDesc::protection` (or `setClothProtection` at run
time) picks how hard the engine works at it:

| Level | What it does | When to pick it |
| --- | --- | --- |
| **Full** (default) | Everything below, plus cloth against cloth and against itself: every vertex kept `thickness` from every triangle, every edge from every edge, with friction. Anything that went through between two steps is put back on the side it came from (continuous: fast folds don't slip through), the pass repeats until nothing is crossed, and it runs once more after every step so what's drawn is clean. While it is undoing crossings, it also runs between the solver's sub-steps (below). | Anything the camera looks at: capes, blankets, a bed, curtains people walk through. |
| **Basic** | Vertices are balls of `thickness` (cloth rests *on* things), tethers stop it being stretched through a collider, back-stops keep capes off the body. No cloth-against-cloth. | Lots of background cloth that never folds onto itself or other cloth: distant flags, awnings. |
| **Off** | Raw Jolt, no extras. | You're measuring, or the cloth is tiny and far away. |

What Full has been checked against (`tests/test_cloth.cpp` and runs of the
demo scenes, counting exactly how many edges pass through triangles):

- A sheet dropped on a sheet, and a denim throw dropped from a metre onto a
  silk sheet on a wool blanket: **zero crossings at every moment**.
- The six fabrics dropped over a ball and flown as banners in gusty wind,
  in 12 one-minute runs with different starting points and winds: never
  unstable, never crumpled up.

### Layers pressed together: sub-steps

The solver (Jolt) and the pass (the engine) take turns: Jolt keeps cloth
out of solids, the pass keeps cloth out of cloth. Where a solid presses
layers together (three sheets draped over a ball, a bed), Jolt pushes the
lower sheet up through the upper one every step, and the pass puts it
back. Done once after the whole step, that fix is big: the fabric around
it is stretched, the solver springs back from it, and the layers work
their way through each other. Letting such vertices go as "tangled"
(what the pass used to do) left the sheets through each other for good.

So while the pass is undoing crossings, each physics step is cut in
`RigidWorld::Settings::clothSubsteps` (6 by default) and the pass runs
between the cuts (each cloth's solver iterations are shared out over
them). Every fix is then small, and the layers are kept apart as they are
pressed together. Once nothing has been undone for half a second, steps
go back to one piece. No fix moves a vertex more than a few thicknesses
in one pass, so pressed layers can't build up energy and fly apart.

Edges through other cloth, three sheets (silk, cotton, denim, 32 x 32)
dropped together over a ball, counted every third step for 6 s
(`tests/test_cloth.cpp` has a smaller version):

| clothSubsteps | Three sheets over a ball | 64 x 64 blanket piling on the floor |
| --- | --- | --- |
| 1 (the pass after each step only) | 367,000 | 2,300 |
| 2 | 7,500 | 1,400 |
| 6 (default) | 272 | 118 |

The cost is the pass running up to six times a step, only while it is
undoing crossings (the table below). Set `clothSubsteps = 1` to go back
to one pass a step, with the old "let go" behaviour. The rigid bodies are
stepped in the same sub-steps meanwhile (more exact, and more costly with
many bodies).

**Known limit.** The demo's bed (silk sliding over a wool blanket at the
mattress edge while a denim throw lands on it) used to leave a few hundred
of the silk sheet's edges through the throw for about two seconds after a
hard landing. It has not been measured again with sub-steps.

## What it costs

`kke_bench --filter cloth_` (one thread, the solver's 6 sub-steps, 60 steps a
second; this sandbox's 4-core VM, so a desktop is faster; medians over 4 s):

| Case | Off | Basic | Full, clothSubsteps 1 | Full (default: 6) |
| --- | --- | --- | --- | --- |
| One 32 x 32 sheet over a ball (1,024 vertices) | 0.41 ms | 0.43 ms | 0.82 ms | 0.92 ms |
| One 64 x 64 blanket over a ball, piling on the floor (4,096 vertices) | | 1.4 ms | 10 ms (p95 19 ms) | 35 ms (p95 68 ms) |
| 16 sheets of 24 x 24 (9,216 vertices) | | 3.4 ms | 6.6 ms | 6.9 ms |
| A 32 x 32 wool cape on swinging shoulders in gusty wind | | 0.38 ms | 6.8 ms | 8.5 ms |

With the job system (the demo, 4 threads), the pass's search for pairs runs
on every thread: the Fabrics scene (12 cloths, 7,944 vertices) steps in
about 7 ms, the stress scene (16 sheets) in about 6 ms. The bed (3 layers,
3,624 vertices, all touching) is always undoing something, so it runs in
sub-steps all the time: about 2.9 times its cost with `clothSubsteps = 1`
(1.6 times with 2).

Nearly all of Full's cost is finding which parts are near which (the
fixing itself is under 5%). What keeps that down:

- **Flat patches are skipped.** Each cloth is cut into patches of 32
  triangles. A patch, or two or three neighbouring ones together, whose
  normals all stay within 86 degrees of one direction for the whole step
  can't pass through itself (Volino and Magnenat-Thalmann 1994, Provot
  1997), so nothing in it is searched. A falling sheet or a gently curved
  cape costs almost nothing. Only small pieces are skipped this way: a
  scarf lying in a flat loop, end over start, has all its normals up and
  can still go through itself.
- **Only relative motion counts.** A vertex is compared with a triangle
  (and an edge with an edge) by how far it moved relative to it, so a
  cape swinging in one piece isn't treated as if every part could reach
  every other.
- **Grid cells as big as what's in them**, so cloth moving fast gets
  bigger cells. Nothing is left out of the search for moving fast.

What still costs: cloth that is really close to itself, like a blanket in
a heap or a cape flapping into folds. A light cape in strong wind costs up
to 20 times Basic. Moving the search to the GPU is the next step for that.

Rules of thumb:

- Full costs 2 to 4 times Basic for cloth lying or hanging, and up to 20
  times for cloth folding onto itself all the time (a flapping cape, a
  heap). A cloth lying alone costs little more than Basic.
- Heaps and layers pressed together cost up to 3 times more again while
  they run in sub-steps. `clothSubsteps` trades that against clipping.
- Cloth that stops moving falls asleep and costs almost nothing (it still
  stops other cloth as an obstacle).
- Vertex count matters most. A cape is fine at 16 x 20; a blanket at 32 x
  32; go higher only for a hero close-up.
- `lastClothMs()` is the engine's pass (air, protection); `lastStepMs()`
  the whole physics step. `clothStats(id)` gives contacts and undone
  crossings per step.

## The demo

`./build/bin/cloth_demo`, scenes 1 to 6 (or `KKE_CLOTH_SCENE`): fabrics,
bed, nets, cape, stress, hair. `P` (X on a controller) cycles Full, Basic
and Off so you can see and time the difference. See [games/cloth_demo/README.md](../games/cloth_demo/README.md).

## Hair

Hair is strands, not sheets, with its own page: [HAIR.md](HAIR.md).
