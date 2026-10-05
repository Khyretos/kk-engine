# Auto-rigging a body with no skeleton

`kke::autoRigHumanoid` ([kke/AutoRig.h](../engine/include/kke/AutoRig.h))
gives a humanoid mesh that has no bones (an OBJ export, a sculpt, a scan)
the skeleton and skin weights of a reference rig, so it plays that rig's
animation clips. The jiggle demo uses it for the realistic body
(`female_body`, a Character Creator 4 OBJ): she runs, jumps and stops on
the UAL clips like any rigged character.

```cpp
#include "kke/AutoRig.h"

kke::ModelData ual = kke::loadModel("assets/animations/UAL1_Standard.fbx");
kke::ModelData body = kke::loadModel("female_body/Obj/obj.obj");   // no bones
// (an OBJ in centimetres: scale the vertices by 0.01 first; see below)
const kke::AutoRigReport r = kke::autoRigHumanoid(body, ual);
if (!r.ok) log->warn("can't rig: {}", r.reason);                 // body is left as it was
// body is now skinned, with UAL's bones, in UAL's T-pose:
const kke::BoneMatch match = kke::matchBones(ual, body);
body.animations = kke::retargetAnimations(ual, body, match);
```

It takes about 25 ms for a 14 000-point body on one core, so it runs at
load time; nothing is written to disk.

## What it does

1. **Turns the mesh** to face the way the reference faces
   (`AutoRigOptions::forward` says where the mesh faces; most exports
   face +Z, UAL faces -Z).
2. **Fits the skeleton.** The reference is scaled to the mesh's height,
   then the joints are moved into the body:
   - hips, knees and ankles to the middle of that leg at their height;
   - pelvis, spine, neck and head to the middle of the torso (front to
     back) at their height;
   - each arm: the axis of the arm's points (any angle: A-pose, T-pose or
     in between), the shoulder on it where the reference has it, the
     elbow and wrist at the reference's proportions of the arm's length,
     centred in the arm;
   - everything else (clavicles, fingers, toes, root) keeps its offset
     from its parent, scaled, and turned with the arm.
3. **Weights the skin.** Every point is weighted to the bone segments
   nearest to it, blended over `blend` (3 cm) past the nearest one, then
   smoothed over the surface (`smoothing` passes) so joints bend softly.
   Seams are welded first, so a UV seam or a material border never tears.
   Left bones never reach the right half of the body, nor the other way
   round (inner thighs stay apart). Fingers go with the hand (finger
   curls in clips don't show). Small parts inside the head (eyes, teeth,
   tongue) follow the head.
4. **Poses the mesh into the reference's rest pose.** An A-pose body is
   lifted into the T-pose, so every bone rests turned exactly as the
   reference's and `retargetAnimations` copies the clips unchanged.

`AutoRigReport` says how it went: `scale` (mesh height / reference
height), `armDropDegrees` (how far below the T the arms were), `points`
(welded skin points). Failing, `reason` names what wasn't found ("no left
arm found away from the body").

## What the mesh needs

- Meters, +Y up, standing on y = 0. OBJ files have no units: Character
  Creator writes centimetres, so a body 173 units tall is scaled by 0.01
  before rigging (the jiggle demo does this).
- Arms away from the body (an A-pose or T-pose; not hands on hips), legs
  apart.
- The reference needs UE-style bone names: `pelvis`, `spine_01`..`03`,
  `neck_01`, `Head`, `clavicle_l`, `upperarm_l`, `lowerarm_l`, `hand_l`,
  `thigh_l`, `calf_l`, `foot_l`, `ball_l` and the `_r` side. UAL has
  them; so do Synty and Mixamo rigs (matched by `canonicalBoneName`).

## Limits

- Weights by distance, not by a heat or voxel solve: good on a body,
  less so on loose clothes, long hair cards or a skirt (they follow the
  nearest bone).
- No fingers, no face bones. An artist's rig is always better when one
  exists: prefer a rigged FBX.
- Hands far below the hips, crossed arms or touching legs confuse the
  joint fit: the report's `reason` says which part was not found.

Tested by `tests/test_auto_rig.cpp`: an A-pose body of tubes is rigged to
a UAL-like skeleton; the arms end up in a T at shoulder height, the
fingertip goes with the hand, the feet with the feet, and no point is
weighted to the other side's limbs.
