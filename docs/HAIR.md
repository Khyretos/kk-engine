# Hair

Heads of hair that swing when the head turns, blow in the wind and fall
over the shoulders, and never go into the head. A few hundred **guide
strands** per head are simulated; thousands of **drawn hairs** follow them,
built on the GPU. The API is in [`kke/Hair.h`](../engine/include/kke/Hair.h),
`RigidWorld::addHair` and [`kke/HairRenderer.h`](../engine/include/kke/HairRenderer.h).
The demo is the Hair scene of [games/cloth_demo](../games/cloth_demo/README.md).

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

## Styles

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

The look: `rootColor` and `tipColor` (sRGB), `hairsPerGuide`, `spread` (how
far they spread around their guide, in guide spacings), `clump` (tips
gather to their guide), `frizz`, `hairWidth`, `shine` and `shift` (the
highlight's shift along the hair).

## How it is drawn

`HairRenderer` uploads only the guide points each frame (for 160 guides,
about 40 KB). The vertex shader (`shaders/hair.vert`, `hair_common.glsl`)
builds every drawn hair from its guide, a neighbouring guide it leans
towards (so the gaps between clumps fill as guides move apart) and its own
offset, which turns with the head. Each hair follows a smooth curve through
its guide's points (Catmull-Rom), so curls stay round. Each is a ribbon
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
- Drawn hairs sit around their guide, at most a guide spacing away, so
  they stay out of the head too.
- Checked in `tests/test_hair.cpp`: 160 long strands on a head turning and
  nodding in 3 m/s wind for 5 s. No guide vertex ever went into the head,
  and no strand stretched more than 12%.

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

With the job system, each head's strands are split into soft bodies of 64
guides, and Jolt steps them on separate threads. In the demo (4 threads),
four heads with 640 guides in total step in about 1.2 ms, hair and all.
Sending the guides to the GPU takes 0.1 ms.

The drawn hairs cost only GPU time: hairs x (2 x segments) x 6 vertices
(the demo's 20,480 hairs: about 4 million vertex shader runs a frame).
That is not measured yet, because this sandbox renders on the CPU. Run the
demo with `KKE_BENCHMARK` on the machine you care about to see its frame
time.

Rules of thumb:

- 100 to 200 guides are enough for a head in a game, and 400 for a hero
  close-up. The drawn hair count (`hairsPerGuide`) is what makes it look
  full, and it costs nothing on the CPU.
- Curls need more segments (24) than straight hair (12 to 16), and cost
  more.
