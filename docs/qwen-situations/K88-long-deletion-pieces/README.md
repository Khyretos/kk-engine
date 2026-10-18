K88 (f02t, 2026-10-18): one block in shaders/reflect.glsl replacing the 80 lines of ray code (moved to ray_trace.glsl) with an
include and a 3-line wrapper. The dictated SEARCH held all 80 lines. The Coder's SEARCH never matched them all; repairs moved the
new `#define` lines around. Failed after 3 attempts.
Rule: a removed run longer than 12 lines is split into pieces of 12 removed lines, each its own block applied in order; the first
keeps the context before, the last gets the added lines and the context after (diffjobs `split_deletions`).
