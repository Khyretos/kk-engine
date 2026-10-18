K101 (r1b, 2026-10-18): docs/plans/rhi.md's old "R1b, the graphics path moved" bullet was to become a new "R1b (done)"
bullet plus an "R1c" bullet. The dictated SEARCH ended mid-bullet, after "...DebugDraw, Grid, Ocean,", because the
patch's context stopped there. In three runs the Coder put the bullet's last two lines ("the RmlUi and ImGui renderers.
A module's render() takes a CommandList / instead of a VkCommandBuffer...") into its SEARCH, finishing the item, but not
into its REPLACE, so they were deleted. Run 1 also had the new bullet dictated higher up (moved to the old bullet's
place); run 2 had the change split in two edits; run 3 had it in place. Every attempt failed the check.
Rule: a Markdown block is extended down to its paragraph's or item's last line (diffjobs para_end), as K67 does upward.
This folder holds run 1's job and calls.
