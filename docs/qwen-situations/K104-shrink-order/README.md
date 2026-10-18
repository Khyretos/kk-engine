K104 (r1c, 2026-10-19): unit r1c-particleef1x replaced three lines (`VkBuffer vb = ...`, `VkDeviceSize off = 0;`,
`ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(vb), off);`) with one (`ctx.list->bindVertexBuffer(0,
slot.buffer->ref(), 0);`), which stands right after the unchanged `ctx.list->bindGroups(...)`. The Coder put the one
line above bindGroups instead, and three attempts could not move it down although the check named the line to move.
Rule: when an edit shrinks lines to fewer, the prompt says which line the new one comes right after (diffjobs K104,
as K95 does for a growing change). Rerun: see the R1c row.
