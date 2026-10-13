# Lesson K38: An extra condition the spec never asked for

Situation: ENG-G07 job `g07-slc` (2026-10-13): write ScriptLights::add;
the spec said "skip i when ... an entry of m_entries has slot i".

Wrong output: the Coder skipped only entries of the SAME owner
(`e.owner == owner && e.slot == slot`), so another script could take a
light a script had switched off (wrong-output.gz). Claude's tests only
used one owner, so the unit passed.

Corrected: Claude added the test AnotherScriptNeverTakesASwitchedOffLight;
the Coder fixed the line from the failing test in one repair run
(job g07-slcfix).

Rule: for every "any X" in a spec, the tests include a case where the X
belongs to someone else. Prompts say "whoever its owner is" when it matters.

Rerun: pass = the g07-slc job passes the full ScriptLights tests on run 1.
