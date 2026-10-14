K67 (g17, 2026-10-14): a docs job with two dictated blocks in wrapped Markdown prose. The first SEARCH block's first line begins in the
middle of a sentence ("(the min-spec reference) run at 1x. With MSAA the 3D pass renders into a"); attempt 1 cut it at the sentence
boundary and searched from "With MSAA ...", which is not a whole line, so the patch did not apply. The repair rounds then mixed the two
blocks (block 2's REPLACE got block 1's text). Failed after 3 attempts; Claude applied the dictated blocks.
Rule: for Markdown, diffjobs tells the model the lines are wrapped prose and every SEARCH line is copied whole, even when it starts
mid-sentence. Rerun: the same job with that note.
Rerun 1 with the "copy every SEARCH line whole" note: failed the same way (3 attempts). Rerun 2 with the block extended up to its
paragraph's first line (diffjobs.para_start, at most 6 lines): passed on attempt 1, identical to the reference.
