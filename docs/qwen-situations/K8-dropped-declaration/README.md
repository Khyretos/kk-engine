# Lesson K8: Coder drops the declaration line of a dictated code block

Situation: task ENG-G03, job `g03-frag` (2026-10-13): add normal, roughness
and metallic maps to `shaders/model.frag`. Step 3 of the prompt gave a
six-line GLSL block to insert, starting with
`vec3 N = normalize(fragNormalWorld);`.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, 3 attempts): every line
of the block was there except the first one. The next line passed
`fragNormalWorld` where the block said `N`, and the later uses of `N`
did not compile. Claude's own part: the check had no needle for the
declaration, so its error named the call (`perturbNormal(N, ...)`) and
not the missing line, and both repair rounds left it as it was. Lesson 75
also says literal code is pasted by Claude, not sent as a job.

Corrected: Claude added the declaration and changed the call back to `N`
(the file in this PR).

Rerun: `pipeline.py job.json` on `input.frag`; pass = the block is
complete on attempt 1.
