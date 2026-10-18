K77 (f01, 2026-10-17): one job of three blocks in engine/src/Renderer.cpp that moves beginOverlayPass's first half (end the
scene pass, the barrier) into a new endScenePass and makes beginOverlayPass call it. Attempt 1 kept the old single barrier's
last four lines (`b.image = ...`, `vkCmdPipelineBarrier(...)`) inside the new function and again at the old place, and added an
invented two-line comment ("the acquire semaphore is already set by the presentation engine") in beginRenderPass. The repair
rounds chased the first error (move a line) and never reached the stray lines. Failed after 3 attempts; Claude removed the 10
lines (the rest matched the reference).
Rule: K74 again. A job that moves a function's body splits into one job per block (the moved code; the new caller), and a
repair round lists every error. diffjobs now gives a block that takes out 4 or more lines a job of its own.
