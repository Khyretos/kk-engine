K75 (clothpass, 2026-10-17): a job of three one-line additions in engine/src/ClothSystem.cpp (count the cloths the pass works on).
Edit 1 said to add `++m_passes;` after `load(c, *body);`; attempt 1 put it one line earlier, before `load`, and made the other two
edits right. The check said the block "is not in the file as one piece"; the repair rounds didn't move the line but rewrote the
condition above it (`if (!body || !body->IsActive()) continue;` split into two `if`s), then put it back. Failed after 3 attempts;
Claude moved the line below `load` (the rest matched the reference).
Rule: when a block is one added line, the error says which line it goes after ("put `++m_passes;` right after `load(c, *body);`")
instead of the generic "not in one piece", so the repair moves that line and leaves its neighbours alone.
