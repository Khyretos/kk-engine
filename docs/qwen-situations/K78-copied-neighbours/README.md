K78 (f02a, 2026-10-17): one block in CMakeLists.txt adding lines after `get_filename_component(SHADER_OUTPUT_DIR ...)` in
engine_add_shader. The Coder's SEARCH started one line lower than dictated and its REPLACE repeated the function's first three
lines, so they ended up in the file twice. Every check passed: the needles were all there, the dictated block stood as one
piece, nothing was deleted. Passed on attempt 1; Claude removed the 3 copied lines.
Rule: the check also counts the lines a job may add (diffjobs `added`); any line the file gained beyond them is a copy and fails
("these lines are in the file one time too many").
