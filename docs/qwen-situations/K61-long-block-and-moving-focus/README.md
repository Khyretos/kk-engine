# K61: a 60-line dictated REPLACE, and a focus on the line the edit changes

Job `g09-cbegin` (ENG-G09): replace the whole `beginFrame` of
engine/src/RmlVulkanRenderInterface.cpp (about 60 lines dictated as SEARCH,
75 as REPLACE). Attempt 1 copied only the first 6 lines of each block (the
signature change), so the check listed the missing body lines. Its `focus`
was the old signature line, which attempt 1 had just changed, so both repair
rounds got an empty excerpt (`// ...`) and answered without edit blocks
(asked for the file, guessed SEARCH lines with tabs).

Rerun: `job.json` with the pipeline at the same commit; the fixed spec splits
the edit into `csig` (signature only, focus on an unchanged line) and `cbody`
(the 10-line tail), each focused on a line no edit changes.
