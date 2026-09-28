# Equipment and body awareness

How a character holds and wears things, and how its arms know where its own
body is. Two headers, both pure CPU and unit-tested
(`tests/test_body_shape.cpp`):

| Header | What it gives you |
| --- | --- |
| [`kke/BodyShape.h`](../engine/include/kke/BodyShape.h) | The body as capsules fitted to the character's mesh, and an arm IK that goes round the body instead of through it |
| [`kke/Equipment.h`](../engine/include/kke/Equipment.h) | Sockets, slots and items with grips: a thing is held by its handle in the palm, and the fingers close round it |

> **Status (2026-09-28):** on main. Tennis holds its racket with it and
> Climb Race puts its hands on holds with it. A shooter's rifle (main grip
> plus foregrip) and an RPG's sword, shield and back/hip slots are the
> cases it was designed for; no demo uses the worn slots yet.

Nothing here is new: it is what Unreal calls a Physics Asset (capsules,
"sphyls", on the bones) and skeletal-mesh sockets, the collision capsules
Assassin's Creed and most third-person games keep on a skeleton, and the
slot grids of Escape from Tarkov or an ARPG's paper doll.

## The body: `BodyShape`

`BodyShape::fit(model)` builds one capsule per body part (pelvis, belly,
chest, upper chest, neck, head, thighs, calves, upper arms, forearms,
hands), each riding its bone. The sizes come from the character's own
skinned mesh: every vertex belongs to the part its strongest bone belongs
to, and the capsule wraps most of them (a percentile, so a stray vertex
doesn't inflate it). A broad Synty character and the thin UAL mannequin
both get a body their size. Without a skinned mesh the sizes come from the
bones' lengths.

Bones are found by their usual names (UAL, Unreal, Synty: `pelvis`,
`spine_01..03`, `neck_01`, `head`, `thigh_l`, `upperarm_r`, ...).

```cpp
const kke::BodyShape body = kke::BodyShape::fit(model);          // once per character model
std::vector<kke::Capsule> now = body.posed(kke::poseToModel(model, pose)); // this frame
```

## An arm that knows the body

The body-aware `solveHumanArm` overload is the plain one
([PROCEDURAL_ANIMATION.md](PROCEDURAL_ANIMATION.md)) plus the body:

1. a hand aimed inside the torso, head or legs goes to just outside them;
2. the elbow swings round the shoulder-hand line (within the arm's swivel
   range) to where neither the upper arm nor the forearm is inside the
   body, preferring where it was last frame so it doesn't flicker;
3. an arm reaching up past the head makes the head lean away (up to
   `headTilt` degrees), and it leans back upright over a few frames;
4. if nowhere is clear, the hand gives way, never more than `maxShift`;
5. whatever the hand holds (`held`, capsules in the hand bone's space) is
   kept out of the body too.

```cpp
kke::BodyAvoid avoid;                 // margin 1 cm, the other arm counts too
avoid.held = equipment.heldShape(1);  // the racket, sword or rifle in the right hand
kke::BodyAvoidState state[2];         // keep per arm, frame to frame
kke::BodyAvoidResult r = kke::solveHumanArm(model, pose, rightArm, goal, body, avoid, &state[1]);
// r.arm.hand: where the hand went; r.penetration: 0 when the arm is clear
```

It costs about 0.1 ms per arm (the plain solve about 0.05 ms).

When the hand must stay put (a climber's hand on a hold), set `maxShift`
small and move the body instead: Climb Race moves the pelvis back off the
rock by `r.penetration` and solves again
([games/climb_race/README.md](../games/climb_race/README.md)).

## Holding things: sockets, slots, grips

- A **socket** is a frame on a bone. `Equipment(model)` makes the palms,
  the back (on the upper spine), the hips and the head from the rest pose.
- A **slot** is a place something goes: `LeftHand`, `RightHand`, `Back`,
  `HipLeft`, `HipRight`, `Head`.
- An **`Equippable`** says which slots it fits, its **grips** (where on
  it a palm goes and how thick the handle is there) and its **shape**
  (capsules, for the body awareness above).

The palm socket sits on the palm's skin where a handle lies: diagonally
from the heel of the hand toward the index knuckle, +Z out of the palm.
An item held by a grip has that grip's axis in the palm, so a racket or a
sword sits in the hand, not at the wrist.

```cpp
kke::Equippable racket;
racket.slots = kke::kBothHands;
// Item space: +Y along the handle toward the head, +Z the way the palm faces.
racket.grips.push_back({ "main", glm::translate(glm::mat4(1), { 0, 0.035f, 0 }), 0.016f, 0.05f });
racket.grips.push_back({ "support", supportFrame, 0.016f, 0.045f }); // a two-handed backhand
racket.shape = { /* shaft, rim, ... */ };

kke::Equipment eq(model);
eq.equip(kke::EquipSlot::RightHand, racket);                     // grip 0
eq.equip(kke::EquipSlot::LeftHand, racket, racket.grip("support")); // the same item, second hand

// Each frame: aim the arm so the item goes where the game wants it...
kke::ArmGoal goal = eq.armGoal(1, racket, 0, wantedRacketModel);
kke::solveHumanArm(model, pose, rightArm, goal, body, avoid, &state[1]);
// ...then draw it where the hand really got to, and close the hands.
const glm::mat4 racketModel = eq.itemTransform(kke::EquipSlot::RightHand, kke::poseToModel(model, pose));
eq.closeHand(model, pose, 1);
eq.closeHand(model, pose, 0);
```

A second hand on an item (a backhand, a rifle's foregrip) is the same
item equipped to that hand with another grip: the hand holding grip 0
places it, and `handFor` / `armGoal` tell the other arm where to reach.

## Fingers that close on what they hold: `wrapFingers`

`wrapFingers(model, pose, hand, surface, close, thumb)` curls each finger
joint toward the palm, knuckle first, until that finger touches the
surface or reaches a person's range (about 90 degrees at the knuckle, 105
in the middle, 75 at the tip; the thumb less). A `GripSurface` is handles
(capsules) and flat surfaces (planes: a rock face, a table). `closeHand`
builds the surface from the held grip's handle. Climb Race builds its own
from the hold (a ball for a jug or sloper, a thin capsule for a crimp, a
lip for an edge, and the rock).

`HandRig` (from `makeHandRig`) is what it measures from the rest pose and
the mesh: the finger bones (`_01.._03`, and the `_04_leaf` tips when the
rig has them), the palm frame, how long the hand is, how thick the
fingers are.

## Trying it

```sh
# Tennis, looking at the hands on the racket
KKE_TENNIS_BOTS=1 KKE_TENNIS_CLOSEUP_DISTANCE=1.5 ./tennis
# Climb Race, close behind the climber, and the hands-on-holds report
KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_CLOSEUP=1.6 KKE_CLIMB_QUIT=60 ./climb_race
```

## Tests

`tests/test_body_shape.cpp`, on the UAL mannequin (CC0, in
`assets/animations`): the fit matches the mesh, the animated arms are not
inside the body, six goals per arm that plain IK reaches through the body
(10 to 16 cm deep) come out clear, a hand aimed into the body stops at its
skin, a held stick stays out of the body, the palm faces where the
fingers curl, the fingers close on a handle without going through it, and
an item goes where the hand holds it.
