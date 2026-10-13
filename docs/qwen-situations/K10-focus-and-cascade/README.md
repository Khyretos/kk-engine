# Lesson K10: edited lines outside `focus`, and dependent jobs in one call

Situation: task ENG-G03, jobs `g03-pixels`, `g03-load`, `g03-applin` and
`g03-mips` (2026-10-13), sent in one pipeline call. Three of them change
`engine/src/Texture.cpp` or need it to compile. The `g03-pixels` focus
(`createFromPixels` and `createImage`) left out the `downsampleRgba8Srgb`
line the job had to change.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`): the Coder could not see
the line, so it copied the excerpt marker `// ...` into its SEARCH and
REPLACE blocks. Its repair rounds broke `Texture.cpp`, which then left
conflict markers there through `g03-load`. Because all four jobs ran in
one call, the `g03-applin` and `g03-mips` checks failed on that file's
errors, and their repair rounds changed code that had nothing wrong with
it.

Corrected: Claude restored `Texture.cpp`, added the edited lines to the
focus, and ran `g03-pixels` and `g03-load` as two separate calls. Both
passed on attempt 1. The `g03-mips` draft was right all along, and
`g03-applin` needed one line (lesson K11).

Rerun: `pipeline.py job.json` with the narrow focus; pass = no `// ...`
in any block. The rule is Claude's: every line a job edits must match a
focus regex, and dependent jobs go in separate calls.
