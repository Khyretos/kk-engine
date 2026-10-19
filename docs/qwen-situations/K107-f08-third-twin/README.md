K107 (F08, 2026-10-17, filed 2026-10-19 in the report audit): unit f08-scriptligh2 dictated the many-lights condition in
ScriptLights::remove and releaseAll. The file has a third look-alike condition in ScriptLights::clear, which was the next
unit's. The Coder rewrote all three in one block; its two repairs then kept a copy of the new condition and lost one of
the old ones, so the check failed after 3 attempts and Claude applied the dictation by hand.
Rule: K102's (R1b, 2026-10-18): neighbouring units in a file that change the same kind of line are one unit, so the
twin is dictated, not guessed. wrong-output holds the three answers.
