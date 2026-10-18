K87 (f02c, 2026-10-18): a job of three blocks in engine/src/LightingBuffer.cpp; the third adds `info[5] = m_behind && ... : none;`
before the write loop. Attempt 1 copied an excerpt's `// ...` marker into a SEARCH and wrote its own `if (m_behind ...) { info[5] = ...; }`.
The repair rounds saw only the error messages; the Coder said it did not have the file, guessed lines that were not there, and both
repair answers ran into the token limit. Failed after 3 attempts.
Rule: when a dictated block is not in the file as one piece, the check also prints what the file reads around the block's first
line that is still there (six lines either side), for the repair to copy its SEARCH from (chk_g09).
Seen again on `f02c-renderer4` before the excerpt picked a line found once: it showed the constructor's last `}` instead of the
block's place, and the repairs guessed again. The excerpt now centres on the first block line that is found exactly once.
