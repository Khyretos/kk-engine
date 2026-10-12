# Lesson K6: Coder writes each Markdown bullet as one long line, and repairs don't wrap it

Situation: task ENG-B075, jobs `b075-doc-trunk` and `b075-doc-scan`
(2026-10-13): replace one three-line bullet each in
`games/command_kit/README.md`, "lines under 80 characters".

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, 3 attempts each): the text
was right, but each bullet came back as one line of 190 to 200
characters, and the repair rounds, fed "keep lines under 80 characters",
answered with the same long line. Claude's own part: the second job's
check read the whole file's diff, so it also failed on the first job's
line (lesson K1 again).

Corrected: Claude wrapped both bullets at 72 columns with a two-space
continuation indent (`textwrap.fill`), the words unchanged (the file in
this PR).

Rerun: `pipeline.py job.json` from main before this PR; pass = both
bullets wrapped under 80 characters on attempt 1. Until then, wrap Coder
Markdown by script after the job.
