# Procedural animation

Motion worked out every frame instead of played back from a clip: feet that
find the ground, animals with any number of legs that walk, trot and gallop
without a single walk clip, heads that turn toward what they notice, and
bodies that stagger when hit and catch themselves (or don't).

> **Status (2026-09-26):** the API below is the contract and is landing in
> stages; the header is published first so games (farm wildlife, pets) can
> build against it.

Everything lives in [`kke/ProceduralAnim.h`](../engine/include/kke/ProceduralAnim.h)
(the reference) on top of what was already there:

| Already in the engine | Where |
| --- | --- |
| Clips, blend spaces, crossfading state machine | `kke/Animator.h` |
| Two-bone IK, biped foot placement, retargeting | `kke/AnimRig.h` |
| Ragdolls (humanoid, quadruped), joint limits | `kke/Ragdoll.h`, [RAGDOLLS.md](RAGDOLLS.md) |
| Secondary motion (tails, ears, soft bodies) | `kke/JigglePhysics.h`, [JIGGLE.md](JIGGLE.md) |
| Locomotion controller (what moves the capsule) | `kke/Locomotion.h`, [MOVEMENT.md](MOVEMENT.md) |

Nothing here is new science. The algorithms are the standard published ones:
FABRIK for long chains (Aristidou & Lasenby 2011), the Raibert heuristic for
where a foot lands (Raibert 1986), gait phase tables from biomechanics
(Hildebrand; Alexander 1984, including the Froude number that tells every
animal when to change gait), and motor-driven active ragdolls exactly the way
Jolt's own `JPH::Ragdoll::DriveToPoseUsingMotors` does it (Jolt is already a
dependency; no new library was needed).

## The layers, in order

Every piece is a layer that takes a `kke::Pose` and changes it, with a weight
so it can fade in and out:

```
Animator (clips)                      or the rest pose, for a creature with no clips
  -> blendPosesMasked / addPose       upper body waves while the legs walk; additive flinch
  -> ProceduralGait + applyGait       procedural legs          (or LegPlacer: clip legs on hills)
  -> LookAt                           head / spine toward a target
  -> solveTwoBone / solveFabrik       hands on things, tails, tentacles
  -> JigglePhysics                    secondary motion
  -> ActiveRagdoll                    physics follows all of the above; hits push it off
```

## Legs for any animal: `ProceduralGait`

The controller (AI, player input, `kke::Locomotion`) moves the creature as a
point with a heading. `ProceduralGait` plans the footsteps: each foot stays
planted in the world until its turn in the gait, then swings in an arc to
where it will be needed next, found by asking the ground. The body rises and
settles with the terrain, pitches and rolls to its feet, bobs at push-off and
leans into turns.

```cpp
#include "kke/ProceduralAnim.h"

// A dog-sized quadruped: 0.8 m long, 0.3 m wide, hips 0.5 m up.
kke::ProceduralGait gait(kke::makeLegs(4, 0.8f, 0.3f, 0.5f));

auto ground = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
    auto h = physics->physicsRaycast(from, {0, -1, 0}, 3.0f); // any IPhysicsWorld
    if (!h.hit) return false;
    hit = h.point; normal = h.normal; return true;
};

gait.reset(bodyWorld, ground);                                // once, and after teleports
// every frame, after the controller moved the body:
gait.update(bodyWorld, velocity, turnRateDegrees, ground, dt);
for (int i = 0; i < gait.legCount(); ++i) {
    const auto& f = gait.foot(i);                             // f.position, f.normal, f.planted
    if (f.landed) footstepAt(f.position);                     // sound and dust
}
glm::mat4 drawnBody = gait.bodyPose();                        // for the torso / pelvis
```

**Legs** are numbered front to back, left then right: a biped is L, R; a
quadruped FL, FR, BL, BR (the same order as `kke::QuadrupedBones`); six legs
L1, R1, L2, R2, L3, R3; eight likewise. `makeLegs(count, length, width,
hipHeight)` lays out mirror pairs; a `LegDesc` per leg (hip, rest foot, upper
and lower segment lengths, in body space: +Z forward, origin on the ground)
describes anything else.

**Gaits** (`kke::Gait`): walk, trot, pace, canter, gallop, bound, pronk,
tripod, wave, and `Auto`, which picks from speed with the Froude number
v²/(g·hip height): quadrupeds walk below 0.5, trot to 2.5 and gallop above;
bipeds walk then run; six and eight legs wave then run on tripods. Because the
thresholds are relative to leg length, a pug and a horse both change gait at
the right speed for their size. Gaits change mid-stride without feet
teleporting: a leg finishes its swing before the new timing takes over.

**Timing.** A cycle is between `cycleFast` and `cycleSlow` pendulum periods of
the leg (2π√(L/g)), and never so long that a foot would have to drag further
than `strideScale` × leg length. Standing still, feet that ended up far from
rest (`resettle`) step back under the body, one gait beat at a time.

### On a skeleton

For a rigged animal (the Quaternius farm animals, anything with
`QuadrupedBones` names) the same gait drives the bones:

```cpp
auto chains = kke::quadrupedLegChains(model);                 // FL, FR, BL, BR
kke::ProceduralGait gait(kke::legsFromSkeleton(model, chains, modelToBody));
int hips = /* index of "Hips" */;
// per frame:
kke::Pose pose = animSet.restPose();                          // or an Animator pose (idle, eat...)
kke::applyGait(model, pose, chains, hips, gait, instanceTransform);
```

`applyGait` moves the hips by the gait's body sway and puts every foot on its
planned spot with two-bone IK. Knees keep bending the way they bend at rest,
so front knees and hind hocks fold opposite ways.

### Clip legs on hills: `LegPlacer`

An animal with good walk and run clips doesn't need procedural legs, only
feet that meet the ground. `LegPlacer` is `FootPlacer` for any number of
legs: each foot keeps its animated height above the ground under it, the body
drops as far as the lowest foot needs and pitches / rolls to the slope, and
two-bone IK bends the legs.

```cpp
kke::LegPlacer legs(kke::quadrupedLegChains(model), hips);
legs.apply(model, pose, groundInModelSpace, dt, onGround ? 1.0f : 0.0f);
```

## Look-at and aim: `LookAt`

```cpp
kke::LookAt look = kke::LookAt::quadruped(model);   // Neck, Head
// or kke::LookAt::humanoid(model)                  // spine_02, spine_03, neck_01, head
glm::vec3 target = worldToModel * glm::vec4(player, 1);
look.apply(model, pose, noticed ? &target : nullptr, dt);
```

The turn is shared along the chain (more in the head, less in the spine),
limited (`maxYaw`, `maxPitch`) and smoothed (`speed`). A target further round
than `giveUpYaw` is dropped and the head comes back to forward rather than
snapping over the shoulder. For aiming, build a `LookAt` from any chain of
`Link{bone, share}` and pass the weapon's direction as `forward`.

## Long chains: `solveFabrik`

Two-bone IK (`kke::solveTwoBone`) is exact for arms and most legs. Tails,
necks, tentacles and three-segment insect legs use FABRIK:

```cpp
kke::IkChain tail = kke::findIkChain(model, "Tail1", "Tail4");
kke::solveFabrik(model, pose, tail, targetModelSpace, &pole);
```

`fabrikPoints` does the same on bare joint positions (creatures drawn from
primitives), and `kneePosition` gives the knee of a two-segment limb.

## Layering clips: `blendPosesMasked`, `addPose`

```cpp
kke::BoneMask upper = kke::boneMask(model, {"spine_01"});        // spine and everything above
kke::blendPosesMasked(walkPose, wavePose, upper, 1.0f, pose);    // walk + wave
kke::addPose(pose, flinchPose, flinchFirstFrame, {}, 0.7f, pose); // additive flinch on top
```

## Active ragdoll: hit reactions and balance

A ragdoll whose joints have motors, driven toward the animation: at full
strength it looks exactly like the clip; a hit weakens the muscles around the
body that was hit (and, less, the joints next to them), knocks the balance,
and both recover over time. Too far off balance (the torso leaning more than
`fallTilt` from where the clip has it, or the pelvis sinking by `fallDrop`)
and it goes limp, lies there for `getUpDelay`, then blends back to the clip.

```
Animated --hit()--> Active --steady for calmSeconds--> Animated
                      \--off balance--> Fallen --getUpDelay--> GettingUp --> Animated
```

```cpp
kke::ActiveRagdoll active(desc);                            // desc from buildHumanoidRagdoll / buildQuadrupedRagdoll
// when something hits the character:
if (!active.physical()) handle = ragdolls->createRagdoll(desc, velocity);
active.hit(body, push);
ragdolls->pushRagdollBody(handle, body, push);
// every frame:
active.setTargets(binding, animatedBoneWorld);              // where the clip has the bones
std::vector<glm::mat4> bodies;
ragdolls->ragdollBodyTransforms(handle, bodies);
active.update(dt, bodies);
if (active.physical()) ragdolls->driveRagdoll(handle, active.drive());
else if (handle) { ragdolls->destroyRagdoll(handle); handle = 0; }
// skin from poseFromRagdoll(...) while physical(); in GettingUp blend with
// blendPoses(ragdollPose, animatedPose, active.getUpBlend()).
```

`IRagdollPhysics::driveRagdoll` is implemented by the Jolt module
(`RigidBodyModule`): swing-twist and hinge motors in position mode, torque
limited by strength (`torquePerKg` × the heavier body's mass at full
strength), plus an optional pull on one body straight to its target
(`assist`, the pelvis) that is what keeps a hit character on its feet and
goes away when balance is lost. FEMFX ragdolls have no motors: there
`driveRagdoll` returns false and characters simply go limp.

## Play-to-make

Following [PLAY_TO_MAKE.md](PLAY_TO_MAKE.md), the same blocks are open at all
three levels:

| Simple (drag and drop) | Node | Lua |
| --- | --- | --- |
| Bat a person: they stagger, catch themselves or fall | "Stagger" | `play.stagger(thing, push)` |
| — | "Look at" | `play.lookAt(thing, target)` / `play.lookAt(thing)` to stop |
| — | "Walk like" | `play.gait(thing, "trot")` |

## Trying it

`games/procedural_demo` (no Synty assets needed): a spider, a beetle, a dog
and a biped built from the gait alone walk over bumpy ground after the
target you click, watch the camera, and stagger when you hit them. See its
README for keys and `KKE_PROC_*` switches.

## Tests

`tests/test_procedural_anim.cpp`: gait tables (diagonal pairs trot together,
duty factors), Froude gait changes, feet staying planted while in stance,
steps landing on raised ground, body pitch on a slope, look-at limits and
give-up, FABRIK reach and bone lengths, masked and additive blending, the
active ragdoll state machine, and the Jolt motor drive holding a limb on
target (`tests/test_jolt_ragdoll.cpp`).
