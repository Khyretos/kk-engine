# Lesson K35: Needles on a rewritten loop pass while the old loop stays

Situation: ENG-G07 job `g07-appb` (2026-10-13): replace the point-face
loop in Application::run with two loops by slot. The prompt DESCRIBED the
new loops (K25 says dictate them); the check had needles and two "gone"
lines but no build.

Wrong output: the Coder wove the new lines into the old loop, with tabs
(wrong-output.gz). Every needle matched, so the unit passed; the next
unit's build found it.

Corrected: Claude restored the file and re-ran the unit with the whole
REPLACE block dictated and a check that builds Application.cpp.o, counts
each key line once and rejects tab indents.

Rule: a unit that replaces a block always builds, and every new key line
must appear exactly once; needles alone can't tell "replaced" from "mixed in".

Rerun: pass = chk_g07.py appb prints ok (build included).

Also: the unit before it, `g07-appa`, rewrote this same loop although
its SEARCH block was only the five lines above it (its check had no
"gone" line for the loop, so it passed). Every unit's check now names
the old lines that must stay or go.
