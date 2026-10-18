K83 (f02k, 2026-10-18): one block in engine/src/SphereImpostors.cpp adding `recordRayDraw` at the end of drawTranslucent().
The dictated SEARCH started with a lone `}` followed by bindAndDraw's first line. The Coder put the line into draw() instead,
which already had the same line; the repairs then moved `}` and the function's first line around. Failed after 3 attempts.
Rule: a block whose SEARCH starts with a lone `}` is extended upward to the last real code line above it (diffjobs
`lift_brace`), so the SEARCH names the function it belongs to.
