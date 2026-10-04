# Lesson: goblin horde rework (2026-10-04)

Role: worker (C++ in `games/goblin_horde`, a little engine animation and ragdoll code). Shared notes at the end.
Could a 9B local model do this alone? No: it was 5000 lines over 10 files. It could do the parts that are
data (a new goblin type in `foes.yml`, a weapon in `heroes.yml`, a wave in `waves.yml`) and the README,
given lessons 1, 3 and 6 verbatim. Split such work into one file per task for a 9B model.

## The task

Kees: goblins invisible; one attack swing; no goblin variety; no inventory, no character select;
bows and crossbows; bosses with telegraphed attacks; split screen, online and both; no pause menu.

## Lessons

1. **"Invisible" was three different things.** Check each before fixing:
   - the code loaded a pack that wasn't there (SIDEKICK goblins), so nothing was drawn;
   - after switching to the Goblin War Camp pack the goblins were there but in a T-pose;
   - their ragdolls failed with "no Pelvis".
   Tool for this: `KKE_HORDE_LINEUP=1` (every type in a row) and `KKE_HORDE_POSE=Idle_Loop` (every goblin plays
   one clip). A screenshot shows at once which skeleton doesn't take the clip.
2. **T-pose = bone names don't match.** War Camp uses Synty's older names (`Shoulder_L` is the upper arm,
   `Elbow_L` the forearm, `UpperLeg_L`, `LowerLeg_L`, `Ankle_L`, `Neck`). UAL clips use `upperarm_l`,
   `lowerarm_l`, `thigh_l`, `calf_l`, `foot_l`, `neck_01`. Fix in the engine, once: aliases in
   `kke::canonicalBoneName` (engine/src/AnimRig.cpp), plus a test. List a model's bones with
   `build/bin/kke_model_info FILE.fbx`.
3. **Fix the lookup where it's used, not in a shared helper.** Putting the alias fallback into `findBoneCI`
   broke the quadruped ragdoll test. A new `findHumanBone` (exact name, then canonical) used only by
   `buildHumanoidRagdoll` fixed the goblins and kept the test green. Run the whole test file after such a change.
4. **The repo's UAL1 is a subset.** `assets/animations/UAL1_Standard.fbx` has 43 clips; the pack's
   `Universal Animation Library/Unity/UAL1.fbx` has 120 (Kick, Spell_*, strafes). Find it with
   `kke::findPackFile("Universal Animation Library", "UAL1.fbx", exe)` and add with
   `kke::appendClipsByBoneName`. Log missing clips as info: the pack is optional.
5. **Props in a hand: get the axes right, then screenshot.** In `grip()` (Art.cpp) `y` is the prop's long axis.
   A bow held to shoot stands up: `y = forward` of the hand, so it is vertical when the arm points forward.
   No pack has a crossbow: a short bow laid flat (`y = up`, across the arm) on a stock box drawn in code.
6. **Data first.** Each goblin type is a YAML entry: models (pack, file, mesh part), weapon, `keep` distance,
   and three `moves` (kind melee/dash/shot/lob/blast/slam/combo/buff/heal, clip, range, cooldown, damage...).
   A goblin picks the first ready move in range. Telegraphs are ground marks that fill until the blow.
7. **A bot that plays for minutes finds the bugs a 20 s run doesn't.** With `KKE_HORDE_BOT=1 KKE_HORDE_QUIT=150`
   the report showed "2 alive" for 70 s. A temporary per-foe log (position, tactic) showed broken goblins
   pressed against the wall: "flee" steers straight away from the player. Fix: route through the nearest
   gateway (`wayTo` in Foes.cpp). Then the hero was in Knockdown at every report: arrows call
   `Combatant::receive` directly, which knocks a downed fighter down again (melee skips downed fighters).
   Fix: shots skip Knockdown, and a player just up gets 1.2 s grace. Delete the debug lines
   (`grep -rn TMPDBG games/`).
8. **Online: the host owns the horde.** Host sends wave state and each goblin (15 Hz); clients send their
   hero and report their hits on goblins; the host applies them; a goblin's hit on a remote player goes to
   that player's screen (`sendEventTo`), which decides block or parry. Test on one machine with two processes.
9. **Auto mode refuses `git checkout --theirs` and `git stash drop`.** Resolve a conflict by editing the file
   (keep both sides' intent), `git add` it, and leave the stash entry alone.

## Verified commands (cloud container, Ubuntu 24.04, 4 cores, lavapipe)

```
ninja -C build goblin_horde kke_tests
export KKE_ASSETS_DIR=/mnt/project-files/kk-engine/assets-cache KKE_SKIP_INTRO=1
KKE_HORDE_LINEUP=1 tools/check_game goblin_horde --seconds 22 --headless --shot lineup.jpg
KKE_HORDE_BOT=1 KKE_HORDE_WEAPON=bow tools/check_game goblin_horde --seconds 18 --headless --shot bow.jpg
cd build/bin && KKE_HORDE_BOT=1 KKE_HORDE_QUIT=150 timeout 190 xvfb-run -a ./goblin_horde > bot.log 2>&1; grep "t [0-9]* s:" bot.log
KKE_HORDE_BOT=1 KKE_NET=host KKE_HORDE_QUIT=60 xvfb-run -a ./goblin_horde > host.log 2>&1 &
sleep 8; KKE_HORDE_BOT=1 KKE_NET=join:127.0.0.1 KKE_HORDE_WEAPON=bow KKE_HORDE_QUIT=45 xvfb-run -a ./goblin_horde > client.log 2>&1
cd ../.. && build/bin/kke_tests --gtest_brief=1          # 992 passed, 23 skipped
python3 tools/ci/check_std_includes.py                    # "Every std:: name used has its header included."
```
Lavapipe needs about 12 s before the first frame with all the packs; ask `check_game` for 18 s or more.
`check_game` always waits the full `--seconds`, even when the game quit earlier: run the binary with
`timeout` to time a quit.

## Shared

- Kees's "jittery movement like the climbing demo" meant goblins snapping between positions; smooth
  velocity toward the wanted one (`v += (want - v) * min(1, 10 * dt)`), never set positions directly.
- `git format-patch -1 --stdout > X.patch`, then `git apply --check` it in a fresh `git worktree` on the base.
  Check whether other queued patches touch the same files (here `KnownPacks.cpp` and `docs/ASSETS.md`).
