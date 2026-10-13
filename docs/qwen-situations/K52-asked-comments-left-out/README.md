# Lesson K52: asked comments left out when the code was dictated

Situation: ENG-G06 job `g06-math` (2026-10-13): a new file with three free
functions, every statement dictated, and "one short `//` line above each
function" asked in the same prompt.

Wrong output: every statement was right, but no function had its comment and
the anonymous namespace lost its `// namespace` closer (wrong-output.gz).
The check had no needle for comments, so it passed on run 1.

Corrected: Claude wrote the three comments.

Rule: a comment asked for is checked like code: chk_g06 `commented` needs a
`//` line directly above each named definition.

Rerun: pass = g06-math writes the three comments on run 1 (with the
`commented` check on).
