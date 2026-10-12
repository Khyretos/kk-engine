# Lesson K1: Coder rewrote the wrong bullet, then its repair rounds were empty

Situation: task ENG-B068, job `b068-readme` (2026-10-12): in
`games/sandbox/README.md`, delete the known-issue bullet "Barrel reports as
Box" and, in the earlier bullet "The same asset in two blocks", replace the
pointer "see the note about Barrel below." with a sentence that the default
blocks never share a mesh.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, attempt 1): it replaced the
"Barrel reports as Box" bullet with a second copy of the "same asset in two
blocks" bullet, and left the earlier bullet and its stale pointer as they
were. Both repair rounds answered with no SEARCH/REPLACE blocks at all.
Claude's part: the job's check also tested BUGS.md, which the next job had
not edited yet, so the repair rounds were fed an error about another file.

Corrected: Claude removed the duplicate bullet and edited the earlier bullet
in place (the file in this PR).

Rerun: `pipeline.py job.json` from main before this PR; pass = exactly one
"same asset in two blocks" bullet, no "Barrel below", no "Barrel reports as
Box". Files: `job.json` (prompt), `calls.json.gz` (every prompt and answer),
`wrong-output.gz` (the file after attempt 3).
