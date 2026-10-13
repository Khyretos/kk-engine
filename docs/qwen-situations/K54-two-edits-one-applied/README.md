# Lesson K54: two edits in one patch job, only the second applied

Situation: ENG-G06 job `g06-skyfield` (2026-10-13): Sky.h gets a forward
declaration `class EnvMap;` above the SkyEnvironment comment and a new member
inside the struct, both dictated as SEARCH/REPLACE blocks, numbered 1 and 2 in
one prompt. This broke an existing rule (one edit per patch job,
prompting-workers.md); Claude wrote the job that way.

Wrong output: one block, the member (edit 2). Both repair rounds, told that
the declaration was missing, added empty lines after the struct instead
(wrong-output.gz).

Corrected: Claude removed the empty lines and added the declaration; the
include in Sky.cpp then went out as its own job (g06-skyinc) and passed on
run 1.

Rule: one SEARCH/REPLACE edit per job, even for two one-line inserts in the
same file; a numbered "two edits" prompt is a red flag in review of the
job file.

Rerun: pass = g06-skyfield applies both blocks on run 1 (expected to fail
until the model handles two blocks; the split jobs are the fix).
