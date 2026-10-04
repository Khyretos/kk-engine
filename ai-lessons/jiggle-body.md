# Lesson: a realistic body with no skeleton, auto-rigged, with jiggle and hair (kk-engine, jiggle_demo)

Roles: worker (C++ engine + demo), runner (headless screenshots), reviewer (what to look at), shared (asset checks).
Repo: kk-engine. Files: `engine/include/kke/AutoRig.h`, `engine/src/AutoRig.cpp`, `tests/test_auto_rig.cpp`,
`games/jiggle_demo/JiggleDemoModule.{h,cpp}`, `docs/AUTO_RIG.md`, `tools/model_info/main.cpp` (`--bones` prints rest positions).

## What was asked

Kees: in the jiggle demo, use the realistic body from `female_body.zip` (free CGTrader model, Character Creator 4)
instead of a Synty character; animate it, jiggle breasts and buttocks, and give her our strand hair (she is bald).

## What went wrong and what fixed it

1. **The zip had no rig, and it took a day to say so plainly.** The listing said "rigged, game ready", but
   `female_body.zip` holds only `Obj/obj.obj` + textures (+ `obj.ObjKey`, a Reallusion re-import key, binary, not a rig).
   I asked Kees to upload the FBX and waited; Kees was annoyed ("I gave you the body days ago").
   Rule: **open the archive first, then report what is in it in one sentence, and offer to work with what is there.**
   Check:
   ```sh
   KKE_ASSETS_DIR=/mnt/project-files/kk-engine/assets-cache tools/fetch_assets.sh female_body
   find /mnt/project-files/kk-engine/assets-cache/female_body -type f ! -iname '*.jpg' ! -iname '*.png' ! -iname '*.tga'
   ```
   An OBJ never has bones. An FBX may: `build/bin/kke_model_info FILE.fbx --bones` lists them (0 bones = no rig).
2. **The OBJ loaded 100x too big** (a giant foot filled the screen). OBJ has no units; Character Creator writes cm.
   Fix: if an unskinned body is taller than 10 m, multiply positions and bounds by 0.01.
3. **Grey shells over the eyes.** CC4 eye occlusion, tear line and eyelash meshes use opacity maps; the engine draws
   them opaque. Fix: drop meshes whose material name contains `Occlusion`, `Tearline` or `Eyelash`.
4. **Auto-rig design that worked** (`kke::autoRigHumanoid`, 26 ms for 14k points):
   turn the mesh to face like the reference (`kke::modelForward`); scale UAL's skeleton to her height; move legs and
   spine joints to the centroid of the mesh slice at their height; arms: principal axis of the arm's points (power
   iteration on the covariance), elbow/wrist at UAL's proportions; weights = nearest bone segments blended over 3 cm,
   smoothed 4 passes over **welded** points (seams!), left bones never on the right half; then pose the mesh into UAL's
   T-pose so every bone rests turned exactly as UAL's. Why the T-pose step matters: `retargetAnimations` copies the
   rotation *change from rest*; an A-pose rest would put every clip's arms 29 degrees too low.
5. **The unit test failed on a coarse mesh** ("no torso found at the height of spine_02"): the 1.5 cm slab fell
   between two rings. Fix: widen the slab (1.5, 3, 6 cm) until points are found.
6. **Braids went through her back and out of her chest.** Cause: the hair rest pose only avoids the head sphere, so
   long hair rested inside the torso and the solver pushed it out the front. Fix in the demo: pass her real facing to
   `hairstyleOnHead(..., up, front)` (it was +Z, she faces -Z after rigging), set `HairDesc::down` a little backwards
   (`normalize((0,-1,0) - front*0.4)`), and add hair-only kinematic proxies (neck capsule, shoulder capsule, a chest
   box fitted to the mesh) that follow the `spine_03` bone. Snap them on the first frame (`setTransform` +
   `resetHair`), never `moveKinematic` from the origin.
7. **Teeth showed through her lips when the head turned.** The jaw and lower face were weighted half to the neck,
   so the skin moved apart from the rigid teeth. Fix: points above the head joint, in front of it (>3.5 cm, scaled) and
   near the middle (<10 cm) go 100% to the head, applied again after every smoothing pass (smoothing undid it).
   Also: a flat-topped chest proxy at shoulder height made wavy hair fan out sideways; moving its top down to 30 cm
   below the head centre fixed it.
8. **clang caught an unused constant GCC did not** (`-Wunused-const-variable`). Always run the clang syntax check.

## Exact commands (all run in the cloud container)

```sh
cd /home/user/kk-engine
ninja -C build jiggle_demo kke_tests
./build/bin/kke_tests --gtest_filter='AutoRig*:Jiggle*'
python3 tools/ci/check_std_includes.py
# clang syntax check of one changed file:
cd build && f=games/jiggle_demo/JiggleDemoModule.cpp
cmd=$(ninja -t commands | grep -F -- "-c /home/user/kk-engine/$f" | head -1 | sed -E 's#^/usr/bin/c\+\+#clang++#; s# -o [^ ]+ # #; s# -MD -MT [^ ]+ -MF [^ ]+# #; s#-mfpmath=sse##')
eval "$cmd -fsyntax-only -Wall -Wextra -Werror"
# headless screenshot (Xvfb + lavapipe):
Xvfb :91 -screen 0 1280x720x24 &
KKE_ASSETS_DIR=/mnt/project-files/kk-engine/assets-cache KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_HIDE_UI=1 \
KKE_JIGGLE_SCENE=body KKE_JIGGLE_MOVE=2 KKE_JIGGLE_VIEW=-0.6,-0.05,2.0 DISPLAY=:91 timeout 45 ./bin/jiggle_demo &
sleep 36; DISPLAY=:91 import -window root shot.png
```
`KKE_JIGGLE_VIEW` yaw is measured from her side when "Side view" is on (1.57 = her back, -1.57 = her front).
`KKE_JIGGLE_TRACE=1` logs the peak swing: about 4 degrees idle, 27-31 jogging/sprinting.

## Could a 9B local model do this alone?

- The auto-rigger: **no**, not from scratch. It needs 3D maths (quaternion shortest arc, PCA, rest-pose
  composition) and judging screenshots. With this lesson and `docs/AUTO_RIG.md` it could **tune** it (blend width,
  smoothing, slab sizes) if given one parameter at a time and a screenshot command.
- The asset check (step 1) and the unit fixes (steps 2, 3, 7): **yes**, with the exact commands above.
- The hair clipping fix: only with the cause spelled out (rest pose vs. collision proxies); a 9B model tends to
  raise collision radii instead, which does not fix hair that starts inside the body.
