# Lesson: climbing feet, lunge and no clipping (kk-engine, Climb Race)

Topic: changing game logic that a CPU player (bot) and many tests depend on.
Repo: kk-engine. Files: `engine/src/Climber.cpp`, `engine/include/kke/Climber.h`, `engine/src/ClimbWall.cpp`, `tests/test_climb_wall.cpp`, `games/climb_race/*.cpp`.

## What was asked

Kees's climbing feedback: feet on bumpers with marks where they can go; stamina back only with feet planted (arms alone cost double); jump held is a lunge you must catch in time; triggers are the hands; two hands on one hold side by side (no shaking); no knee, head or body through rock or ledges; no dislocated free arm.

## What went wrong and what fixed it

1. **The bot stopped reaching the top after the rules changed.** Every new rule (body can't pass a ledge, stamina only with feet) broke the bot in a new place. Fix: change the rule, then run the bot tests at once, then fix the bot. Never change many rules before running tests.
2. **The bot ran out of stamina on long sideways climbs.** Cause: too few footholds, so its feet were rarely planted. Fix: small feet-only holds ("foot chips", `ClimbHold::Kind::Foot`) generated *after* all other holds, so the rest of each mountain stays the same. Every place that picks holds for hands had to skip them (`usable`, `catchTarget`, `start`, the bot's search loops, the route builder). Use `grep -n "Kind::" engine/src/*.cpp` to find them all.
3. **A spacing test failed by 4 mm.** Cause: the generator compared distances with the point *before* it was moved out along the rock's normal; the test compared the final points. Fix: build the hold first, then compare its final position.
4. **The bot almost never lunged.** Two causes. (a) It only lunged if the "best" hold at the top of the jump was exactly the one it planned; with many holds that rarely happens. Fix: a new function `canCatch(hand, hold, hips)` that checks one hold, and the climber honours a hold picked by the bot (or the mouse) if it is in reach. (b) In the air the bot thought it was already falling on the first frame (it compared with the apex instead of the highest point reached), grabbed the nearest hold and the lunge ended at once. Fix: remember the highest hip height in the jump; "falling" means below that.
5. **The bot rested at every stance.** With feet planted it was almost always recovering, and it rested whenever recovering and below 90%. Fix: start resting only below `restAt` (70%), then rest up to `restUntil`. This is called hysteresis: one threshold to start, a higher one to stop.
6. **Debug code.** Temporary `fprintf` and a `ZZDebug` test helped find causes 4 and 5. They must be removed before the commit: `grep -n "getenv(\"KKE_DBG\|ZZDebug" engine tests` must print nothing.

## Exact commands that were run (all verified in the cloud container)

```sh
cd /home/user/kk-engine/build
ninja kke_tests                                   # build the tests only
./bin/kke_tests --gtest_filter='Climb*'           # 32 climbing tests
./bin/kke_tests                                   # everything: 996 passed
ninja climb_race                                  # the game, -Werror
python3 ../tools/ci/check_std_includes.py         # "Every std:: name used has its header included."
```

clang syntax check of changed files: read `build/compile_commands.json`, replace the compiler with `clang++`, drop `-o/-MF/-MT/-MD/-c`, add `-fsyntax-only -Werror -Wall -Wextra`. All six files printed OK.

Patch for the PC that runs heavy checks:

```sh
git fetch forgejo main
git checkout -b climb/hands-feet-lunge forgejo/main
git checkout claude/project-thread-fq2q3h -- .    # whole result as one commit (no files were deleted)
git commit -F msg.txt
git format-patch -1 --stdout > /mnt/project-files/kk-engine/patches/climbing-hands.patch
git checkout -q forgejo/main && git apply --check <patch> && echo APPLIES
```

`git checkout <branch> -- .` does not delete files. Run `git diff --name-status forgejo/main HEAD | grep -v ^M` first; if it prints `D` lines, delete those files by hand.

## Rules worth keeping

- Change one rule, run the tests, then the next rule.
- When the bot fails, print what it sees (its plan, the climber's state) every frame for one seed. Do not guess.
- A test number that you made up (like "more than 2 lunges per climb") can be wrong; make it measure what matters ("lunges on at least 30 of 40 mountains").
- Never push to GitHub; Forgejo PRs only. Heavy runs (window, screenshots) go to soucouyant with a checks list.

## Could a 9B model do this alone?

No, not the bot tuning: finding causes 4 and 5 needed reading several hundred lines of geometry and adding the right debug prints. A 9B model can do the parts with clear steps: the control remap (replace action names, add bindings), the docs updates, building and running the tests, writing the patch with the commands above, and running the soucouyant checks list. Give it one file and one change at a time, and the exact test command to run after.
