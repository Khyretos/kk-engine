# Lesson K31: A one-line insert "followed by" still lands above the anchor

Situation: ENG-G07 job `g07-alg` (2026-10-13): add `#include <algorithm>`
after `#include <glm/glm.hpp>` in engine/include/kke/Application.h. The
prompt said "your REPLACE block is those same lines, unchanged and first,
followed by the one new line".

Wrong output: the Coder put the new include ABOVE the anchor line
(wrong-output.gz). Both fix rounds then ran out of tokens (K32).

Corrected: Claude moved the line under the anchor.

Rule: K25 holds for a single line too: never describe an insert, write the
whole REPLACE block (anchor first, new line after) in the prompt.
g07.py's after() now does that for every "one new line" insert.

Rerun: pass = chk_g07.py alg prints ok.
