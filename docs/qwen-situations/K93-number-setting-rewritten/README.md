K93 (f10b, 2026-10-18): a block in engine/src/Renderer.cpp that changed the post pass's binding list started with an unchanged
setting, `si.maxLod = 0.0f;`. The Coder rewrote it to `si.maxLod = VK_LOD_CLAMP_NONE;` on every attempt, as if fixing it, and
the repairs never put it back. Failed after 3 attempts.
Rule: unchanged lines at a block's edge that set something to a plain number are dropped from the SEARCH while what is left
is still unique (diffjobs `trim_values`).
