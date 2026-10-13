# Lesson K3: Coder puts a new struct field first and uses another comment style

Situation: task CI-5 (flaky `Hair.MotionGoesFromNaturalToSolid`), jobs
`hair-stats-field` and `hair-stats-iterations` (2026-10-12): add a field
`iterations` to `kke::HairStats` and sum it in `ClothSystem::hairStats`.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, both passed their checks
on attempt 1): the field went in as the FIRST member, which changes the
order of every brace initialiser of the struct; its comment used `///`
where the whole file uses `//`; the sum was a braced one-line `if` where
the file writes one-line `if`s without braces. `wrong-output.gz` is the
diff.

Corrected: Claude moved the field to the end, made the comment `//` and the
`if` one line (the files in this PR).

Rerun: `pipeline.py job.json` from main before this PR; pass = the field is
last, `//` comment, file style kept.
