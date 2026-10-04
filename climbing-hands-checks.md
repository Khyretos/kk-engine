# Climbing hands: checks for soucouyant

Patch: `/mnt/project-files/kk-engine/patches/climbing-hands.patch` (one commit, `git format-patch`)
Base: Forgejo `main` at `93499963c87767a15dc7a8086da8270175a2ef4d` (checked: `git apply --check` is clean on it)
PR branch name: `climb/hands-feet-lunge`

## What was already checked in the cloud

- `kke_tests`: 996 of 996 pass, including all 32 `Climb*` tests (feet, lunge, two hands on a hold, no clipping, the CPU climber reaching the top on 40 mountains with and without stamina).
- `climb_race` and `kke_tests` build with GCC `-Wall -Wextra -Werror`, zero warnings.
- clang++ 18 `-fsyntax-only -Werror -Wall -Wextra` is clean on every changed .cpp.
- `tools/ci/check_std_includes.py` passes.
- Not run in the cloud: the game window, screenshots, Vulkan validation.

## 1. Apply and build

```sh
cd /media/development/Software/kk-engine
# FORGEJO = the remote that points at git.kreative-kompas.com (see `git remote -v`)
git fetch FORGEJO main && git checkout -B climb/hands-feet-lunge FORGEJO/main
git am /path/to/climbing-hands.patch
tools/runner/kkrun build        # expect: PASS build (zero warnings)
tools/runner/kkrun tests        # expect: PASS tests
```

## 2. Headless runs (no hands needed)

The CPU climbs, then the game quits and logs how far the hands sat from their holds:

```sh
KKE_SKIP_INTRO=1 KKE_CLIMB_MOUNTAIN=granite_tower KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_QUIT=110 \
  tools/check_game climb_race --seconds 115 --headless --log build/climb-autopilot.log
```

Expect:
- last line `OK`, no warnings in the output;
- `topped out` lines in the log for both climbers (you on autopilot, and the Hard rival);
- the grip report at the end: average under about 3 cm (it was 2.5 cm before).

Rockfall and Overhang Cove (steep, so lunges are likely):

```sh
KKE_SKIP_INTRO=1 KKE_CLIMB_MOUNTAIN=overhang_cove KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_QUIT=120 \
  tools/check_game climb_race --seconds 125 --headless
KKE_SKIP_INTRO=1 KKE_CLIMB_ROCKFALL=1 KKE_CLIMB_QUIT=16 tools/check_game climb_race --seconds 20 --headless
```

Expect `OK` on both.

## 3. Screenshots (look at each one)

```sh
KKE_SKIP_INTRO=1 KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_CLOSEUP=1.5 \
  tools/check_game climb_race --seconds 25 --shot build/climb-closeup-25s.jpg
KKE_SKIP_INTRO=1 KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_CLOSEUP=1.5 \
  tools/check_game climb_race --seconds 45 --shot build/climb-closeup-45s.jpg
KKE_SKIP_INTRO=1 KKE_CLIMB_LOBBY=0 KKE_CLIMB_CPUS=0 KKE_CLIMB_INTRO=1 \
  tools/check_game climb_race --seconds 6 --shot build/climb-howto.jpg
tools/runner/kkrun shots climb_race
```

In the close-ups, check:
- knees point out to the sides and **never go into the rock**;
- the head and chest are out of the rock; under a ledge the head is below it;
- two hands on one hold sit side by side (not on top of each other, no shaking between frames);
- a free hand hangs naturally (no stretched or dislocated shoulder);
- small flat cyan and magenta marks on footholds under the climber; small pale chips on the rock.

In the how-to shot: card 3 says triggers move a hand, bumpers step a foot, hold A (Space) to lunge; card 4 says keep both feet on.

## 4. Needs Kees with a controller (send him this list, don't run it for him)

- LT / RT move each hand to its lit hold; LB / RB step each foot onto its lit mark.
- Tap A on the rock: nothing happens (no floating hop).
- Hold A, aim with the stick, let go: the body jumps that way, the marker turns gold; pull the matching trigger in time to catch, or you fall.
- Both feet on and hanging still: the stamina bar refills slowly. Feet off: it drains about twice as fast.
- Both hands on one hold: they sit side by side and stay still; stamina refills a little faster.
- Keyboard: Q / E hands, Z / X feet, hold Space to lunge.

## Note for the PR

The generator no longer puts holds inside a ledge's stone, so every mountain's holds moved a little: saved ghosts (Time trial) from older builds won't line up with the new rock.
