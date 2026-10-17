K82 (f02k, 2026-10-18): a job of three blocks in engine/src/ReflectionPass.cpp: a new 66-line rayPipeline()/createRayPath()
after pipeline(), and two small edits in record(). Attempt 1 put the new functions before destroyImages and its third block
deleted `const bool ms = scene.depthSamples > 1;`. The repair hint "move `VkPipeline ReflectionPass::rayPipeline(...)`" made the
Coder move lines one at a time and rewrite the body from pipeline(), its near twin right above. Failed after 3 attempts; Claude
applied the dictated blocks.
Rule: a block that adds 20 or more lines is a job of its own (diffjobs `longadd`, like the code-moving blocks of K77), so its
repair round has only that block to put back.
Seen again in the same task on `f02k-reflection3` (a 57-line block in record(), the rule still off for f02k): the Coder said the
excerpt lacked the code and put the block before the wrong line. Claude applied the blocks.
And on `f02k-sphereimpo1x` (a 28-line block among two small ones): both repair answers were cut off at the job's token limit.
Claude applied the blocks.
