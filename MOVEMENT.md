# MOVEMENT.md — how characters move, and why

`kke::Locomotion` (`engine/include/kke/Locomotion.h`) is the movement layer
between input actions and `RigidWorld`'s character controller. Its rules
come from PointDown's controller, "mechanics museum" and parkour videos
(https://www.youtube.com/@PointDown). They're Godot videos, but the
principles are engine-independent. This file keeps the notes and says
where each principle lives in the code.

Tested in `tests/test_locomotion.cpp` (12 tests, including the whole
kke_demo parkour lane at 60 and at 15 fps). You can play it in `kke_demo`:
walk to the lane at x = 20, or run `KKE_DEMO_AUTOPILOT=1 ./kke_demo` to watch
it run the lane by itself.

## The frame

PointDown's controller core (*Advanced controller core*, E3BxMgmP4m0) splits
a character into **input → model → presentation**:

1. **Input**: `InputModule` / `InputMap` gather actions (`move`, `sprint`,
   `walk`, `crouch`, `jump`). The game never reads keys.
2. **Area awareness** runs next, before any state logic, and is the only
   code that asks the world questions (`Locomotion::probe`).
3. **Translation, per state**: the current state turns actions into a move.
   `jump` on the ground becomes a *vault* if a thin, hip-high obstacle is
   ahead, a *climb* if there's a top with room, and a *jump* otherwise
   (*Designing AAA parkour system*, veOpS2S8Lco: "input actions are not
   move names").
4. **Update**: the state moves the capsule.
5. **Presentation**: the Animator picks poses from the state. Animations are
   *pose providers*: they never decide where the capsule goes
   (*Root motion once and for all*, _PHRm2EqfX0).

## Area awareness: dynamic, no markup

PointDown compares static awareness (trigger volumes placed by a designer)
with dynamic awareness (ray casts that read the level). Dynamic costs more
code but lets a level designer drop in any mesh (*Designing AAA parkour
system*; *Ledge actions*, IMSFMmekFxg). We use dynamic awareness because
the Synty levels have hundreds of props nobody will mark up. `probe()`
costs about 10–20 ray casts and 2–3 capsule tests, and runs only on the
frame "go up" is pressed, or every frame while in the air.

- **Face**: rays forward at knee, hip and chest height. Anything lower is a
  stair the controller steps up by itself.
- **Top**: one ray down, just past the face, from above the highest top this
  sensor can reach. A ray that starts inside geometry means the obstacle is
  too tall.
- **Depth**: rays down across the top until it drops away. Thin is a fence
  (vault); a platform is a climb.
- **Room**: capsule tests for a tucked body over the top, and for a standing
  body at the landing or on the top.
- **Sensors per state** (PointDown's "ray slice" resources): walking looks
  0.7 m ahead and reaches 1.9 m, sprinting looks 1.6 m ahead and reaches
  2.2 m, and the air sensor reaches 2.1 m. A 2.1 m ledge is only climbable
  from a sprint.

## Floor: three tiers, not two

From *Designing AAA parkour system*: on the floor; up to 25 cm above the
floor, which still counts as walking (snap down, no fall animation on a
kerb or a stair edge going down); and in the air. That removes the
fall/land stutter on small drops. Coyote time (0.12 s) and a jump buffer
(0.15 s, PointDown's "queued input") come from the same idea: don't make
the player hit a one-frame window.

## Turning (*Smoother turn movement*, MM1, ysKxT3q4tA8)

- **Speed and direction are separate.** Input rotates the direction at a
  limited rate (600°/s running, 320°/s sprinting) instead of replacing the
  velocity. The head doesn't jump 40 cm in one frame when you mash A/D.
- **Sharp turns slow you down** to 60% while the turn lasts ("creatures
  that value their lives drop speed in turns").
- **A 180° reversal keeps its side.** Near 180° the sign of the angle
  flips on float noise, and the turn would change sides every frame; the
  last turn direction wins inside ±12° of 180°.
- **The animation follows the measured speed**: the blend space gets how
  far the feet actually moved, not the requested speed. Legs slow down in
  turns and stop against walls.

## Jumping and the air (*Jump & Fall control*, MM2, vPFAh2T8Ipg)

- **Air control is a small acceleration** (5 m/s²), added to the flight and
  capped at the take-off speed (at least 1.6 m/s for standing jumps). It
  corrects a jump; it doesn't fly the character.
- **Facing is stored separately from velocity.** When a wall deflects the
  flight, the body still faces where you meant to go.
- **Take-off is a glue window.** The last grounded frame may re-aim the run
  at the input, so a jump straight after a turn goes where you asked.
- **Ledge grab in the air**: rising slowly or falling, steering into a wall
  whose top is within reach turns into a climb.

## Vault and climb (*Parkour ep3*, rhzwhJPb-jQ; *Ledge actions*)

- The capsule becomes **kinematic** for the move (`RigidWorld::
  setCharacterKinematic`). It follows a path that was checked for room
  before the move started, so there's no fight with the solver halfway
  over.
- **Correction window** (first 0.2 s): the body turns square to the
  obstacle, whatever angle you came in at.
- **Vault path**: PointDown's ledge-leap parabola (`leapParabola`). It is
  solved through the start and the landing, with a peak that clears both
  edges of the top. Horizontal speed stays constant, so a speed vault
  keeps its momentum (90% of the entry speed).
- **Climb path**: up the wall to the edge (hands on top), then over it onto
  the top.
- At the end the momentum goes back to the controller, and the character
  is on the floor that the probe found (no "stand on thin air" snap).

## Animations

The Universal Animation Library "Standard" set in `assets/animations/` has
**no vault or climb clips** (its 43 clips are locomotion, jump, crouch,
combat, sitting, swimming and interaction). kke_demo uses stand-ins: the
tucked jump pose for the vault, the take-off reach and a crouch step for
the climb. States look up `Vault`, `Climb_Up` and `Climb_Over` clips by name
first, so a pack that has them (Quaternius' full UAL, for example) drops
in without code changes.

## Not done yet (next)

- Ledge hang and shimmy, corners, ledge-to-ledge leaps (*Ledge actions*),
  wall run.
- Two-bone / CCD IK to put the hands on the edge and the feet on the ground
  (*IK fundamentals with CCD*, 8pX6LeZdpOo; *SkeletonModifier3D*,
  xpoPfUKI9tw: modifiers run after the animation in a fixed order, and
  each is small).
- Animation layering (upper body over locomotion; *Animation layering
  pipelines*, Fsa2wxyQvzM, blocked below).
- Root-motion extraction as data (velocity tracks) for authored vaults
  (*Root motion once and for all*).

## Transcripts

Fetched 2026-09-26 as auto-captions. YouTube blocks this server after
about a dozen requests, so these videos didn't come through: *God Tier 3D
Character Controller* (qIf5YQ8qJng), *Detect climbable ledges*
(yxWxHfjNpa4), *Animation layering pipelines* (Fsa2wxyQvzM), *Post-start
melee attack redirection* (WGZ-QG-0cpw), *Use professional AAA practices*
(FgO5edghqRE), *Importing animated 3D characters* (a0_JVEY7sbY). Their principles overlap with the videos above; retrying
from another network would complete the set.
