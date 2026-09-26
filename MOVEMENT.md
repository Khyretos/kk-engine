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
it run the lane by itself. The Synty scenes (SCENES.md) are real-art trails:
`SceneTrails.*` runs them headless when the packs are present.

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
costs about 15–30 ray casts and 2–4 capsule tests, and runs only on the
frame "go up" is pressed, or every frame while in the air.

- **Face**: rays forward at knee, hip and chest height, from the middle and
  both shoulders (a centre ray slips through the seam between two fence
  panels). Anything lower is a stair the controller steps up by itself.
- **Top**: one ray down, just past the face, from above the highest top this
  sensor can reach, and a second one further in when the rim is bevelled
  (rocks). A ray that starts inside geometry means the obstacle is too tall.
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

After the Animator, two small modifiers run in a fixed order
(`engine/include/kke/AnimRig.h`, PointDown's *SkeletonModifier3D* idea):

- **Feet on the ground.** Each foot keeps its animated lift above the
  ground under it, the hips drop when one foot has to go lower than the
  capsule's floor (stairs, slopes, rock), and analytic two-bone IK bends
  the legs. Each foot also tilts to lie along the ground under it (the
  ray's normal, at most 30 degrees), keeping the animated foot angle on
  top of that. Smoothed over frames, and off in the air.
- **Hands on the edge.** During the first part of a vault or climb, the
  hands go to the top edge the probe found, shoulder-width apart, with
  the elbows bending out and back. It stands in for the hand plant the
  stand-in clips don't have.

**Any Synty character can wear the UAL clips.** Bones pair up by name
(UAL and Synty both follow the Unreal mannequin, give or take case and
Synty's `indexFinger`/`finger`), and each paired bone copies the source's
rotation change from its rest pose, turned by the difference in facing
(UAL faces -Z, Synty +Z); the pelvis travel scales with leg length. The
match logs which bones stayed at rest (Synty's eyes, eyebrows and toes).
*Importing animated 3D characters* (a0_JVEY7sbY) puts it as a contract: a
clip only means something for the skeleton it was made for, so a
mismatch should be visible, not silent. Try it in kke_demo's Character
panel, or `KKE_CHARACTER=SK_Character_Father_01 ./kke_demo`.

**Root motion** is available as data: `AnimationSet::extractRootMotion`
moves a bone's horizontal travel out of the clips into a track, the clip
plays in place, and `Animator::rootMotion()` reports the travel each
update for the game to apply. Locomotion clips don't use it (the
controller moves the capsule, and the blend space follows the measured
speed); it's for authored moves such as a real vault clip.

## Not done yet (next)

- Ledge hang and shimmy, corners, ledge-to-ledge leaps (*Ledge actions*),
  wall run.
- CCD for longer chains (*IK fundamentals with CCD*, 8pX6LeZdpOo); the
  legs and arms use the analytic two-bone solve.
- Standing still on a walkable slope creeps downhill (~5 cm/s on the
  24 degree ramp); the character should hold its place.
- Animation layering (upper body over locomotion; *Animation layering
  pipelines*, Fsa2wxyQvzM, blocked below).
- Rest-pose matching for retargeting between skeletons whose rest poses
  differ a lot (A-pose vs T-pose arms); UAL and Synty are close enough.

## Transcripts

Fetched 2026-09-26 as auto-captions. YouTube blocks this server after
about a dozen requests, so these videos didn't come through: *God Tier 3D
Character Controller* (qIf5YQ8qJng), *Detect climbable ledges*
(yxWxHfjNpa4), *Animation layering pipelines* (Fsa2wxyQvzM), *Post-start
melee attack redirection* (WGZ-QG-0cpw), *Use professional AAA practices*
(FgO5edghqRE). *Importing animated 3D characters* (a0_JVEY7sbY) came
through on a retry. Their principles overlap with the videos above; retrying
from another network would complete the set.
