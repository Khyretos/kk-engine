K72 (g18a, 2026-10-14): a new 40-line file dictated whole (engine/src/ScriptFx.cpp). Attempt 1 added a helper of its own,
`ScriptFx::forgetOwner`, to share the erase-by-owner line between two functions; the header has no such member, so the file did not
compile. The repair rounds rewrote releaseAll around it (the SEARCH of the rewritten function no longer matched) and never removed the
helper. Failed after 3 attempts with that one function too many; Claude removed it, and the rest is identical to the reference.
Rule: a dictated new file's prompt says "add no function, member or helper of your own"; the compile error of an undeclared member
names the function to delete, and the check now prints that hint.
