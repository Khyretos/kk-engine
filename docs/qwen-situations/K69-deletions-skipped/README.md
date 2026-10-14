K69 (g17, 2026-10-14): pure deletions skipped while the check passed. g17-swapchain1 kept `canBlitTo()` and its member (and wrote
`m_canBlitTo = true`), g17-thumbnailm3 kept the two tile-slot lines the render loop no longer needs. The check only looked for added
lines (and, since K65, for extra deletions), not for the dictated deletions; the final compare with Claude's reference found them.
Fix: chk_g09 now also fails when a dictated deleted line is still there, and when a dictated REPLACE block is not in the file as one
piece. ThumbnailModule.cpp: Claude removed the two lines. SwapChain.h: rerun with the new checks.
Rerun of g17-swapchain1 with the new checks: passed on attempt 1, identical to the reference.
