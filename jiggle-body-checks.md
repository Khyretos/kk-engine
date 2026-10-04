# Jiggle demo (realistic body, auto-rig, jiggle, strand hair): checks for soucouyant

Patch: `/mnt/project-files/kk-engine/patches/jiggle-body.patch` (one commit, `git format-patch`)
Base: Forgejo `main` at `fa1b7f1` (`git apply --check` clean on it)
PR branch name: `feature/jiggle-realistic-body`
PR title: `Jiggle demo: realistic body, auto-rigged, with jiggle and strand hair`

## Already checked in the cloud

- Full GCC build (every target): zero warnings.
- clang++ `-fsyntax-only -Wall -Wextra -Werror` clean on every changed .cpp
  (AutoRig.cpp, JiggleDemoModule.cpp, test_auto_rig.cpp, model_info/main.cpp).
- `tools/ci/check_std_includes.py` passes.
- `kke_tests`: 993 passed, 0 failed (2 new: `AutoRig.FitsTheSkeletonAndLiftsTheArmsIntoTheTPose`,
  `AutoRig.SaysWhyItCannotRig`).
- Headless (lavapipe) runs with the `female_body` pack: auto-rig 25 ms, 14063 points, scale 0.95,
  arms lifted 29 degrees; jiggle peak swing about 4 degrees at idle, 27-31 jogging or sprinting.
  Box braids, afro, locs and wavy hair stay outside the body; lips closed, no teeth showing.
  Screenshots: `jiggle-body-shots/`.
- No warnings in the game log (only the cloud's XDG_RUNTIME_DIR notice).
- Not run in the cloud: real GPU, Vulkan validation, Android, Windows.

## 1. Apply and build

```sh
cd /media/development/Software/kk-engine
git fetch FORGEJO main && git checkout -B feature/jiggle-realistic-body FORGEJO/main
git am /path/to/jiggle-body.patch
tools/runner/kkrun build        # expect: PASS build (zero warnings)
tools/runner/kkrun tests        # expect: PASS tests
```

The body needs the `female_body` pack (`tools/fetch_assets.sh female_body`, or Kees's extracted
folder under the assets dir). Without it the demo falls back to the Synty character with hair.
Never commit the pack.

## 2. Headless run

```sh
KKE_JIGGLE_SCENE=body KKE_JIGGLE_MOVE=2 tools/check_game jiggle_demo --seconds 30 --headless --log build/jiggle-body.log
grep -iE "auto-rig|warn|error" build/jiggle-body.log
```

Expect a log line `auto-rigged in NN ms: ... skin points` (about 25 ms) and no warnings or errors.

## 3. Screenshots on the real GPU (look at these)

Run the demo windowed, Body scene. Check:
- she jogs, sprints and idles on the UAL clips, arms at the right height;
- breasts and buttocks jiggle when she moves, not at rest;
- Hair choice rows (box braids, afro, locs, wavy, straight, long): hair stays outside her back,
  chest and shoulders;
- side and front view: lips closed, no teeth through the lips, no grey shells over the eyes.

Useful env vars: `KKE_JIGGLE_HAIR="box braids"`, `KKE_JIGGLE_VIEW=yaw,pitch,distance`
(-1.57 = her front with Side view on), `KKE_JIGGLE_TRACE=1` (logs the peak swing).
