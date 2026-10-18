K102 (r1b, 2026-10-18): unit r1b-particleef2 dictated drawGpu's draw calls moving onto ctx.list. drawBatch, 260 lines
further on, has the same `pipe.bind(ctx.cmd);` and vkCmd* lines, and those were unit 3's. The Coder converted drawGpu
right, then also converted drawBatch in a second block (wrongly: `ctx.list->vertexBuffers(vb, &off)`, which doesn't
exist). Its two repair attempts went back and forth on drawBatch and ended with `pipe.bind(ctx.cmd);` deleted there.
Rule: neighbouring units in a file that change the same line become one unit (up to 6 edits), so the twin is dictated;
a note saying a later step changes those was tried first and did not help (run 2). Rerun: see the R1b row.
