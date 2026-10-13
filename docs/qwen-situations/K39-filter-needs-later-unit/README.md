# Lesson K39 (Claude): a unit's test filter ran a test that needs a later unit

Situation: ENG-G07 unit `g07-cm` (2026-10-13) added src/ScriptLights.cpp
to the engine; its check ran `ScriptLights.*`, which includes
FullThenFreedByRemove, a test that only holds once kMaxLights is 16
(unit `g07-max`, later).

Wrong output: the Coder's line was right on run 1, but the check failed,
so both repair rounds tried to "fix" CMakeLists.txt for a test it can't
affect (they changed nothing, luckily).

Corrected: the filter excludes that test until `g07-max`.

Rule: run a test filter on the hand-made final tree AND on the tree as it
will be after that unit; drop tests that need later units.

Rerun: not a model mistake.
