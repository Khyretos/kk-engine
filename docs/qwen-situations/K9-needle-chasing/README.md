# Lesson K9: a repair round satisfies a needle by breaking the logic

Situation: task ENG-G03, job `g03-surf` (2026-10-13): write
`ModelModule::surfaceSetFor`. Step 5 said that when the descriptor-set
allocation fails, it should log a warning and use `set = m_defaultSurface;`.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`): the draft wrote
`return m_defaultSurface;` in that branch, which is also correct. The
check wanted the line `set = m_defaultSurface;`, so the repair round
added `else { set = m_defaultSurface; }`. After that, every successful
allocation was thrown away and the default set was overwritten (with a
null set on the first call). It compiled, and the check passed.

Corrected: Claude removed the `else` branch (the file in this PR).

Rerun: `pipeline.py job.json` on `input.cpp`; pass = only the failed
allocation falls back to the default set. The check now matches the
branch's shape (regex over the if block, `m_defaultSurface` used once),
not one line.
