# Lesson K22: an append after a 1,500-character row, and a repair that wrote the error into the file

Situation: ENG-G05, job `g05-bug` (2026-10-13): add one BUGS.md row
(BUG-089) "directly after the row that starts `| BUG-088 |`". That row is
about 1,500 characters long.

Wrong output (3 attempts, all failed): attempt 1's SEARCH block retyped
the whole BUG-088 row, got it slightly wrong and did not apply, twice.
The repair rounds had only the check's error to go on; two answered in
prose about an unrelated row, and the last turned the check's error
message itself into a BUG-089 row ("The check ... fails after your
change to BUGS.md").

Corrected: Claude wrote the row from the facts in the prompt (the file in
this PR).

Rule: never anchor on a long line. Append table rows by script after a
short anchor, or have the Coder write only the new row to a scratch file
that a script inserts. Rerun: pass = a BUG-089 row with every fact.
