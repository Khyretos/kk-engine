K96 (f10b, 2026-10-18): one new line, `post.lookPower.a = ...`, added above a one-line SEARCH (`post.tone.z = ...`; K80 trimmed
the long lines around it). The Coder widened the SEARCH up to `PostParams post = m_post;` and put the new line right after it,
one line too high. Given the exact edit, the repairs copied `post.tone.w` a second time and then removed the copy, but never moved
the new line. Failed after 3 attempts.
Rule: when new lines go above a SEARCH, the prompt names the file's line above it too: the new lines go between the two (diffjobs).
