K106 (r1d, 2026-10-19): R1d deletes long runs of hand-made descriptor-pool code. K88 splits a run into 12-line pieces;
a piece in the middle has an empty REPLACE. Every applier (tools/qwen/pipeline.py apply_patch, the dry run dry09, the
check's clean run clean09) replaced the SEARCH text with nothing but kept its newline, so a blank line stayed behind;
the new blank-line count (K103) failed the dictation itself in the dry run.
Two workarounds in diffjobs were tried first and made it worse: each piece anchored on the lines before the run
(SEARCH blocks starting `}` `}` that the Coder could not place), then all pieces as one unit (the Coder deleted three
quarters of a run, then the shadow-map set's lines in its repairs). Nine R1d units failed under them; Claude applied
those as dictated.
Rule: an empty REPLACE deletes whole lines, newline too, in all three appliers and the blank count; K88's pieces are
back, and a dry run of R1d with them equals the reference. This folder holds r1d-applicatio2's job and calls.
