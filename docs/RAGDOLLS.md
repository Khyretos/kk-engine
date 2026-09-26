# Ragdolls

A ragdoll is plain data (`kke::RagdollDesc` in `engine/include/kke/Ragdoll.h`):
boxes and the joints between them. Whatever physics module offers
`kke::IRagdollPhysics` simulates it. Jolt (`RigidBodyModule`) has joint limits
and limbs that collide with each other, so `kke::bestRagdollPhysics()` picks it
when it's there. FEMFX (`PhysicsModule`) also works, but it has no limits.

Out of the box you get a ragdoll that moves like a body: knees and elbows fold
one way, a hip swings far forward and only a little back, and a horse's back
stays stiff. Every limit can be changed before the ragdoll is created.

## Presets

| Builder | Rig | Bodies |
|---|---|---|
| `buildHumanoidRagdoll(model, boneWorld, mass = 70)` | Synty / Unreal names (`Pelvis`, `spine_01..03`, `UpperArm_L`, `calf_r`, ...) | 11: pelvis, torso, head, upper/lower arms, thighs, calves |
| `buildQuadrupedRagdoll(model, boneWorld, mass = 500, &missing, bones)` | Quaternius animals by default (`Hips`, `Torso`, `FrontUpLeg.L`, `BackLowLeg.R`, `Tail1..4`, ...); other names via `kke::QuadrupedBones` | 12-13: pelvis, chest, neck, head, 4 upper and 4 lower legs, tail if there is one |

Both builders start the ragdoll from the pose the character is in right now,
fill in which bones ride which body (`RagdollBody::bones`, used by
`bindSkeletonToRagdoll`), and tell you the first missing bone if the skeleton
doesn't fit.

## The default limits

Ball joints are oval cones. `swingDegrees` is the main swing (for a hip, forward
and back), and `swingSideDegrees` is the swing across it (sideways). A joint
whose range isn't symmetric has its cone leaned to the middle of the range: a
hip that flexes 120 degrees and extends 20 is a 70-degree cone leaned 50 degrees
forward. Hinges (knees, elbows, hocks) fold one way, with a few degrees of give
the other way.

Humanoid limits are measured in the body's own frame, so a ragdoll made
mid-run gets the same range as one made standing:

| Joint | Range |
|---|---|
| `spine` (pelvis to torso) | 40 forward, 25 back, 25 to each side, twists 30 |
| `neck` (torso to head) | 50 down, 60 up, 40 to each side, turns 70 |
| `shoulder_l/r` | 80 forward/back and 85 up/down around out-down-forward, turns 60 |
| `elbow_l/r` | 0 to 145 |
| `hip_l/r` | 120 forward, 20 back, 45 out, 25 in, turns 35 |
| `knee_l/r` | 0 to 140 |

Quadruped limits are centred on the rig's rest pose, so a horse that ragdolls
while grazing can still lift its head:

| Joint | Range |
|---|---|
| `spine` (pelvis to chest) | 25 up/down, 15 sideways, twists 15 |
| `neck` (chest to neck) | 45 up/down, 35 sideways, twists 20 |
| `head` | 35 nod, 25 sideways, twists 25 |
| `tail` | 45 each way, twists 20 |
| `hip_fl/fr/bl/br` (leg to body) | 45 forward/back, 12 sideways, twists 10 |
| `knee_fl/fr` (front knee) | 0 to 130, folds the hoof backward |
| `knee_bl/br` (hind hock) | 0 to 110, folds the hoof forward |

Joints also resist moving a little (muscle tone). `frictionTorque` is scaled
from the lighter body's mass by default, so a pug and a horse both look right.

## Overriding

Change the desc before handing it to `createRagdoll`:

```cpp
kke::RagdollDesc desc = kke::buildHumanoidRagdoll(model, boneWorld, 80.0f);
desc.findJoint("knee_l")->hingeMaxDegrees = 90.0f;     // a stiff knee
desc.findJoint("neck")->limited = false;               // cartoon neck
if (auto* hip = desc.findJoint("hip_r")) hip->swingSideDegrees = 60.0f;
desc.scaleLimits(1.3f);                                // everything a bit looser
auto ragdoll = physics->createRagdoll(desc, velocity);
```

`scaleLimits` multiplies every cone and twist, and how far every hinge bends
(the straight stop stays where it is). Values are capped at what each joint type
can do. A desc built by hand works too: joints without a name, with
`swingSideDegrees < 0`, get a round cone.

## Trying it

`synty_demo` with the POLYGON Prototype pack (and, if it's in the same asset
folder, Quaternius' *Farm Animals Animated*): **Shift+R** ragdolls everyone,
including a horse and a pug, and **T** stands them back up.

Assets used by that scene: POLYGON Prototype (characters, floor, walls, props),
Quaternius Farm Animals Animated (`Horse.fbx`, `Pug.fbx`). Neither is in the
repository: see `assets/README.md`.

## Tests

- `tests/test_ragdoll.cpp` covers joint names, human ranges, overrides and
  `scaleLimits`, a quadruped from a Quaternius-shaped rig (hind hocks fold the
  other way from front knees), and a neck range centred on the rest pose. It
  also runs on the real farm animals if they're in `KKE_ASSETS_DIR`.
- `tests/test_jolt_ragdoll.cpp` covers the physics side. With the pelvis held
  still, a hip kicked forward reaches ~120 degrees and one kicked back stops at
  ~20; an unlimited hip keeps going. A 500 kg horse knocked over keeps every
  knee inside its range.
