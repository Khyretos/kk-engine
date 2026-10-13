# Lesson K27: Asked comments left out

Situation: ENG-G04, job `g04-lbs` (2026-10-13): The job asked for a `//`
comment on each new struct member and above a new function.

Wrong output: The prompt asked for a `//` comment on each new struct
member (g04-lbs) and above a new function (g04-pfn); the Coder wrote the
code without them and the checks, which only tested the code, passed.
g04-pfn also put the brace on its own line, unlike the file.

Corrected: Claude added the comments and fixed the brace by hand.

Rule: When comments are wanted, dictate their text (K15) and make the
check require them; a check that does not test it means the Coder skips
it.

Rerun: pass = each member and the function carry a comment. more-pfn
holds the second unit.
