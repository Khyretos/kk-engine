# K62: a dictated SEARCH block that ends in half a line

Job `g09-cgeom` (ENG-G09): three dictated blocks in RenderGeometry. Block 3's
SEARCH ended with `void RmlVulkanRenderInterface::ReleaseGeometry(`, the start
of a line (Claude's spec cut the function signature short). The pipeline
matches SEARCH blocks as whole lines, so attempt 1's answer, a correct copy of
all three blocks, failed to apply ("SEARCH text is missing"). The repair
rounds then edited other places that looked alike (RenderToClipMask's
`writer->layout()` lines), with a `texIt` that does not exist there.

Rerun: `job.json`; the fixed spec ends block 3 on the whole signature line,
and the job generator now refuses any dictated block that is not whole lines.
