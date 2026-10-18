K84 (f02c, 2026-10-18): one block in shaders/lighting_ubo.glsl adding `vec4 behind;` as the uniform block's last member. The
dictated SEARCH started at `} lighting;`. The Coder searched one line higher and put the new line before `vec4 refl;`. The check
then named the old `} lighting;` as out of place (K79 only looked for new lines after it) and said to move it; the Coder changed
nothing twice. Failed after 3 attempts.
Rule: a SEARCH that starts with a line closing a block (`} lighting;`, `};`) is lifted to the code line above, as K83 does for a
lone `}` (diffjobs `lift_brace`); and when the out-of-place line is old, the check also looks before it for the new line to name.
