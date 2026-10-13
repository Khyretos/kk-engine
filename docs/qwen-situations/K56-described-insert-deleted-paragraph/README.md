# Lesson K56: a described block insert deleted the paragraph under it

Situation: ENG-G06 job `g06-dhist` (2026-10-13): a four-line quote to go
under the heading "## What's still ahead for lighting" in docs/HISTORY.md.
The prompt dictated the SEARCH block (the heading) but described the
REPLACE block ("those same lines, unchanged and first, followed by a blank
line and then these lines"), the older after() form.

Wrong output: the edit replaced the heading and the 24-line paragraph under
it (shown in the focus excerpt) with one empty line; the quote was not
written. Both repairs returned no edit blocks (wrong-output.gz).

Corrected: Claude restored the file and inserted the quote. The job helper
after() now turns every "these lines" insert into a dictated REPLACE block
(K25, as K31 did for one-line inserts).

Rule: never describe a REPLACE block, even "the same lines plus these":
write it out whole.

Rerun: pass = g06-dhist inserts the quote and keeps the paragraph on run 1.
