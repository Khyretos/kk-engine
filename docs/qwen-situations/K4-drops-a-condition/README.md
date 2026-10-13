# Lesson K4: Coder drops a condition from the line it was told only to re-point

Situation: task ENG-B074, job `b074-climb` (2026-10-13): in
`games/climb_race/ClimbRaceModule.cpp`, replace `std::getenv` with
`kke::dev::env` in `envFloat()`, `envOn()` and one more call, and change
nothing else.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, passed its check on
attempt 1): in `envFloat()` it also rewrote the next line from
`return v && *v ? ... : fallback;` to `return v ? ... : fallback;`, so an
empty variable (`KKE_CLIMB_SEED=`) gave 0 instead of the default. The check
did not cover empty values. `wrong-output.gz` is the diff.

Corrected: Claude put `&& *v` back (the file in this PR).

Rerun: `pipeline.py job.json` from main before this PR; pass = only the
`std::getenv` calls change.
