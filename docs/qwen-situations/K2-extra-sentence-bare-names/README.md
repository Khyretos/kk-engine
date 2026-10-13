# Lesson K2: Coder adds a sentence nobody asked for, and leaves code names bare

Situation: task ENG-B068, job `b068-bugs` (2026-10-12): change only the
Status cell of the BUG-068 row in `BUGS.md` to "Fixed 2026-10-12: " plus one
sentence naming `engine/src/PlayBlocks.cpp` and the test.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`): the cell had the fix, then
an extra sentence "Documentation updated 2026-10-12: removed
barrel-from-crate note from games/sandbox/README.md." that the prompt did
not ask for, and the file, function and test names were not in backticks
like the rest of the table.

Corrected: Claude removed the extra sentence and put the names in
backticks (the BUGS.md row in this PR).

Rerun: `pipeline.py job.json` from main before this PR; pass = the cell is
exactly the asked sentence, with code names in backticks.
