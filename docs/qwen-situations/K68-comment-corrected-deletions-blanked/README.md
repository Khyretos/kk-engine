K68 (g17, 2026-10-14): three dictated blocks in shaders/lighting_ubo.glsl (a header comment changed, 4 UBO fields deleted, and
one `#include` deleted). Attempt 1 "corrected" the comment it had to copy, writing "(set 0, binding = 0)" for "(set 0, binding 0)" because the
layout line below says "binding = 0", so the SEARCH did not apply. The repair rounds then paraphrased the new 2-line comment into one
("// gradient sky, height fog.") and answered both pure deletions with a blank line where the removed lines were. Failed after 3
attempts; Claude applied the dictated blocks.
Rule: a block that only deletes lines is dictated with a one-line non-blank anchor kept after it, and its prompt says "delete these
lines; put nothing in their place"; a comment that mirrors nearby code is flagged as text to copy exactly (the K66 twin note).
Rerun with the K68 deletion note: passed on attempt 3 (attempt 1 again wrote "binding = 0" in the copied comment; the check's nearest-line hint fixed it).
