# Lesson K32: Fix rounds on a big file see the whole file and run away

Situation: ENG-G07 job `g07-alg#fix1`/`#fix2` (2026-10-13). The draft
used `focus` (25-line excerpts of engine/include/kke/Application.h), but
pipeline.py's fix rounds drop `focus`, so the repair prompt held the whole
header (8,799 prompt tokens).

Wrong output: both repair answers ran to the 2,500-token limit without
finishing ("answer cut off at the context limit"), so no repair applied.

Corrected: the fix rounds keep the job's `focus` (tools/qwen/pipeline.py
check_loop, job `g07-pipefix`).

Rule: a repair round shows the same excerpts as the draft; never the
whole of a file the draft only saw in part.

Rerun: pass = a fix round on Application.h answers in under 500 tokens.
