# Hair

Heads of hair that swing when the head turns, blow in the wind and fall
over the shoulders, and never go into the head. A few hundred **guide
strands** per head are simulated; thousands of **drawn hairs** follow them,
built on the GPU. The API is in [`kke/Hair.h`](../engine/include/kke/Hair.h),
`RigidWorld::addHair` and [`kke/HairRenderer.h`](../engine/include/kke/HairRenderer.h).
The demo is the Hair scene of [games/cloth_demo](../games/cloth_demo/README.md):
a hairdresser's catalog of every hair type, 1A to 4C, and hairstyles
from an afro and bantu knots to box braids, cornrows and locs.

## Which solver, and why

Guide strands are **Jolt Physics soft bodies**, the same XPBD solver that
runs the cloth ([CLOTH.md](CLOTH.md)) and the MIT library that runs the
rigid bodies. Each strand is a chain of vertices held by distance
constraints: stretch along it, bend across two segments, and across three
in a curl (which holds a spiral's turn). The engine adds the parts Jolt
doesn't have: the rest pose (a hairstyle), wind, following the head, and
the drawn hairs.

What was tried first and not used:

- **Jolt's own GPU hair** (`Jolt/Physics/Hair`). Its author marks it "in
  development": it has no wind and collides only with convex hulls.
- **Jolt's Cosserat rods** (strands that know bend *and* twist). Nothing
  turns the first rod's twist with the head (Jolt has no way to set a rod's
  orientation), so a quick head turn flipped a strand's rest curve from
  down to up: hair stood on end. They also cost 2.5 times as much.
- **FEMFX.** It solves volumes made of tetrahedra, not strands.

## Making hair

```cpp
#include "kke/RigidWorld.h"
#include "kke/HairRenderer.h"

// The head: a sphere only cloth and hair feel, moved with the character.
kke::RigidWorld::BodyDesc head;
head.shape = kke::RigidWorld::Shape::Sphere;
head.motion = kke::RigidWorld::Motion::Kinematic;
head.clothOnly = true;
head.radius = 0.1f;
head.position = headCentre;
auto headBody = world.add(head);

kke::HairDesc hair;
hair.style = kke::hairStyle("wavy");
hair.style.rootColor = {0.45f, 0.3f, 0.14f};
hair.style.tipColor = {0.78f, 0.6f, 0.36f};
hair.bindPose = headMatrix;                         // the head's world matrix now
kke::hairScalp(hair, headCentre, 0.1f, 160);        // 160 guide strands, off the face
kke::RigidWorld::HairId id = world.addHair(hair);

kke::HairRenderer drawn(app);                        // once
drawn.build(hair);

// every fixed step, before world.step(dt):
world.moveKinematic(headBody, headCentreNow, headRotationNow, dt);
world.setHairJoint(id, headMatrixNow);
// every frame, after it:
world.hairPositions(id, guides);
drawn.update(guides, headMatrixNow);
// compute(cmd): drawn.compute(cmd);   (builds the hairs, before any render pass)
// render(): drawn.draw(ctx);  renderShadow(): drawn.drawShadow(ctx, towardsTheSun);
```

- `hairScalp(desc, centre, radius, count, crown, up, front, hairline)`
  spreads `count` roots evenly over the part of a sphere where hair grows
  (within `crown` radians of `up`, not over the face) and combs the rest
  pose back, off the face. Your own roots work too: fill `roots` and
  `directions` (and `headCenter`/`headRadius` so the rest pose lies over
  the head instead of through it).
- The root of each strand and the vertex just above it follow the head
  exactly, so hair leaves the scalp the way it grows. `hold` lets a style
  keep its shape: 0 hangs free, 0.85 is gel.
- `setWind` on the world blows on hair as on cloth; `HairDesc::wind`
  scales it (0 indoors).
- Collide it with the neck and shoulders the same way: `clothOnly`
  bodies, or ordinary static and moving bodies (hair feels both).
- Paint the scalp the hair's root colour where hair grows. At any density
  there are gaps between drawn hairs, and skin showing through them reads
  as thin hair. The demo does it with vertex colours.

## Every hair type, 1A to 4C

`hairStyle("1a")` to `hairStyle("4c")` give each type of Andre Walker's
chart, the one hairdressers use. The number is the pattern, the letter
how loose (A) or tight (C) it is.

| Type | What it looks like | How it is made |
| --- | --- | --- |
| 1A, 1B, 1C | straight: fine and silky, medium, coarse with a slight bend | straight guides; 1C waves a little in one plane |
| 2A, 2B, 2C | S-shaped waves, looser to defined from the roots, more frizz in 2C | waves in the guides' rest shape, 3 to 5 a metre |
| 3A, 3B, 3C | ringlets as wide as sidewalk chalk (A), a marker (B), a pencil (C) | coils drawn round each hair, 11 to 4.5 mm across; a clump's hairs coil together |
| 4A | tight S-shaped coils, as fine as a crochet needle | 3.5 mm coils, 110 turns a metre, 60% shrinkage |
| 4B | Z-shaped: sharp bends instead of round coils | zig-zags whose plane turns every bend, 70% shrinkage |
| 4C | the tightest coils, less defined, a soft halo | 2.5 mm coils, each hair on its own, 75% shrinkage |

Two things make types 3 and 4 what they are, and the solver has both:

- **Coils.** A 4C coil is a few millimetres across; guides a centimetre
  apart can't hold it. So the coils are drawn round every drawn hair, in
  the vertex shader: `coil` turns a metre, `coilRadius`, `zigzag` (0
  round .. 1 Z-shaped). `definition` says whether a clump's hairs coil
  together (1: ringlets, 3A) or each on its own (0: 4C's halo).
- **Shrinkage and spring.** Coiled hair rests far shorter than it is:
  `shrinkage` 0.75 means it rests at a quarter of its length pulled
  straight. The guides rest at the short length and can be pulled out to
  1 / (1 - shrinkage) of it (their tethers allow it), then spring back.
  As a guide stretches, the coils drawn round it unwind: the hair's own
  length stays the same, so they get longer and narrower, and straight
  when fully pulled out. `tests/test_hair.cpp` drops a head fast: 3B
  ringlets stretch 32% and are back within 2% a few seconds later, while
  straight hair stays within 14%.

## Hairstyles

`hairstyleOnHead(desc, name, centre, radius, guides)` puts a whole
hairstyle on a head: its type, where it grows, its cut and ties
(`hairstyleNames()`):

| Hairstyle | How it is made |
| --- | --- |
| `afro` | 4C grown out, every strand standing out from the scalp (up and back, off the face), longer on top so the shape sits high |
| `puff` | everything gathered along the scalp to a tie at the crown, then bursting out every way into a ball |
| `high-top fade` | hair only on top, standing straight up and cut flat (`lengths`); paint the fade on the scalp, from dark at the cut down to the skin |
| `twist-out` | two-strand twists taken out: big defined coils, clumped |
| `bantu knots` | parted into seven sections, each twisted and wound into a knot |
| `box braids` | each part of the scalp a three-strand braid, long and heavy (with extensions), falling back from the face |
| `cornrows` | three-strand braids flat on the scalp in rows from the hairline over the top to the nape, the ends hanging (6 to 14 rows: `guides / 16`) |
| `locs` | each part one rope of matted hair, fuzzy and matte, slowly twisting |
| `two-strand twists` | each part two strands twisted round each other |

Any type name works too (`hairstyleOnHead(desc, "3a", ...)`), and it
keeps `desc.style`'s colours. The parts to make your own:

- `HairDesc::lengths`: a length per root, as a fraction of
  `style.length` (a flat top, layers, a fade).
- `HairDesc::ties`: every root within `reach` of a tie lies along the
  scalp to it, then a `Puff` bursts out from it and a `Knot` winds into a
  knot `size` across. Give each root to its nearest tie (a large `reach`)
  and the parts fall between the ties by themselves.
- `HairDesc::tiedTo`: which tie each root goes to, instead of the
  nearest. Cornrows are a root at the hairline tied to a `Hang` tie at
  the nape: the braid lies along the scalp to it, then hangs.
- Your own `directions`: the high-top's point straight up.

### Braids, twists and locs

`HairStyle::plait` makes every guide one braid: its drawn hairs are laid
in `plait` strands that cross over each other round the guide all the
way to the tip (3: a braid, 2: a twist, 1: a loc's rope). `plaitRadius`
is half the braid's width and `plaitTurns` how often the pattern repeats
per metre (a three-strand braid's six crossings). The three strands run
on one figure of eight, a third of the way round it apart, so each
crosses over the middle and then under, as in a real braid; each strand
is a rope of hairs that turns with it. Only the guides are simulated, so a
braid swings as one piece, and its drawn hairs can't come out of it.
Braids don't collide with each other (as loose hair doesn't); give them
`thickness` as wide as the braid so they stay off the head by their
width.

## Other presets

`hairStyle(name)` gives a preset. Copy it and change what you like.

| Preset | Length | Shape and feel |
| --- | --- | --- |
| `straight` | 30 cm | the default |
| `long` | 55 cm | falls over the shoulders, swings |
| `wavy` | 40 cm | waves in one plane, soft |
| `curly` | 30 cm | spirals (9 turns a metre), springy, fluffy tips |
| `short` | 7 cm | stays close to the head |
| `fur` | 3.5 cm | a pelt |
| `gel` | 10 cm | spikes that keep their shape |

How the numbers work. `density` is the mass per metre of the clump one
guide stands for (a real hair is about 0.005 g/m, and a guide stands for
about a thousand of them). `bend` and `stretch` are softness, as for
fabrics: 0 is as stiff as the solver can make it, and higher is floppier.
`stiffRoot` makes roots stiffer than tips. `width` is how wide the clump is
to the air: its hairs shelter each other, so it is much less than a
thousand hair widths. `thickness` is the collision radius of every guide
vertex. `droop` is how soon a strand turns from the scalp towards the
ground in the rest pose (long for gel).

Coils: `coil`, `coilRadius`, `zigzag`, `definition` and `shrinkage`, as
above.

The look: `rootColor` and `tipColor` (sRGB), `hairsPerGuide`, `spread` (how
far they spread around their guide, in guide spacings), `clump` (tips
gather to their guide), `frizz`, `hairWidth`, `shine` and `shift` (the
highlight's shift along the hair).

## How it is drawn

`HairRenderer` uploads only the guide points each frame (for 160 guides,
about 40 KB). Once a frame, a compute pass (`compute(cmd)`,
`shaders/hair_points.comp`, `hair_common.glsl`) builds every drawn hair's
points from its guide, a neighbouring guide it leans
towards (so the gaps between clumps fill as guides move apart) and its own
offset, which turns with the head. Each hair follows a smooth curve through
its guide's points (Catmull-Rom), so curls stay round. It writes each
point once (16 bytes: the position and a packed direction); the vertex
shaders, for the picture and for the shadow, only read them, so a point
is no longer built again for every vertex and every pass. Each is a ribbon
facing the camera, never thinner than most of a pixel, so it can't flicker
in and out between pixels. There is no dithering, no alpha and no temporal
trick; MSAA smooths the edges.

`hair.frag` shades each ribbon as a round fibre: the normal turns across
it. It uses Kajiya-Kay diffuse and the two shifted highlights of
Marschner's model, as Scheuermann made them practical. R is the white glint
off the cuticle, shifted towards the tip. TRT is the coloured one that went
through the hair and back, shifted towards the root. Roots are darker,
because light reaches them through the rest of the hair. Hair casts
shadows (`drawShadow`, the same ribbons facing the sun, a little wider).

Coils are drawn round that curve. The CPU sends, with the guide points, a
direction across each guide carried along it from the root (so a coil
never flips as the head turns) and how stretched each guide is there (so
a coil unwinds as it is pulled). A drawn hair gets as many points as its
coils need, eight a turn, up to 97. Plaits use the same frame: a hair's
strand and its place in it, twelve points a turn, up to 161.

`HairStrands::ribbons()` builds the same hairs on the CPU, for tests,
exporting or a renderer of your own. It costs about 3 ms per 5,000 hairs,
which is why the GPU builds them in the demo.

## No clipping

- Every guide vertex is a ball of `thickness` that collides with the
  world: hair lies on the head and the
  shoulders instead of in them.
- Tethers from the root: a strand can't be stretched more than
  `maxStretch` (5%) past its length along itself, so it can't be pulled
  through the head.
- Drawn hairs sit around their guide, at most a guide spacing away, and
  are kept out of the head's sphere (`headCenter`, `headRadius`): a hair
  blending between two guides that fall round the head on either side
  would otherwise cut through it.
- Checked in `tests/test_hair.cpp`: 160 long strands on a head turning and
  nodding in 3 m/s wind for 5 s. No guide vertex ever went into the head,
  and no strand stretched more than 12%. Every type and hairstyle on a
  turning head: no guide vertex and no drawn hair in the head.

**Known limits.** Strands don't collide with each other or with cloth.
That is the usual trade in games: with a few hundred guides, strand
against strand would cost more than everything else together. Only the
guide vertices collide, so a drawn hair can cut a sharp corner (a box's
edge) between two of them. Round shapes (heads, capsules) don't show it.

## What it costs

`kke_bench --filter hair_` (one thread, 60 steps a second, heads turning
and nodding in gusting wind, never asleep; this sandbox's 4-core VM):

| Case | Guide vertices | ms per step |
| --- | --- | --- |
| One head, 100 straight strands (30 cm) | 1,400 | 0.12 |
| One head, 400 long strands (55 cm) | 7,200 | 0.71 |
| One head, 400 curly strands | 10,400 | 1.42 |
| Eight heads of 200 long strands | 28,800 | 3.2 |
| One head, 200 strands of 3B ringlets | 3,200 | 0.41 |
| One head, 200 strands of 4C coils | 2,000 | 0.22 |
| One head, 200 strands in bantu knots | 4,000 | 0.54 |
| One head, 160 box braids (50 cm) | 3,200 | 0.33 |
| One head, cornrows (10 rows) | 260 | 0.03 |

(The last five rows were measured in later runs on the same VM, which
then took 0.14 to 0.155 ms for the first row.)

With the job system, each head's strands are split into soft bodies of 64
guides, and Jolt steps them on separate threads. In the demo (4 threads),
four heads with 640 guides in total step in about 1.2 ms, hair and all.
Sending the guides to the GPU takes 0.1 ms.

The drawn hairs cost only GPU time: hairs x (2 x segments) x 6 vertices
(the demo's 20,480 hairs: about 4 million vertex shader runs a frame).
Coiled hair needs more points per hair: a 4C head of 160 guides x 40
hairs x 96 segments is about 3.7 million.
That is not measured yet, because this sandbox renders on the CPU. Run the
demo with `KKE_BENCHMARK` on the machine you care about to see its frame
time.

Far away, fewer hairs are drawn. A head small on screen has its hairs a
pixel wide, piled dozens deep, and drawing all of them costs vertex work
nobody can see. Once they would pile more than 24 deep, the renderer draws
only every second (third, ...) hair, each wider by the square root of that
so the hair looks as full; every guide keeps its share. Close up (the
demo's camera) all of them are drawn. `drawnHairs()` says how many the
last frame drew. Pipelines are shared by every `HairRenderer`, so a new
head costs no shader compiling.

### Slower devices: natural to solid, and detail

Two live settings trade looks for speed, per head:

- `world.setHairMotion(id, motion)`: 1 is natural (the style as made).
  Less holds each guide closer to its styled shape and gives the solver
  fewer iterations (down to a third). 0 is solid: the hair is taken out
  of the simulation and turns with the head exactly as styled, so it costs
  nothing on the CPU. A game can lower it for crowds, far away heads, or
  a phone; the cloth demo's panel has it as "Hair physics".
- `drawn.setDetail(share)`: at most this share of the hairs is drawn
  (0.05 to 1), each wider by the square root so the hair looks as full.
  Distance lowers it further by itself (below). The demo's "Hair detail".

The test `Hair.MotionGoesFromNaturalToSolid` checks that solid hair does
not move from its style, half moves less than natural, and that solid
steps at under a third of natural's cost.

Rules of thumb:

- 100 to 200 guides are enough for a head in a game, and 400 for a hero
  close-up. The drawn hair count (`hairsPerGuide`) is what makes it look
  full, and it costs nothing on the CPU.
- Curls in the guides (`curl`) need more segments (24) than straight
  hair (12 to 16), and cost more. Coils (types 3 and 4) are drawn, so
  their guides are short and cheap; they cost GPU time instead.
