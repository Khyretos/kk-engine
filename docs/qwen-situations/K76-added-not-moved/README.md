K76 (f01, 2026-10-17): one block in shaders/pbr_common.glsl that adds a two-line comment above `float NdotV` and the GI lines
below `envSpecular`. Attempt 1 put the comment two lines lower, next to the GI lines. The K75 error ("put `...` on the line
right after `...`") was followed literally: each repair round added a copy of one comment line in the right place and left the
first copy where it was, so the comment ended up twice. Failed after 3 attempts; Claude removed the two misplaced lines.
Rule: when the misplaced line is already in the file, the error says "move `X` (delete it where it is now) on the line right
after `Y`" (chk_g09 now does).
