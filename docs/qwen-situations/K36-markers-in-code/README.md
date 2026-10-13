# Lesson K36: Code that contains the patch markers can't go through patch mode

Situation: ENG-G07 job `g07-pipefix2` (2026-10-13), first version: the
dictated REPLACE text for pipeline.py contained the literal string
`>>>>>>> REPLACE`.

Wrong output: the patch parser took that string inside the new code as the
block's end, so the file got half a line and a syntax error
(wrong-output.gz; all three runs).

Corrected: the job was re-dictated with the markers built from pieces
(`">" * 7 + " REPLACE"`); it passed on run 1.

Rule: when the new code itself holds `<<<<<<< SEARCH`, `=======` or
`>>>>>>> REPLACE`, build those strings from pieces in the dictated code.

Rerun: pass = the first-version prompt (calls.json.gz) applies cleanly.
