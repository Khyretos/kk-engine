# Lesson K42: A repair drops two lines, and the recipe check reads a stale copy

Situation: ENG-G07 job `g07-recipefix` (2026-10-13): four review points on
docs/cookbook/recipes/lamps.lua (add the pillar, log the reason light.point
returns, drop an unused line and two needless `if`s).

Wrong output: the pillar and the reasons were right, but the ring loop lost
`ring[i] = id` and its `end`, and the old log line stayed next to the new one
(wrong-output.gz). The draft before it had skipped the pillar entirely.

Why it passed: test_cookbook embeds recipes at CMake configure time, so
rebuilding kke_tests compiled the FIRST version of the file. Reconfigured,
the broken one fails EveryLuaRecipeCompiles.

Corrected: Claude restored the two lines and removed the duplicate;
chk_g07.py reconfigures before testing a .lua unit.

Rule: a check must test the file as the model left it: know what is
embedded at configure time and re-run configure.

Rerun: pass = chk_g07.py recipefix (with reconfigure) prints ok on run 1.
