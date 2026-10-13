# Lesson K5: a new checker script that reads the wrong layout and mixes up its names

Situation: task ENG-B076, job `b076-checker` (2026-10-13): write
`tools/ci/check_game_modules.py`, which compares each
`games/<game>/game.json` "modules" list with the module names the game's
`.cpp` files add. The prompt gave the regexes and the algorithm step by
step.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, 174 lines, failed after 3
attempts): it looked for `games/*.json` instead of
`games/<game>/game.json`, so it checked nothing and printed "ok: 0
game.json files match"; it compared class names with module names; it
swapped "missing" and "extra". Both repair rounds answered with a
one-line patch that changed nothing. Claude's own part: the job's check
expected `Showcase` for rmlui_demo, whose own `ShowcaseModule` is named
`UiShowcase` (two games have a class called `ShowcaseModule`).

Corrected: Claude wrote the script (93 lines, the file in this PR); a
game's own classes win over another game's class of the same name.

Rerun: `pipeline.py job.json` from main before this PR with the check in
the PR's `docs/qwen-log` notes; pass = the check passes on attempt 1.
