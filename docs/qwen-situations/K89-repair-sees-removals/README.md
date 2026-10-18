K89 (f02t, 2026-10-18): the second of K88's deletion pieces in shaders/reflect.glsl, 12 lines ending at a function's first line.
Attempt 1 deleted ten of them and kept the comment and the function's first line. The check said which two lines must still go,
but the repairs saw only that message: they asked to read the file (`cat`), said they could not see it, and gave no edit blocks.
Failed after 3 attempts.
Rule: an error about lines that must be removed, or lines that are there twice, also prints what the file reads around them, for
the repair to copy its SEARCH from (chk_g09, as K87 does for broken blocks).
K90, same unit, rerun: the check's excerpt never reached the Coder. The pipeline keeps an error up to its first blank line
(compiler style), and the excerpt began with one; and the fix round's file excerpt follows the draft's focus lines, which the bad
draft had deleted, so it was empty. Rule: a fix round's excerpt also centres on every line the errors quote (in backticks or as list items), and a deletion piece is focused on its first removed line (diffjobs)
(tools/qwen/pipeline.py `check_loop`; this copy is local to kk-wt-gaps, the pipeline's home is kreative-kompanion).
