# Lesson K45: the spec read the file the job edits

Situation: BUG-087 job `b087-bug87` (2026-10-13). b087.py built the wanted
BUGS.md row from the row it found in the working copy, at import time, and
asserted that the new row differs from the old one.

Wrong output: none from the Coder; its first draft was right. chk_b087.py
imports b087.py after the edit, so the spec now found the new row, the assert
failed, and both repair rounds were fed a Python traceback instead of a check
result (wrong-output.gz holds that traceback).

Corrected: row() reads `git show HEAD:<path>`; the import-time asserts are
gone. The same check then passed on the Coder's first answer.

Rule: a job spec reads the committed file, never the file the job edits.

Rerun: pass = b087-bug87 is accepted on attempt 1 with the fixed b087.py.
