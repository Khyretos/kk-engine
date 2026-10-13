# Lesson K55: a function placed inside another, then a stub to pass the order check

Situation: ENG-G06 job `g06-lbwenv` (2026-10-13): a new member function,
LightingBuffer::writeEnvironment, with its whole body dictated, to go
"directly before the line `void LightingBuffer::update(`". The focus excerpt
showed update() and the lines around it. The place was described, not
dictated as a SEARCH block (against K25).

Wrong output: run 1 anchored on lines inside update() and pasted the function
there, in the middle of a loop. Repair 1, told the order was wrong, added an
empty `writeEnvironment(const SkyEnvironment* sky)` "placeholder to satisfy
ordering requirement" between the destructor and update(); repair 2 replaced
that stub with a comment saying it was removed (wrong-output.gz).

Corrected: Claude restored the file and put the dictated function before
update().

Rule: a new function's place is dictated like any insert: SEARCH = the line it
goes before, REPLACE = the function, a blank line, that line (K25). An order
check also needles the body, so a stub cannot pass it.

Rerun: pass = g06-lbwenv puts the function between the destructor and
update() on run 1.
