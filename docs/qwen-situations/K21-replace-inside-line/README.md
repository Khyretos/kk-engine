# Lesson K21: "replace the final `+ 16`" became an extra term

Situation: ENG-G05, job `g05-lbcpp` (2026-10-13): the lighting UBO's C++
mirror gains two arrays; "in the static_assert, replace the final `+ 16`
with `+ 2 * ShadowMap::kCascades * 16`".

Wrong output (3 attempts, all failed): the draft kept `+ 16` and appended
`+ ShadowMap::kCascades * 2 * 16 + 2 * ShadowMap::kCascades * 16`, and the
static_assert failed. Both repair rounds returned edits that left it as it
was.

Corrected: Claude wrote the expression (the file in this PR).

Rule: never ask for a replacement inside a line; dictate the whole new
line. Rerun: pass on attempt 1.
