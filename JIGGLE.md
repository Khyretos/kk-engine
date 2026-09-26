# JIGGLE.md — jiggle physics

Secondary motion as core engine functionality: soft parts of a character
(breasts, glutes, belly, thighs, hair, tails) lag, overshoot and settle
when the body moves, and whole soft objects (jelly, slime) dent and wobble.
Everything is in `engine/include/kke/JigglePhysics.h`, pure CPU, no GPU
types, tested in `tests/test_jiggle.cpp`. The demo is `games/jiggle_demo`.

## The pieces

| Piece | What it does | Cost |
|---|---|---|
| `kke::JiggleRig` | Bone chains become verlet points that chase the animated pose; the bones are turned (and, with `stretch`, lengthened, squashed) to follow them. Runs after the Animator, on its `Pose`. | 8 points for a full body: ~20-40 µs per character per frame, including the pose rebuild |
| `kke::addJiggleBone` | Adds a soft-tissue bone to a rig that has none (most game rigs, Synty's included) and moves the nearby skin onto it. Nothing changes at rest. | load time |
| `kke::inflateSkin` | Reshapes the body: pushes the skin near a point outward (volume in metres). | load time |
| `kke::addHumanoidSoftTissue` | All of the above for any humanoid in one call: finds chest, pelvis and thighs by bone name and the skin around them, reshapes, adds breast and glute bones, returns rig chains and skin zones. | load time |
| `kke::JiggleSkin` | Jiggle on the mesh without bones: a few verlet points ride on bones and displace the skin near them (`ModelModule::setSkinJiggle`, applied after skinning). Belly, thighs. | a few µs |
| `kke::JellyBody` | A whole soft body: a particle lattice kept in shape by shape matching over every 2x2x2 cell, a rounded render surface embedded in it, two-way collision with balls. | 320 particles, 3 iterations at 120 Hz: ~0.8 ms per frame |

## Use

```cpp
kke::ModelData body = kke::loadModel(path, options);
kke::HumanoidSoftTissue tissue;
tissue.bust = 0.06f;   // metres of extra volume, 0 = the artist's shape
tissue.glutes = 0.05f;
kke::HumanoidJiggleSetup setup = kke::addHumanoidSoftTissue(body, tissue);
// ... retarget clips onto `body` now (the new bones stay at rest in them)
auto id = models.add(std::move(body), "my-character");

kke::JiggleRig rig(rigData, setup.chains);
kke::JiggleSkin skin(rigData, setup.zones, setup.zonePositions);
// every frame, after the Animator:
kke::Pose pose = animator.pose();
rig.apply(rigData, pose, instanceTransform, dt);
skin.apply(rigData, pose, instanceTransform, dt);
models.setSkinJiggle(instance, skin.offsets());
kke::poseToLocals(pose, *models.boneLocals(instance));
```

Hair, tails, antennae: `JiggleRig::Chain{ firstHairBone }` on the rig's
own bones; the last bone gets a virtual tip continuing its parent's
direction. `JiggleCollider` spheres and capsules keep points out of the
body.

## Settings (`kke::JiggleSettings`, all 0..1 unless noted)

- `stiffness`: pull back to the animated pose each step.
- `soften`: how much weaker that pull is near rest (soft wobble for small
  motions, still held for big ones).
- `stretch`: 0 keeps bone length, 1 is a free spring (squash and stretch;
  a one-bone chain from `addJiggleBone` scales along its axis, keeping volume).
- `drag`: damping relative to the parent (the tissue's own wobble).
- `airDrag`: damping in the world (what makes a ponytail trail).
- `gravity`: multiple of world gravity (sag). `blend`: 0 = animation only.
- `angleLimit` (degrees), `maxStretch` (ratio), `radius` (metres, for colliders).

## How it's built (and what the "jiggle physics" trope gets wrong)

Techniques from naelstrof's JigglePhysics for Unity, rewritten for this
engine rather than copied, plus fixes for what makes jiggle look bad:

- **Driven by the difference from animation.** Points chase where the
  pose puts them, so the authored pose always wins and nothing drifts. A
  character standing still has no energy to bounce with: no bouncing on
  its own at idle.
- **Fixed internal step (90 Hz) with interpolation of the offset from the
  pose**, not of the positions. Frame-rate independent, and no lag or
  jitter against the body at any frame rate (interpolating positions would
  trail the body by one step).
- **Two drags.** Relative drag damps the tissue's own wobble, air drag
  damps world motion. Separate, so riding in a car or an elevator doesn't
  set everything wobbling forever.
- **Soften near rest**, angle limits, hard length clamps, colliders: it
  stays anatomically sane whatever the sliders say.
- **Every step is a blend or a clamp, never a force.** It can't explode.
- **Sleep.** A rig whose points are still and whose pose isn't moving
  skips its solve. **Teleports** (more than `teleportDistance` in one
  frame) reset instead of whipping across the map.
- **Hitches** run at most 4 steps and drop the rest.
- **Flat-shaded art.** `inflateSkin` pushes by position, never by vertex
  normal: low-poly meshes split every corner per face, and a normal-based
  push would tear them open.

The jelly uses lattice shape matching (Müller et al. 2005, "Meshless
deformations based on shape matching"; per-cell regions as in Rivers &
James 2007, "FastLSM"). Each cell's rotation comes from Müller et al. 2016,
"A robust method to extract the rotational part of deformations",
warm-started from the last step, so three iterations are enough. Balls
collide with the particles and with the top as a height field, so a fast
ball can dent the jelly but never end up inside it.

## Why not FEMFX

FEMFX (`kke::PhysicsModule`) solves real volumetric elasticity and
fracture; that's the right tool for breakable hero objects. Jiggle is
secondary motion that has to run on every character every frame: FEMFX
costs about 0.2 ms per body per step on its own and needs a tet mesh; the
rig above costs tens of microseconds for a whole character and needs
nothing but the skeleton. For a jelly you want to *break*, FEMFX is still
the tool (see the physics demo's rubber ball); for a jelly that wobbles,
`JellyBody` is ~0.8 ms and works on builds without FEMFX (the default).

## The demo

`cd build/bin && ./jiggle_demo`. `Tab` switches scenes.

- **Jelly**: balls rain onto a jelly with fruit set in it. `Space` toggles
  the rain, `B` drops a big ball, `P` squishes it, `R` resets; sliders for
  firmness, iterations, damping, and the look: flavour (strawberry, lime,
  blue raspberry, orange, panna cotta, clear gelatin), density, milkiness.
  `KKE_JELLY_LOOK=0..5` picks the flavour.

  The jelly is drawn with `DynamicMeshRenderer::drawTranslucent`, a
  reusable translucent material (`shaders/translucent*.{glsl,frag}`): one
  pass multiplies what's behind by the transmittance (Beer-Lambert through
  a thickness that grows toward the silhouette, so edges look richer, as
  real jelly does), a second adds Fresnel reflection, light scattered
  inside, light shining through from behind and a glossy highlight. No
  sorting or extra render targets; draw it after the opaque scene.
- **Body**: a Synty character reshaped with `addHumanoidSoftTissue`,
  running a circle on UAL clips: stand, jog, jump, sprint, stop dead, jump,
  walk (`5`, the tour), or `1`-`4` for one gait, `Space` to jump. The camera
  follows from the side. "Twin without jiggle" adds the same body without
  jiggle half a lap behind; "Show points" draws the simulated points
  (pink) against their animated targets (blue). Breast and glute sliders.

Environment: `KKE_JIGGLE_SCENE=body`, `KKE_JIGGLE_MOVE=0..4`,
`KKE_JIGGLE_CHARACTER=SK_...`, `KKE_JIGGLE_TWIN=1`,
`KKE_JIGGLE_VIEW=yaw,pitch,distance` (screenshots), `KKE_JIGGLE_TRACE=1`
(logs swing and stretch every frame). The body scene needs a Synty
character pack and `assets/animations/UAL1_Standard.fbx`; see SCENES.md
for exactly which.
